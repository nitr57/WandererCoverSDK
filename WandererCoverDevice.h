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

#ifndef WANDERER_COVER_DEVICE_H
#define WANDERER_COVER_DEVICE_H

#include "WandererCoverSerialPort.h"
#include <memory>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <condition_variable>

namespace WandererCover
{
    /**
     * Device represents a Wanderer Cover device with its current state.
     *
     * Locking (acquire in this order, never the reverse):
     *   g_globalMutex -> ioMutex -> stateMutex
     * g_globalMutex only guards g_devices lookups and is never held while
     * taking a device lock or doing serial I/O.
     */
    struct Device
    {
        std::shared_ptr<SerialPort> port;
        std::string portName;
        std::atomic<bool> isOpen{false};

        /* Device info and status, updated from the status messages the device
         * broadcasts - guarded by stateMutex */
        std::string modelType;
        int firmwareVersion = 0;
        float closePositionAngle = 0.0f;
        float openPositionAngle = 0.0f;
        float currentPositionAngle = 0.0f;
        int brightness = 0;
        int heaterPower = 0;
        int asiairControl = 0;
        bool isMoving = false;
        std::chrono::steady_clock::time_point movingSince; /* When isMoving was last set */
        std::mutex stateMutex;

        /* Serializes Open, Close and command writes on this device. Callers
         * must never hold g_globalMutex while taking it. */
        std::mutex ioMutex;

        /* Handshake: handshakePending is cleared by the listener (under
         * handshakeMutex) when a status message has been parsed */
        std::mutex handshakeMutex;
        std::condition_variable handshakeCV;
        std::atomic<bool> handshakePending{false};

        /* Listener thread state */
        std::atomic<bool> statusListenerRunning{false};
        std::thread statusListenerThread;

        ~Device()
        {
            statusListenerRunning = false;
            if (statusListenerThread.joinable())
                statusListenerThread.join();
        }
    };

    /**
     * Global device registry mapping device IDs to Device objects.
     */
    extern std::map<int, std::shared_ptr<Device>> g_devices;

    /**
     * Global mutex protecting access to g_devices.
     */
    extern std::mutex g_globalMutex;

} /* namespace WandererCover */

#endif /* WANDERER_COVER_DEVICE_H */
