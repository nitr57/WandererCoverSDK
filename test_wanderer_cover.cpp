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

#include "WandererCoverSDK.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

void PrintMenu()
{
	printf("\n=== Wanderer Cover Control Menu ===\n");
	printf("1. Get device status\n");
	printf("2. Get device configuration\n");
	printf("3. Open cover\n");
	printf("4. Close cover (with angle)\n");
	printf("5. Set brightness level (0-255)\n");
	printf("6. Set heater power (0-3)\n");
	printf("7. Set ASIAIR control (0-3)\n");
	printf("8. Set open position angle\n");
	printf("9. Set close position angle\n");
	printf("10. Refresh and display all info\n");
	printf("11. Close device and exit\n");
	printf("> ");
}

void DisplayStatus(int deviceId)
{
	WC_COVER_STATUS status;
	WC_ERROR_TYPE result = WCCoverGetStatus(deviceId, &status);
	if (result == WC_SUCCESS)
	{
		printf("\n--- Current Status ---\n");
		printf("Current Position: %.2f°\n", status.currentPositionAngle);
		printf("Close Position: %.2f°\n", status.closePositionAngle);
		printf("Open Position: %.2f°\n", status.openPositionAngle);
		printf("Cover State: %s", status.coverState == 1 ? "OPEN" : (status.coverState == 0 ? "CLOSED" : (status.coverState == 2 ? "INTERMEDIATE" : "MOVING")));
	}
	else
	{
		printf("[FAIL] Failed to get status (Error: %d)\n", result);
	}
}

void DisplayConfig(int deviceId)
{
	WC_COVER_CONFIG config;
	WC_ERROR_TYPE result = WCCoverGetConfig(deviceId, &config);
	if (result == WC_SUCCESS)
	{
		printf("\n--- Current Configuration ---\n");
		printf("Brightness: %d\n", config.brightness);
		printf("Heater Power: %d\n", config.heaterPower);
		printf("ASIAIR Control: %d\n", config.asiairControl);
	}
	else
	{
		printf("[FAIL] Failed to get config (Error: %d)\n", result);
	}
}

void DisplayAllInfo(int deviceId)
{
	WC_VERSION version;
	WC_ERROR_TYPE result = WCCoverGetVersion(deviceId, &version);
	if (result == WC_SUCCESS)
	{
		printf("\n=== Device Information ===\n");
		printf("Model: %s\n", version.model);
		printf("Firmware: %u\n", version.firmware);
	}
	else
	{
		printf("[FAIL] Failed to get version (Error: %d)\n", result);
		return;
	}

	DisplayStatus(deviceId);
	DisplayConfig(deviceId);
}

int main(int argc, char *argv[])
{
	printf("=== Wanderer Cover Interactive Control ===\n\n");

	/* Get SDK version */
	char sdkVersion[32];
	WC_ERROR_TYPE result = WCGetSDKVersion(sdkVersion);
	if (result == WC_SUCCESS)
	{
		printf("SDK Version: %s\n\n", sdkVersion);
	}

	/* Scan for devices */
	printf("Scanning for Wanderer Cover devices...\n");
	int deviceCount = 32;
	int deviceIds[32] = {0};

	result = WCCoverScan(&deviceCount, deviceIds);
	if (result != WC_SUCCESS)
	{
		printf("[FAIL] Scan failed (Error: %d)\n", result);
		return 1;
	}

	printf("[OK] Found %d device(s)\n\n", deviceCount);

	if (deviceCount == 0)
	{
		printf("No Wanderer Cover devices found. Please connect a device.\n");
		return 0;
	}

	/* List found devices */
	printf("Available devices:\n");
	for (int i = 0; i < deviceCount; i++)
	{
		printf("  [%d] Device ID: %d\n", i, deviceIds[i]);
	}
	printf("\n");

	/* Select device */
	int deviceId = deviceIds[0];
	if (deviceCount > 1)
	{
		printf("Enter device index to use (0-%d) [default: 0]: ", deviceCount - 1);
		char input[10];
		if (fgets(input, sizeof(input), stdin) != NULL)
		{
			int idx = atoi(input);
			if (idx >= 0 && idx < deviceCount)
			{
				deviceId = deviceIds[idx];
			}
		}
	}

	printf("Using device: %d\n\n", deviceId);

	/* Open device */
	printf("Opening device...\n");
	result = WCCoverOpen(deviceId);
	if (result != WC_SUCCESS)
	{
		printf("[FAIL] Failed to open device (Error: %d)\n", result);
		return 1;
	}
	printf("[OK] Device opened\n");

	/* Display initial info */
	DisplayAllInfo(deviceId);

	/* Interactive menu */
	char input[256];
	bool running = true;

	while (running)
	{
		PrintMenu();
		fflush(stdout);

		if (fgets(input, sizeof(input), stdin) == NULL)
		{
			break;
		}

		int choice = atoi(input);

		switch (choice)
		{
		case 1:
			DisplayStatus(deviceId);
			break;

		case 2:
			DisplayConfig(deviceId);
			break;

		case 3:
		{
			printf("Opening cover...\n");
			result = WCCoverOpenCover(deviceId);
			if (result == WC_SUCCESS)
			{
				printf("[OK] Cover opened\n");
				usleep(1000000);  // Wait 1 second for status update
				DisplayStatus(deviceId);
			}
			else
			{
				printf("[FAIL] Failed to open cover (Error: %d)\n", result);
			}
			break;
		}

		case 4:
		{
			printf("Enter close angle (0-360): ");
			fflush(stdout);
			if (fgets(input, sizeof(input), stdin) != NULL)
			{
				float angle = atof(input);
				if (angle < 0.0f || angle > 360.0f)
				{
					printf("[FAIL] Invalid angle (0-360)\n");
					break;
				}

				printf("Closing cover...\n");
				result = WCCoverCloseCover(deviceId);
				if (result == WC_SUCCESS)
				{
					printf("[OK] Cover closed\n");
					usleep(1000000);  // Wait 1 second for status update
					DisplayStatus(deviceId);
				}
				else
				{
					printf("[FAIL] Failed to close cover (Error: %d)\n", result);
				}
			}
			break;
		}

		case 5:
		{
			printf("Enter brightness level (0-255): ");
			fflush(stdout);
			if (fgets(input, sizeof(input), stdin) != NULL)
			{
				int brightness = atoi(input);
				if (brightness < 0 || brightness > 255)
				{
					printf("[FAIL] Invalid brightness level (0-255)\n");
					break;
				}

				WC_COVER_CONFIG config;
				config.mask = MASK_COVER_BRIGHTNESS;
				config.brightness = brightness;

				result = WCCoverSetConfig(deviceId, &config);
				if (result == WC_SUCCESS)
				{
					printf("[OK] Brightness set to %d\n", brightness);
				}
				else
				{
					printf("[FAIL] Failed to set brightness (Error: %d)\n", result);
				}
			}
			break;
		}

		case 6:
		{
			printf("Enter heater power level (0-3): ");
			fflush(stdout);
			if (fgets(input, sizeof(input), stdin) != NULL)
			{
				int heaterPower = atoi(input);
				if (heaterPower < 0 || heaterPower > 3)
				{
					printf("[FAIL] Invalid heater power level (0-3)\n");
					break;
				}

				WC_COVER_CONFIG config;
				config.mask = MASK_COVER_HEATER_POWER;
				config.heaterPower = heaterPower;

				result = WCCoverSetConfig(deviceId, &config);
				if (result == WC_SUCCESS)
				{
					printf("[OK] Heater power set to %d\n", heaterPower);
				}
				else
				{
					printf("[FAIL] Failed to set heater power (Error: %d)\n", result);
				}
			}
			break;
		}

		case 7:
		{
			printf("Enter ASIAIR control level (0-3): ");
			fflush(stdout);
			if (fgets(input, sizeof(input), stdin) != NULL)
			{
				int asiairControl = atoi(input);
				if (asiairControl < 0 || asiairControl > 3)
				{
					printf("[FAIL] Invalid ASIAIR control level (0-3)\n");
					break;
				}

				WC_COVER_CONFIG config;
				config.mask = MASK_COVER_ASIAIR_CONTROL;
				config.asiairControl = asiairControl;

				result = WCCoverSetConfig(deviceId, &config);
				if (result == WC_SUCCESS)
				{
					printf("[OK] ASIAIR control set to %d\n", asiairControl);
				}
				else
				{
					printf("[FAIL] Failed to set ASIAIR control (Error: %d)\n", result);
				}
			}
			break;
		}

		case 8:
		{
			printf("Enter open position angle (0-360): ");
			fflush(stdout);
			if (fgets(input, sizeof(input), stdin) != NULL)
			{
				float angle = atof(input);
				if (angle < 0.0f || angle > 360.0f)
				{
					printf("[FAIL] Invalid angle (0-360)\n");
					break;
				}

				WC_COVER_CONFIG config;
				config.mask = MASK_COVER_OPEN_POSITION;
				config.openPositionAngle = angle;

				result = WCCoverSetConfig(deviceId, &config);
				if (result == WC_SUCCESS)
				{
					printf("[OK] Open position set to %.2f°\n", angle);
				}
				else
				{
					printf("[FAIL] Failed to set open position (Error: %d)\n", result);
				}
			}
			break;
		}

		case 9:
		{
			printf("Enter close position angle (0-360): ");
			fflush(stdout);
			if (fgets(input, sizeof(input), stdin) != NULL)
			{
				float angle = atof(input);
				if (angle < 0.0f || angle > 360.0f)
				{
					printf("[FAIL] Invalid angle (0-360)\n");
					break;
				}

				WC_COVER_CONFIG config;
				config.mask = MASK_COVER_CLOSE_POSITION;
				config.closePositionAngle = angle;

				result = WCCoverSetConfig(deviceId, &config);
				if (result == WC_SUCCESS)
				{
					printf("[OK] Close position set to %.2f°\n", angle);
				}
				else
				{
					printf("[FAIL] Failed to set close position (Error: %d)\n", result);
				}
			}
			break;
		}

		case 10:
			DisplayAllInfo(deviceId);
			break;

		case 11:
			running = false;
			break;

		default:
			printf("[FAIL] Unknown command\n");
			break;
		}
	}

	/* Close device */
	printf("\nClosing device...\n");
	result = WCCoverClose(deviceId);
	if (result == WC_SUCCESS)
	{
		printf("[OK] Device closed\n");
	}
	else
	{
		printf("[FAIL] Failed to close device (Error: %d)\n", result);
	}

	printf("\n=== Test Complete ===\n");
	return 0;
}
