/* *******************************************************************************
 * MIT License
 *
 * Copyright (c) 2025-2026 Nico Trost
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * **************************************************************************** */

#include "WandererCoverProtocol.h"
#include "WandererCoverLogging.h"
#include <cstring>
#include <cstdio>
#include <memory>
#include <chrono>
#include <thread>

namespace WandererCover
{
    bool SendCommand(std::shared_ptr<Device> device, const char *command)
    {
        if (!device || !device->port || !device->port->IsOpen())
        {
            WC_DEBUG("SendCommand: device=%p, port=%p, isOpen=%d",
                     device.get(), device ? device->port.get() : nullptr,
                     device && device->port ? device->port->IsOpen() : 0);
            return false;
        }

        // 100 ms delay
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        WC_DEBUG("SendCommand: Writing '%s'", command);
        if (!device->port->Write((const unsigned char *)command, strlen(command)))
        {
            WC_DEBUG("SendCommand: Write failed");
            return false;
        }

        return true;
    }

    /* The device stops broadcasting status while it moves. A status message
     * that was already on its way when a move was commanded must not clear
     * the moving flag, so only messages arriving this long after the command
     * count as "move finished". */
    static const int kMovingGraceMs = 150;

    /* Clear the moving flag if no status message arrived for this long after a
     * move command, so it can't stay set forever (e.g. a message was lost). */
    static const int kMaxMoveMs = 120000;

    /* Longest a blocked listener read can delay StopStatusListener */
    static const int kListenerSliceMs = 200;

    /* Message parsing helper functions */
    static void ParseStatusMessage(Device* device, const char *buffer)
    {
        int firmware, heaterPower, brightness, asiairControl = 0;
        float voltage, closePosition, openPosition, currentPosition;
        char model[8];
        int end = 0;
        int parsed;
        {
            ClassicNumericLocale numericLocale;

            // Try parsing with asiairControl field (newer firmware). %n after the
            // last 'A' rejects messages that were cut short.
            parsed = sscanf(buffer,
                            "WandererCover%7[^A]A%dA%fA%fA%fA%fA%dA%dA%dA%n",
                            model,
                            &firmware,
                            &closePosition,
                            &openPosition,
                            &currentPosition,
                            &voltage,
                            &brightness,
                            &heaterPower,
                            &asiairControl,
                            &end);

            // If that fails, try parsing without asiairControl field (older firmware)
            if (parsed != 9 || end == 0)
            {
                asiairControl = 0;  // Default value for older firmware
                end = 0;
                parsed = sscanf(buffer,
                                "WandererCover%7[^A]A%dA%fA%fA%fA%fA%dA%dA%n",
                                model,
                                &firmware,
                                &closePosition,
                                &openPosition,
                                &currentPosition,
                                &voltage,
                                &brightness,
                                &heaterPower,
                                &end);
                if (end == 0)
                {
                    parsed = 0;
                }
            }
        }
        
        if (parsed < 8)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(device->stateMutex);
            device->modelType = std::string(model);
            device->firmwareVersion = firmware;
            device->closePositionAngle = closePosition;
            device->openPositionAngle = openPosition;
            device->currentPositionAngle = currentPosition;
            device->brightness = brightness;
            device->asiairControl = asiairControl;

            // Heater power
            switch (heaterPower)
            {
            case 0:
                device->heaterPower = 0;
                break;
            case 50:
                device->heaterPower = 1;
                break;
            case 100:
                device->heaterPower = 2;
                break;
            case 150:
                device->heaterPower = 3;
                break;
            }

            /* Reset moving state, the device only sends data while not moving */
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - device->movingSince).count();
            if (elapsedMs > kMovingGraceMs)
            {
                device->isMoving = false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(device->handshakeMutex);
            device->handshakePending = false;
        }
        device->handshakeCV.notify_all();
    }

    /* Background listener thread body for status messages */
    static void StatusListenerThreadBody(Device* device)
    {
        char buffer[256];
        static const char kPrefix[] = "WandererCover";
        const size_t prefixLen = sizeof(kPrefix) - 1;

        while(device->statusListenerRunning)
        {
            if (!device->port || !device->port->IsOpen())
            {
                WC_DEBUG("StatusListener: Port not open, exiting");
                break;
            }

            /* Short slices so StopStatusListener (Close) is never delayed long */
            bool ioError = false;
            int len = device->port->ReadMessage((unsigned char *)buffer, sizeof(buffer), '\n', kListenerSliceMs, ioError);
            if (len > 0)
            {
                /* Status message. Use the last "WandererCover" in the line: after
                 * power-up the first line may start with junk bytes (possibly
                 * NULs, hence memmem), and a port opened mid-broadcast yields a
                 * truncated message followed by a complete one. */
                const char *status = nullptr;
                for (const char *p = buffer; p < buffer + len;)
                {
                    const char *found = (const char *)memmem(p, buffer + len - p, kPrefix, prefixLen);
                    if (!found)
                        break;
                    status = found;
                    p = found + 1;
                }

                if (status)
                {
                    ParseStatusMessage(device, status);
                }
            }
            else if (ioError)
            {
                /* e.g. the device was unplugged - nothing more will arrive */
                WC_ERROR("StatusListener: Serial port error, device disconnected?");
                break;
            }

            {
                std::lock_guard<std::mutex> lock(device->stateMutex);
                if (device->isMoving &&
                    std::chrono::steady_clock::now() - device->movingSince > std::chrono::milliseconds(kMaxMoveMs))
                {
                    WC_ERROR("StatusListener: No status from the device %d s after a move, assuming it stopped",
                             kMaxMoveMs / 1000);
                    device->isMoving = false;
                }
            }
        }
    }

    /* Background listener thread function for status messages */
    static void StatusListenerThreadFunc(Device* device)
    {
        try
        {
            StatusListenerThreadBody(device);
        }
        catch (...)
        {
            /* An exception escaping a std::thread would call std::terminate */
            WC_ERROR("StatusListener: unexpected exception, stopping");
        }

        try
        {
            std::lock_guard<std::mutex> lock(device->stateMutex);
            device->isMoving = false;
        }
        catch (...)
        {
        }
        device->statusListenerRunning = false;
        WC_DEBUG("StatusListener: exiting");
    }

    void StartStatusListener(std::shared_ptr<Device> device)
    {
        if (!device)
        {
            return;
        }

        /* Stop any existing listener and join it before starting a new one */
        device->statusListenerRunning = false;
        if (device->statusListenerThread.joinable())
            device->statusListenerThread.join();

        /* Start new listener thread */
        device->statusListenerRunning = true;
        device->statusListenerThread = std::thread(StatusListenerThreadFunc, device.get());
        WC_DEBUG("StartStatusListener: Listener thread started");
    }

    void StopStatusListener(std::shared_ptr<Device> device)
    {
        if (!device)
        {
            return;
        }

        /* Signal listener thread to stop and join it */
        device->statusListenerRunning = false;
        if (device->statusListenerThread.joinable())
            device->statusListenerThread.join();
        WC_DEBUG("StopStatusListener: Listener stopped");
    }
} /* namespace WandererCover */
