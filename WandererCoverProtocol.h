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
     * Read and parse device status message.
     *
     * Reads a status message from the serial port and updates the device state with
     * current values: firmware version, position angles, brightness, heater power, etc.
     * The device continuously broadcasts status messages, so this always gets fresh data.
     *
     * Device broadcasts in format:
     * WandererCover[model]A[firmware]A[closePos]A[openPos]A[currentPos]A[voltage]A[brightness]A[heater]A[asiair]A
     *
     * @param device Device to query status from
     * @return true if status was successfully read and parsed
     */
    bool QueryStatus(std::shared_ptr<Device> device);

    /**
     * Verify device connection with handshake.
     *
     * Attempts to read a device status message to confirm the device is present and
     * responding. Used during device open to validate the serial connection before
     * adding the device to the registry.
     *
     * @param device Device to verify
     * @return true if device responds with a valid WandererCover message
     */
    bool QueryHandshake(std::shared_ptr<Device> device);

} /* namespace WandererCover */

#endif /* WANDERER_COVER_PROTOCOL_H */
