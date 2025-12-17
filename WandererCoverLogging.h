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

#ifndef WANDERER_COVER_LOGGING_H
#define WANDERER_COVER_LOGGING_H

/* ============================================================================
 * WANDERER COVER SDK - LOGGING MODULE
 *
 * Compile-time controlled logging system for developers.
 * All logging is disabled in release builds unless explicitly enabled.
 * ============================================================================ */

namespace WandererCover
{
	/* Compile-time logging configuration */
	static constexpr bool WC_DEBUG_ENABLED = false; /* Disable debug logging by default */
	static constexpr bool WC_INFO_ENABLED = false;	/* Enable info logging */
	static constexpr bool WC_ERROR_ENABLED = true;	/* Enable error logging */
	static constexpr bool WC_TIMESTAMP_ENABLED = true; /* Enable timestamps in logs */

/* Logging macros - use these throughout the SDK */

/**
 * Debug logging macro.
 * Use for detailed diagnostic messages during development.
 * Controlled by WC_DEBUG_ENABLED compile-time flag.
 */
#define WC_DEBUG(fmt, ...)                                   \
	do                                                       \
	{                                                        \
		if (WandererCover::WC_DEBUG_ENABLED)               \
		{                                                    \
			WandererCover::WRLogDebug(fmt, ##__VA_ARGS__); \
		}                                                    \
	} while (0)

/**
 * Info logging macro.
 * Use for informational messages about normal operations.
 * Controlled by WC_INFO_ENABLED compile-time flag.
 */
#define WC_INFO(fmt, ...)                                   \
	do                                                      \
	{                                                       \
		if (WandererCover::WC_INFO_ENABLED)               \
		{                                                   \
			WandererCover::WRLogInfo(fmt, ##__VA_ARGS__); \
		}                                                   \
	} while (0)

/**
 * Error logging macro.
 * Use for error messages and failure conditions.
 * Always enabled by default (WC_ERROR_ENABLED = true).
 */
#define WC_ERROR(fmt, ...)                                   \
	do                                                       \
	{                                                        \
		if (WandererCover::WC_ERROR_ENABLED)               \
		{                                                    \
			WandererCover::WRLogError(fmt, ##__VA_ARGS__); \
		}                                                    \
	} while (0)

	/**
	 * Log a debug message with optional timestamp.
	 * Only outputs if WC_DEBUG_ENABLED is true.
	 * Supports printf-style format strings.
	 */
	void WRLogDebug(const char *fmt, ...);

	/**
	 * Log an info message with optional timestamp.
	 * Only outputs if WC_INFO_ENABLED is true.
	 * Supports printf-style format strings.
	 */
	void WRLogInfo(const char *fmt, ...);

	/**
	 * Log an error message with optional timestamp.
	 * Enabled by default and should always be called on error conditions.
	 * Supports printf-style format strings.
	 */
	void WRLogError(const char *fmt, ...);

	/**
	 * Get current timestamp string for logging.
	 * Optionally includes in log output if WC_TIMESTAMP_ENABLED is true.
	 *
	 * @return Pointer to static timestamp string buffer
	 */
	const char *WRGetTimestamp();

} /* namespace WandererCover */

#endif /* WANDERER_COVER_LOGGING_H */
