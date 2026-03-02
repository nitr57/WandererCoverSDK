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

#include "WandererCoverSDK.h"
#include "WandererCoverLogging.h"
#include "WandererCoverDevice.h"
#include "WandererCoverProtocol.h"
#include "WandererCoverSerialPort.h"
#include <map>
#include <mutex>
#include <thread>
#include <memory>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>
#include <cctype>
#include <mutex>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <dirent.h>
#include <libudev.h>

#define SDK_VERSION "1.3.0"

/* Import internal implementation for use in public C API */
using namespace WandererCover;

/* Helper function to send a command and wait for the response with timeout */
static bool SendAndWaitForReply(std::shared_ptr<WandererCover::Device> device,
                                const char *command,
                                std::mutex &configMutex,
                                std::condition_variable &configCV,
                                std::atomic<bool> &configPending,
                                const char *timeoutMsg,
                                int timeoutMs = 1000)
{
    {
        std::lock_guard<std::mutex> lock(configMutex);
        configPending = true;
    }

    if(command != nullptr)
    {
        if (!device->port->Write((const unsigned char *)command, strlen(command)))
        {
            WC_DEBUG("SendAndWaitForReply: Failed to send %s command", command);
            std::lock_guard<std::mutex> lock(configMutex);
            configPending = false;
            return false;
        }
    }

    /* Wait for config to be received with specified timeout */
    {
        std::unique_lock<std::mutex> lock(configMutex);
        configCV.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                         [&configPending]() { return !configPending; });
        if (configPending)
        {
            WC_DEBUG("SendAndWaitForReply: Timeout waiting for %s (timeout=%dms)", timeoutMsg, timeoutMs);
            configPending = false;
            return false;
        }
    }

    return true;
}

/* Structure for parallel device scanning */
struct ScanWorkerTask
{
    std::string portName;
    std::shared_ptr<WandererCover::Device> device;
    bool isValid;
    
    ScanWorkerTask(const char *port) : portName(port), isValid(false) {}
};

/* Worker thread function for testing a single device */
static void ScanWorkerThread(ScanWorkerTask &task)
{
    auto port = std::make_shared<SerialPort>();
    
    /* Use minimal retry for scanning - fail fast if port is busy */
    /* This prevents hanging when other apps are also scanning */
    port->SetRetryParams(1, 10);  /* 1 retry, 10ms delay = ~10ms total wait */
    
    if (!port->Open(task.portName.c_str()))
    {
        WC_DEBUG("ScanWorkerThread: Failed to open port %s (skipped, may be in use by another app)", task.portName.c_str());
        return;
    }

    auto tempDevice = std::make_shared<Device>();
    tempDevice->port = port;
    tempDevice->portName = task.portName;

    // Send dummy command to wake up device
    SendCommand(tempDevice, "\n");

    // Start status listener thread
    StartStatusListener(tempDevice);

    // Perform handshake (just wait for response, no command)
    if(SendAndWaitForReply(tempDevice,
                           nullptr,
                           tempDevice->handshakeMutex,
                           tempDevice->handshakeCV,
                           tempDevice->handshakePending,
                           "handshake"))
    {
        WC_DEBUG("ScanWorkerThread: Valid device found on %s", task.portName.c_str());

        /* Stop listener */
        StopStatusListener(tempDevice);

        /* Valid device found - close port, will be reopened in WCOpen */
        port->Close();
        
        task.device = tempDevice;
        task.isValid = true;
    }
    else
    {
        WC_DEBUG("ScanWorkerThread: No response from device on %s", task.portName.c_str());
        /* Not a valid Wanderer device, close port */
        port->Close();
    }
}

/* ============================================================================
 * PUBLIC SDK API IMPLEMENTATION
 * ============================================================================ */

WCAPI WC_ERROR_TYPE WCGetSDKVersion(char *version)
{
    if (!version)
    {
        return WC_ERROR_NULL_POINTER;
    }

    strncpy(version, SDK_VERSION, WC_VERSION_LEN - 1);
    version[WC_VERSION_LEN - 1] = '\0';
    return WC_SUCCESS;
}

WCAPI WC_ERROR_TYPE WCCoverScan(int *number, int *ids)
{
    if (!number || !ids)
    {
        return WC_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    int count = 0;

    /* Create udev context */
    struct udev *udev = udev_new();
    if (!udev)
    {
        return WC_ERROR_COMMUNICATION;
    }

    /* Create enumeration for tty devices */
    struct udev_enumerate *enumerate = udev_enumerate_new(udev);
    if (!enumerate)
    {
        udev_unref(udev);
        return WC_ERROR_COMMUNICATION;
    }

    /* Filter for tty subsystem */
    udev_enumerate_add_match_subsystem(enumerate, "tty");
    udev_enumerate_scan_devices(enumerate);

    struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *entry;

    /* Step 1: Collect all candidate CH340 devices */
    std::vector<std::string> candidatePorts;
    udev_list_entry_foreach(entry, devices)
    {
        const char *path = udev_list_entry_get_name(entry);
        struct udev_device *device = udev_device_new_from_syspath(udev, path);
        if (!device)
        {
            continue;
        }

        /* Get the parent USB device */
        struct udev_device *parent = udev_device_get_parent_with_subsystem_devtype(
            device, "usb", "usb_device");

        if (!parent)
        {
            udev_device_unref(device);
            continue;
        }

        /* Check VID and PID for CH340 (1a86:7523) */
        const char *vid = udev_device_get_sysattr_value(parent, "idVendor");
        const char *pid = udev_device_get_sysattr_value(parent, "idProduct");

        if (!vid || !pid)
        {
            udev_device_unref(device);
            continue;
        }

        if (strcmp(vid, "1a86") != 0 || strcmp(pid, "7523") != 0)
        {
            udev_device_unref(device);
            continue;
        }

        /* Get the device node (e.g., /dev/ttyUSB0) */
        const char *deviceNode = udev_device_get_devnode(device);
        if (deviceNode)
        {
            WC_DEBUG("Found CH340 device: %s", deviceNode);
            candidatePorts.push_back(std::string(deviceNode));
        }

        udev_device_unref(device);
    }

    /* Step 2: Scan candidate devices in parallel */
    std::vector<ScanWorkerTask> tasks;
    std::vector<std::thread> workerThreads;

    for (const auto &port : candidatePorts)
    {
        if (count >= WC_MAX_NUM)
            break;
        tasks.emplace_back(port.c_str());
        count++;
    }

    /* Spawn worker threads for each candidate port */
    for (auto &task : tasks)
    {
        workerThreads.emplace_back(ScanWorkerThread, std::ref(task));
    }

    /* Wait for all threads to complete */
    for (auto &thread : workerThreads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    /* Step 3: Collect valid devices */
    count = 0;
    for (auto &task : tasks)
    {
        if (task.isValid && count < WC_MAX_NUM)
        {
            int id = count;
            g_devices[id] = task.device;
            ids[count] = id;
            count++;
        }
    }

    /* Clean up udev resources */
    udev_enumerate_unref(enumerate);
    udev_unref(udev);

    *number = count;
    return WC_SUCCESS;
}

WCAPI WC_ERROR_TYPE WCCoverOpen(int id)
{
    std::lock_guard<std::mutex> lock(g_globalMutex);
    WC_DEBUG("WCCoverOpen: Opening device id=%d", id);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        WC_ERROR("WCCoverOpen: Device id=%d not found", id);
        return WC_ERROR_INVALID_ID;
    }

    auto device = it->second;
    WC_DEBUG("WCCoverOpen: Found device, portName=%s", device->portName.c_str());

    /* Create a new SerialPort instance and open it */
    if (!device->port)
    {
        WC_DEBUG("WCCoverOpen: Creating new SerialPort instance");
        device->port = std::make_shared<SerialPort>();
        /* Use standard retry parameters for normal device open (more tolerant than scan) */
        /* Default: 3 retries with 200ms delay = ~600ms max wait time */
        device->port->SetRetryParams(3, 200);
    }

    WC_DEBUG("WCCoverOpen: Attempting to open port %s", device->portName.c_str());
    if (!device->port->Open(device->portName.c_str()))
    {
        WC_ERROR("WCCoverOpen: Failed to open port");
        return WC_ERROR_COMMUNICATION;
    }

    // Send dummy command to wake up device
    SendCommand(device, "\n");

    WC_DEBUG("WCCoverOpen: Port opened successfully, performing handshake");
    // Start status listener thread
    StartStatusListener(device);

    // Perform handshake
    if(!SendAndWaitForReply(device, nullptr, device->handshakeMutex, device->handshakeCV,
                            device->handshakePending, "handshake"))
    {
        WC_ERROR("WDCoverOpen: Handshake failed");
        device->port->Close();
        return WC_ERROR_COMMUNICATION;
    }

    WC_INFO("[OK] Device opened");
    return WC_SUCCESS;
}

WCAPI WC_ERROR_TYPE WCCoverClose(int id)
{
    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return WC_ERROR_INVALID_ID;
    }

    auto device = it->second;

    if (device->port)
    {
        device->port->Close();
    }

    WC_INFO("[OK] Device closed");
    return WC_SUCCESS;
}

WCAPI WC_ERROR_TYPE WCCoverGetConfig(int id, WC_COVER_CONFIG *config)
{
    if (!config)
    {
        return WC_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return WC_ERROR_INVALID_ID;
    }

    auto device = it->second;

    config->brightness = device->brightness;
    config->heaterPower = device->heaterPower;
    config->asiairControl = device->asiairControl;

    return WC_SUCCESS;
}

WCAPI WC_ERROR_TYPE WCCoverSetConfig(int id, WC_COVER_CONFIG *config)
{
    if (!config)
    {
        return WC_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return WC_ERROR_INVALID_ID;
    }

    auto device = it->second;

    if (config->mask & MASK_COVER_OPEN_POSITION)
    {
        if (config->openPositionAngle < 0 || config->openPositionAngle > 359.9)
        {
            return WC_ERROR_INVALID_PARAMETER;
        }

        // Send command: 40000 + x*100
        int angle = 40000 + config->openPositionAngle * 100;
        char cmd[8];
        snprintf(cmd, sizeof(cmd), "%d\n", angle);

        if (!SendCommand(device, cmd))
        {
            return WC_ERROR_COMMUNICATION;
        }

        device->openPositionAngle = config->openPositionAngle;
    }

    if (config->mask & MASK_COVER_CLOSE_POSITION)
    {
        if (config->closePositionAngle < 0 || config->closePositionAngle > 359.9)
        {
            return WC_ERROR_INVALID_PARAMETER;
        }

        // Send command: 10000 + x*100
        int angle = 10000 + config->closePositionAngle * 100;
        char cmd[8];
        snprintf(cmd, sizeof(cmd), "%d\n", angle);

        if (!SendCommand(device, cmd))
        {
            return WC_ERROR_COMMUNICATION;
        }

        device->closePositionAngle = config->closePositionAngle;
    }

    if (config->mask & MASK_COVER_BRIGHTNESS)
    {
        if (config->brightness < 0 || config->brightness > 255)
        {
            return WC_ERROR_INVALID_PARAMETER;
        }

        // Send brightness command: 1-255 (0 means turn off)
        char cmd[16];
        if (config->brightness == 0)
        {
            snprintf(cmd, sizeof(cmd), "9999\n");
        }
        else
        {
            snprintf(cmd, sizeof(cmd), "%d\n", config->brightness);
        }

        if (!SendCommand(device, cmd))
        {
            return WC_ERROR_COMMUNICATION;
        }

        device->brightness = config->brightness;
    }

    if (config->mask & MASK_COVER_HEATER_POWER)
    {
        if (config->heaterPower < 0 || config->heaterPower > 3)
        {
            return WC_ERROR_INVALID_PARAMETER;
        }

        // Send heater power command: 2000, 2050, 2100, 2150
        char cmd[8];
        switch (config->heaterPower)
        {
        case 0:
            snprintf(cmd, sizeof(cmd), "2000\n");
            break;
        case 1:
            snprintf(cmd, sizeof(cmd), "2050\n");
            break;
        case 2:
            snprintf(cmd, sizeof(cmd), "2100\n");
            break;
        case 3:
            snprintf(cmd, sizeof(cmd), "2150\n");
            break;
        default:
            return WC_ERROR_INVALID_PARAMETER;
        }

        if (!SendCommand(device, cmd))
        {
            return WC_ERROR_COMMUNICATION;
        }

        device->heaterPower = config->heaterPower;
    }

    if (config->mask & MASK_COVER_ASIAIR_CONTROL)
    {
        if (config->asiairControl < 0)
        {
            return WC_ERROR_INVALID_PARAMETER;
        }

        // Send asiair command: 1500003 / 1500004
        char cmd[16];
        if (config->asiairControl == 0)
        {
            snprintf(cmd, sizeof(cmd), "1500004\n");
        }
        else
        {
            snprintf(cmd, sizeof(cmd), "1500003\n");
        }

        if (!SendCommand(device, cmd))
        {
            return WC_ERROR_COMMUNICATION;
        }

        device->asiairControl = config->asiairControl;
    }

    return WC_SUCCESS;
}

WCAPI WC_ERROR_TYPE WCCoverGetStatus(int id, WC_COVER_STATUS *status)
{
    if (!status)
    {
        return WC_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return WC_ERROR_INVALID_ID;
    }

    auto device = it->second;

    {
        std::lock_guard<std::mutex> lock(device->movingStateMutex);

        /* Determine cover state based on current position */
        if (device->isMoving)
        {
            status->coverState = 3; /* MOVING */
        }
        else if (device->currentPositionAngle <= device->closePositionAngle + 1.0f)
        {
            status->coverState = 0; /* CLOSED */
        }
        else if (device->currentPositionAngle >= device->openPositionAngle - 1.0f)
        {
            status->coverState = 1; /* OPEN */
        }
        else
        {
            status->coverState = 2; /* INTERMEDIATE */
        }
    }

    status->currentPositionAngle = device->currentPositionAngle;
    status->closePositionAngle = device->closePositionAngle;
    status->openPositionAngle = device->openPositionAngle;

    return WC_SUCCESS;
}

WCAPI WC_ERROR_TYPE WCCoverGetVersion(int id, WC_VERSION *version)
{
    if (!version)
    {
        return WC_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return WC_ERROR_INVALID_ID;
    }

    auto device = it->second;
    version->firmware = device->firmwareVersion;

    // Model
    strncpy(version->model, device->modelType.c_str(), sizeof(version->model) - 1);
    version->model[sizeof(version->model) - 1] = '\0';

    return WC_SUCCESS;
}

WCAPI WC_ERROR_TYPE WCCoverOpenCover(int id)
{
    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return WC_ERROR_INVALID_ID;
    }

    auto device = it->second;

    if (!device->port || !device->port->IsOpen())
    {
        return WC_ERROR_COMMUNICATION;
    }

    {
        std::lock_guard<std::mutex> lock(device->movingStateMutex);

        /* Command to open cover: 1001 */
        if (!SendCommand(device, "1001"))
        {
            return WC_ERROR_COMMUNICATION;
        }

        device->isMoving = true;

        /* Give device time to start moving before listener can reset the flag */
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return WC_SUCCESS;
}

WCAPI WC_ERROR_TYPE WCCoverCloseCover(int id)
{
    std::lock_guard<std::mutex> lock(g_globalMutex);

    auto it = g_devices.find(id);
    if (it == g_devices.end())
    {
        return WC_ERROR_INVALID_ID;
    }

    auto device = it->second;

    if (!device->port || !device->port->IsOpen())
    {
        return WC_ERROR_COMMUNICATION;
    }

    {
        std::lock_guard<std::mutex> lock(device->movingStateMutex);

        /* Command to close cover: 1000 */
        if (!SendCommand(device, "1000"))
        {
            return WC_ERROR_COMMUNICATION;
        }

        device->isMoving = true;

        /* Give device time to start moving before listener can reset the flag */
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return WC_SUCCESS;
}
