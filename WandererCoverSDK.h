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

#ifdef _WINDOWS
#define WCAPI __declspec(dllexport)
#else
#define WCAPI
#endif

#define WC_MAX_NUM 32	  /* Maximum cover numbers supported by this SDK */
#define WC_VERSION_LEN 32 /* Buffer length for version strings */

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

	typedef struct _WC_VERSION
	{
		unsigned int firmware; /* Cover firmware version */
		char model[8];		   /* Model type (e.g., "Lite", "Mini") */
	} WC_VERSION;

	typedef struct _WC_COVER_CONFIG
	{
		unsigned int mask;		  /* Used by WRCoverSetConfig() to indicate which field wants to be set */
		float openPositionAngle;  /* Open position angle in degrees */
		float closePositionAngle; /* Close position angle in degrees */
		int brightness;			  /* Brightness level */
		int heaterPower;		  /* Heater power level */
		int asiairControl;		  /* ASIAIR control setting */
	} WC_COVER_CONFIG;

	typedef struct _WC_COVER_STATUS
	{
		int coverState;				/* Current cover state (0 = close, 1 = open)*/
		float currentPositionAngle; /* Current motor position angle */
		float inputVoltage;			/* Input voltage */
		float closePositionAngle;	/* Closed position angle */
		float openPositionAngle;	/* Open position angle */
	} WC_COVER_STATUS;

	/* Device scanning and management */
	WCAPI WC_ERROR_TYPE WCCoverScan(int *number, int *ids);
	WCAPI WC_ERROR_TYPE WCCoverOpen(int id);
	WCAPI WC_ERROR_TYPE WCCoverClose(int id);

	/* Configuration */
	WCAPI WC_ERROR_TYPE WCCoverGetConfig(int id, WC_COVER_CONFIG *config);
	WCAPI WC_ERROR_TYPE WCCoverSetConfig(int id, WC_COVER_CONFIG *config);

	/* Status and information */
	WCAPI WC_ERROR_TYPE WCCoverGetStatus(int id, WC_COVER_STATUS *status);
	WCAPI WC_ERROR_TYPE WCCoverGetVersion(int id, WC_VERSION *version);

	/* Motion control */
	WCAPI WC_ERROR_TYPE WCCoverOpenCover(int id);
	WCAPI WC_ERROR_TYPE WCCoverCloseCover(int id);

	/* Utility */
	WCAPI WC_ERROR_TYPE WCGetSDKVersion(char *version);

#ifdef __cplusplus
}
#endif

#endif /* WANDERER_COVER_SDK_H */
