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

#ifndef WANDERER_COVER_SERIAL_PORT_H
#define WANDERER_COVER_SERIAL_PORT_H

/* ============================================================================
 * WANDERER COVER SDK - SERIAL PORT MODULE
 *
 * Low-level serial port communication with select()-based timeout handling.
 * ============================================================================ */

namespace WandererCover
{
	class SerialPort
	{
	private:
		int fd = -1;

	public:
		SerialPort() {}
		~SerialPort() { Close(); }

		/**
		 * Open a serial port device.
		 * @param portName Device path (e.g., "/dev/ttyUSB0")
		 * @return true if successfully opened and configured
		 */
		bool Open(const char *portName);

		/**
		 * Close the serial port.
		 */
		void Close();

		/**
		 * Write data to the serial port.
		 * @param data Buffer containing data to write
		 * @param len Number of bytes to write
		 * @return true if all bytes were successfully written
		 */
		bool Write(const unsigned char *data, int len);

		/**
		 * Read a complete line from the serial port with timeout.
		 *
		 * Performs a blocking read that synchronizes to line boundaries and reads exactly
		 * one complete message terminated by newline. The input buffer is flushed at the start
		 * to discard any stale data, ensuring fresh responses to commands.
		 *
		 * Reading process:
		 * 1. Flushes input buffer (tcflush TCIFLUSH) to discard stale data
		 * 2. Synchronizes to line boundary by finding the next \\n
		 * 3. Reads complete line from start marker through \\n
		 * 4. Replaces \\n with \\0 for null-terminated string
		 *
		 * This approach handles continuous device broadcasts where the buffer may contain
		 * multiple messages or partial messages.
		 *
		 * @param buf Buffer to read data into
		 * @param maxlen Maximum number of bytes to read (including null terminator)
		 * @param timeoutMs Timeout in milliseconds
		 * @return Number of bytes read (0 on timeout or error)
		 */
		int Read(unsigned char *buf, int maxlen, int timeoutMs);

		/**
		 * Check if the serial port is open.
		 * @return true if port is open
		 */
		bool IsOpen() { return fd >= 0; }

		/**
		 * Get the file descriptor for the serial port.
		 * @return File descriptor or -1 if closed
		 */
		int GetFD() { return fd; }
	};

} /* namespace WandererCover */

#endif /* WANDERER_COVER_SERIAL_PORT_H */
