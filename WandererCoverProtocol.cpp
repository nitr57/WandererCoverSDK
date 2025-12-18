/* *******************************************************************************
 * MIT License
 *
 * Copyright (c) 2025 Nico Trost
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
#include <unistd.h>
#include <termios.h>
#include <memory>

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
        usleep(100000);

        WC_DEBUG("SendCommand: Writing '%s'", command);
        if (!device->port->Write((const unsigned char *)command, strlen(command)))
        {
            WC_DEBUG("SendCommand: Write failed");
            return false;
        }

        return true;
    }

    bool QueryHandshake(std::shared_ptr<Device> device)
    {
        if (!device || !device->port)
        {
            return false;
        }

        WC_DEBUG("QueryHandshake: started for device %s", device->portName.c_str());

        if (!device->port->IsOpen())
        {
            WC_DEBUG("QueryHandshake: Port not open");
            return false;
        }

        // No need to send a request, the cover keeps sending its status
        char response[32];
        if (device->port->Read((unsigned char *)response, 32, 3000))
        {
            if (strstr(response, "WandererCover") != NULL)
            {
                return true;
            }
        }

        return false;
    }

    bool QueryStatus(std::shared_ptr<Device> device)
    {
        if (!device || !device->port)
        {
            WC_DEBUG("QueryStatus: invalid device");
            return false;
        }

        WC_DEBUG("QueryStatus: started for device %s", device->portName.c_str());

        if (!device->port->IsOpen())
        {
            WC_DEBUG("QueryStatus: Port not open");
            return false;
        }

        // Read cover status
        char response[64];
        if (device->port->Read((unsigned char *)response, 64, 3000))
        {
            float voltage;
            int heaterPower;
            char model[8];
            if (sscanf(response,
                       "WandererCover%7[^A]A%dA%fA%fA%fA%fA%dA%dA%dA",
                       model,
                       &device->firmwareVersion,
                       &device->closePositionAngle,
                       &device->openPositionAngle,
                       &device->currentPositionAngle,
                       &voltage,
                       &device->brightness,
                       &heaterPower,
                       &device->asiairControl) != 9)
            {
                WC_DEBUG("QueryStatus: invalid message %s", response);
                return false;
            }

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

            device->modelType = std::string(model);
        }
        else
        {
            WC_DEBUG("QueryStatus: timeout reading model from serial");
            return false;
        }

        WC_DEBUG("QueryStatus: Successfully parsed, model=%s", device->modelType.c_str());
        return true;
    }

    /* Background listener thread function for movement completion */
    static void MoveListenerThreadFunc(std::shared_ptr<Device> device)
    {
        if (!device || !device->port)
        {
            return;
        }

        WC_DEBUG("MoveListener: Started for device %s", device->portName.c_str());

        if (!device->port->IsOpen())
        {
            WC_DEBUG("MoveListener: Port not open, exiting");
            device->listenerRunning = false;
            return;
        }

        device->movingState = 1;
        char buffer[8];

        // Read the next incoming line which is "done" with a 60sec timeout
        if (device->port->Read((unsigned char *)buffer, 8, 60000))
        {
            WC_INFO("Returning from cover movement");
            device->listenerRunning = false;
            device->movingState = 0;

            // Run a query to get the current angle
            QueryStatus(device);
            return;
        }
        else
        {
            WC_DEBUG("MoveListener: Timeout reading from port");
            device->listenerRunning = false;
            // State unknown
            device->movingState = 2;
            return;
        }
    }

    void StartMoveListener(std::shared_ptr<Device> device)
    {
        if (!device)
        {
            return;
        }

        /* Stop any existing listener by setting the flag */
        device->listenerRunning = false;

        /* Small delay to let old thread exit if it's still running */
        usleep(50000);

        /* Start new listener thread */
        device->listenerRunning = true;
        std::thread listenerThread(MoveListenerThreadFunc, device);
        listenerThread.detach(); /* Detach immediately - let it run independently */
        WC_DEBUG("StartMoveListener: Listener thread started");
    }

    void StopMoveListener(std::shared_ptr<Device> device)
    {
        if (!device)
        {
            return;
        }

        /* Signal listener thread to stop */
        device->listenerRunning = false;
        WC_DEBUG("StopMoveListener: Listener stop requested");
    }
} /* namespace WandererCover */
