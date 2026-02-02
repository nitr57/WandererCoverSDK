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
    bool SendCommand(std::shared_ptr<Device> device, const char *command, int timeoutMs)
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

    /* Message parsing helper functions */
    static void ParseStatusMessage(std::shared_ptr<Device> device, const char *buffer)
    {
        int firmware, heaterPower, brightness, asiairControl;
        float voltage, closePosition, openPosition, currentPosition;
        char model[8];
        if (sscanf(buffer,
                   "WandererCover%7[^A]A%dA%fA%fA%fA%fA%dA%dA%dA",
                   model,
                   &firmware,
                   &closePosition,
                   &openPosition,
                   &currentPosition,
                   &voltage,
                   &brightness,
                   &heaterPower,
                   &asiairControl) == 9)
        {
            // Store in device
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

            device->handshakePending = false;
        }

        device->handshakeCV.notify_one();
    }

    /* Background listener thread function for status messages */
    static void StatusListenerThreadFunc(std::shared_ptr<Device> device)
    {
        char buffer[256];

        while(device->statusListenerRunning)
        {
            if (!device || !device->port)
            {
                WC_DEBUG("StatusListener: Port unavailable, exiting");
                device->statusListenerRunning = false;
                return;
            }

            if (!device->port->IsOpen())
            {
                WC_DEBUG("StatusListener: Port not open, exiting");
                device->statusListenerRunning = false;
                return;
            }

            if (device->port->Read((unsigned char *)buffer, 256, '\n', 60000))
            {
                /* Parse different message types based on prefix */
                if (strstr(buffer, "WandererCover") == buffer)
                {
                    /* Status message */
                    ParseStatusMessage(device, buffer);

                    /* Reset moving state, the device only sends data while not moving */
                    {
                        std::lock_guard<std::mutex> lock(device->movingStateMutex);
                        device->isMoving = false;
                    }
                }
            }
        }

        WC_DEBUG("StatusListener: exiting");
    }

    void StartStatusListener(std::shared_ptr<Device> device)
    {
        if (!device)
        {
            return;
        }

        /* Stop any existing listener by setting the flag */
        device->statusListenerRunning = false;

        /* Small delay to let old thread exit if it's still running */
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        /* Start new listener thread */
        device->statusListenerRunning = true;
        std::thread listenerThread(StatusListenerThreadFunc, device);
        listenerThread.detach(); /* Detach immediately - let it run independently */
        WC_DEBUG("StartStatusListener: Listener thread started");
    }

    void StopStatusListener(std::shared_ptr<Device> device)
    {
        if (!device)
        {
            return;
        }

        /* Signal listener thread to stop */
        device->statusListenerRunning = false;
        WC_DEBUG("StopStatusListener: Listener stop requested");
    }
} /* namespace WandererCover */
