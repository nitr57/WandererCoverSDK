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
#include <locale.h>

namespace WandererCover
{
    /**
     * While in scope, numeric conversions on this thread (sscanf/snprintf %f)
     * use '.' as the decimal separator, like the device does - whatever
     * LC_NUMERIC the host application has set (e.g. "," with a German locale).
     */
    class ClassicNumericLocale
    {
    public:
        ClassicNumericLocale() : previous(uselocale(Classic())) {}
        ~ClassicNumericLocale() { uselocale(previous); }
        ClassicNumericLocale(const ClassicNumericLocale &) = delete;
        ClassicNumericLocale &operator=(const ClassicNumericLocale &) = delete;

    private:
        static locale_t Classic()
        {
            static locale_t classic = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
            return classic;
        }
        locale_t previous;
    };

    /**
     * Send a command to the device.
     *
     * Sends a command string to the device via serial port. The device continuously
     * broadcasts its status, which the status listener picks up. Waits 100ms before
     * writing so consecutive commands are not run together.
     *
     * @param device Device to send command to
     * @param command Command string, written as is (include any terminator)
     * @return true if command was successfully transmitted
     */
    bool SendCommand(std::shared_ptr<Device> device, const char *command);

    /**
     * Start listening for status messages.
     * Spawns a background thread that parses the device's status broadcasts
     * until StopStatusListener is called (or the port fails). Called by Open
     * (and the scan probe) once the port is open.
     *
     * @param device Device to listen on
     */
    void StartStatusListener(std::shared_ptr<Device> device);

    /**
     * Stop listening for status messages.
     * Terminates the background listener thread. Called by Close before the
     * port is closed.
     *
     * @param device Device to stop listening on
     */
    void StopStatusListener(std::shared_ptr<Device> device);

} /* namespace WandererCover */

#endif /* WANDERER_COVER_PROTOCOL_H */
