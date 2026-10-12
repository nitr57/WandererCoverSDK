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

#ifndef WANDERER_COVER_SDK_H
#define WANDERER_COVER_SDK_H

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(_WIN32)
#if defined(WANDERER_COVER_SDK_BUILD)
#define WCAPI __declspec(dllexport)
#else
#define WCAPI __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define WCAPI __attribute__((visibility("default")))
#else
#define WCAPI
#endif

#define WC_MAX_NUM 32	  /* Maximum cover numbers supported by this SDK */
#define WC_VERSION_LEN 32 /* Buffer length for version strings */
#define WC_PORT_LEN 256	  /* Buffer length for port names */

	typedef enum _WC_ERROR_TYPE
	{
		WC_SUCCESS = 0,				/* Success */
		WC_ERROR_INVALID_ID,		/* Device ID is invalid */
		WC_ERROR_INVALID_PARAMETER, /* One or more parameters are invalid */
		WC_ERROR_INVALID_STATE,		/* Device is not in correct state for specific API call */
		WC_ERROR_COMMUNICATION,		/* Data communication error such as device has been removed from USB port */
		WC_ERROR_NULL_POINTER,		/* Caller passes null-pointer parameter which is not expected */
	} WC_ERROR_TYPE;

/*
 * Used by WCxxxSetConfig() to indicate which field wants to be set
 */
#define MASK_COVER_BRIGHTNESS 0x01
#define MASK_COVER_HEATER_POWER 0x02
#define MASK_COVER_ASIAIR_CONTROL 0x04
#define MASK_COVER_OPEN_POSITION 0x08
#define MASK_COVER_CLOSE_POSITION 0x10
#define MASK_COVER_ALL 0x1F

/* Values of WC_COVER_STATUS.coverState */
#define WC_COVER_STATE_CLOSED 0
#define WC_COVER_STATE_OPEN 1
#define WC_COVER_STATE_INTERMEDIATE 2
#define WC_COVER_STATE_MOVING 3

	typedef struct _WC_VERSION
	{
		unsigned int firmware; /* Cover firmware version */
		char model[8];		   /* Model type (e.g., "Lite", "Mini") */
	} WC_VERSION;

	typedef struct _WC_COVER_CONFIG
	{
		unsigned int mask;		  /* Used by WCCoverSetConfig() to indicate which field wants to be set */
		float openPositionAngle;  /* Open position angle in degrees */
		float closePositionAngle; /* Close position angle in degrees */
		int brightness;			  /* Brightness level */
		int heaterPower;		  /* Heater power level */
		int asiairControl;		  /* ASIAIR control: 0 - off, others - on */
	} WC_COVER_CONFIG;

	typedef struct _WC_COVER_STATUS
	{
		int coverState;				/* Current cover state, one of WC_COVER_STATE_* */
		float currentPositionAngle; /* Current motor position angle */
		float closePositionAngle;	/* Closed position angle */
		float openPositionAngle;	/* Open position angle */
	} WC_COVER_STATUS;

	/* Device scanning and management */
	/* ids must have room for WC_MAX_NUM entries; *number receives the count written */
	WCAPI WC_ERROR_TYPE WCCoverScan(int *number, int *ids);
	/* Probes only the given port (as returned by WCCoverGetPort, or a device
	 * node such as /dev/ttyUSB0) instead of all CH340 ports, so no other device
	 * is touched. *id receives the device's id; a device already registered on
	 * that port keeps its id and is not probed again. Returns
	 * WC_ERROR_COMMUNICATION if no cover answers there - fall back to
	 * WCCoverScan then. */
	WCAPI WC_ERROR_TYPE WCCoverScanPort(const char *port, int *id);
	/* port must have room for WC_PORT_LEN bytes. Receives the device's
	 * /dev/serial/by-path/... link, which stays the same across reconnects and
	 * reboots as long as the cover is plugged into the same USB socket, or its
	 * device node if there is no such link. The CH340 has no USB serial number,
	 * so /dev/serial/by-id/... cannot tell two such devices apart. */
	WCAPI WC_ERROR_TYPE WCCoverGetPort(int id, char *port);
	WCAPI WC_ERROR_TYPE WCCoverOpen(int id);
	WCAPI WC_ERROR_TYPE WCCoverClose(int id);

	/* Configuration */
	WCAPI WC_ERROR_TYPE WCCoverGetConfig(int id, WC_COVER_CONFIG *config);
	/* Fails with WC_ERROR_INVALID_STATE while the cover is moving or the device
	 * is not opened. closePositionAngle must be below 300 degrees.
	 * WCCoverGetConfig sets config->mask to 0; set the bits of the fields to
	 * change before passing the struct to WCCoverSetConfig. */
	WCAPI WC_ERROR_TYPE WCCoverSetConfig(int id, const WC_COVER_CONFIG *config);

	/* Status and information */
	WCAPI WC_ERROR_TYPE WCCoverGetStatus(int id, WC_COVER_STATUS *status);
	WCAPI WC_ERROR_TYPE WCCoverGetVersion(int id, WC_VERSION *version);

	/* Motion control
	 * Calls on an opened device whose USB connection was lost return
	 * WC_ERROR_COMMUNICATION until it is reopened (WCCoverOpen reconnects). */
	WCAPI WC_ERROR_TYPE WCCoverOpenCover(int id);
	WCAPI WC_ERROR_TYPE WCCoverCloseCover(int id);

	/* Utility */
	/* version must have room for WC_VERSION_LEN bytes */
	WCAPI WC_ERROR_TYPE WCGetSDKVersion(char *version);

#ifdef __cplusplus
}
#endif

#endif /* WANDERER_COVER_SDK_H */
