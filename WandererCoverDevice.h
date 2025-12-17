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

#ifndef WANDERER_COVER_DEVICE_H
#define WANDERER_COVER_DEVICE_H

#include "WandererCoverSerialPort.h"
#include <memory>
#include <string>
#include <map>
#include <mutex>

namespace WandererCover
{
	/**
	 * Device represents a Wanderer Cover device with its current state.
	 */
	struct Device
	{
		std::shared_ptr<SerialPort> port;
		std::string portName;
		std::string modelType;
		int firmwareVersion = 0;
		float closePositionAngle = 0.0f;
		float openPositionAngle = 0.0f;
		float currentPositionAngle = 0.0f;
		float inputVoltage = 0.0f;
		int brightness = 0;
		int heaterPower = 0;
		int asiairControl = 0;
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
