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

#ifndef WANDERER_COVER_PROTOCOL_H
#define WANDERER_COVER_PROTOCOL_H

#include "WandererCoverDevice.h"

namespace WandererCover
{
    /**
     * Send a command to the device.
     *
     * Sends a command string to the device via serial port. The device continuously
     * broadcasts its status, so responses are handled by subsequent QueryStatus() calls.
     * A small delay is automatically inserted between commands to allow device processing.
     *
     * @param device Device to send command to
     * @param command Command string without newline (newline is added automatically)
     * @param timeoutMs Timeout in milliseconds for write operation (default 3000ms)
     * @return true if command was successfully transmitted
     */
    bool SendCommand(std::shared_ptr<Device> device, const char *command, int timeoutMs = 3000);

    /**
     * Start listening for movement completion messages.
     * Spawns a background thread that reads serial data until movement finishes.
     * Should be called before triggering a move command.
     *
     * @param device Device to listen on
     */
    void StartStatusListener(std::shared_ptr<Device> device);

    /**
     * Stop listening for movement completion messages.
     * Terminates the background listener thread.
     * Should be called after movement finishes.
     *
     * @param device Device to stop listening on
     */
    void StopStatusListener(std::shared_ptr<Device> device);

} /* namespace WandererCover */

#endif /* WANDERER_COVER_PROTOCOL_H */
