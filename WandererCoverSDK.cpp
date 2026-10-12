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
#include <algorithm>
#include <exception>
#include <cmath>
#include <cctype>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <dirent.h>
#include <libudev.h>

#define SDK_VERSION "1.5.0"

/* Import internal implementation for use in public C API */
using namespace WandererCover;

/* No C++ exception may cross the extern "C" API boundary. */
#define WC_API_CATCH                                                  \
    catch (const std::exception &e)                                   \
    {                                                                 \
        WC_ERROR("%s: unexpected exception: %s", __func__, e.what()); \
        return WC_ERROR_COMMUNICATION;                                \
    }                                                                 \
    catch (...)                                                       \
    {                                                                 \
        WC_ERROR("%s: unexpected exception", __func__);               \
        return WC_ERROR_COMMUNICATION;                                \
    }

/* Serializes WCCoverScan calls without blocking any other API call */
static std::mutex g_scanMutex;

static std::shared_ptr<Device> FindDevice(int id)
{
    std::lock_guard<std::mutex> lock(g_globalMutex);
    auto it = g_devices.find(id);
    return it == g_devices.end() ? nullptr : it->second;
}

/* How long Scan and Open wait for the first status broadcast. On the sibling
 * Wanderer devices the first output after power-up arrives up to ~1.6s after
 * the port is opened. */
static const int kHandshakeTimeoutMs = 2500;

/* True if the device was opened but its port has since been hung up, e.g.
 * because the USB cable was unplugged. Such a device must be reopened. */
static bool ConnectionLost(const std::shared_ptr<Device> &device)
{
    return device->isOpen && device->port && device->port->IsHungUp();
}

/* Arm the handshake before starting the status listener, so a broadcast the
 * listener parses right away is not missed */
static void ArmHandshake(const std::shared_ptr<Device> &device)
{
    std::lock_guard<std::mutex> lock(device->handshakeMutex);
    device->handshakePending = true;
}

/* Wait until the listener has parsed a status broadcast */
static bool WaitForHandshake(const std::shared_ptr<Device> &device, int timeoutMs)
{
    std::unique_lock<std::mutex> lock(device->handshakeMutex);
    bool ok = device->handshakeCV.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                           [&device]() { return !device->handshakePending; });
    if (!ok)
    {
        WC_DEBUG("WaitForHandshake: Timeout waiting for a status broadcast (timeout=%dms)", timeoutMs);
        device->handshakePending = false;
    }
    return ok;
}

/* The device node a port name currently points to. A device is registered
 * under its /dev/serial/by-path/... link where it has one, which after a
 * reconnect may point to a different node - compare ports only through this.
 * Returns the name unchanged if it does not exist (unplugged). */
static std::string ResolvePort(const std::string &port)
{
    char resolved[PATH_MAX];
    return realpath(port.c_str(), resolved) ? std::string(resolved) : port;
}

/* True if the tty belongs to a CH340 USB-serial chip (1a86:7523), the one
 * the cover uses */
static bool IsCh340(struct udev_device *tty)
{
    struct udev_device *parent = udev_device_get_parent_with_subsystem_devtype(
        tty, "usb", "usb_device");
    if (!parent)
    {
        return false;
    }

    const char *vid = udev_device_get_sysattr_value(parent, "idVendor");
    const char *pid = udev_device_get_sysattr_value(parent, "idProduct");
    return vid && pid && strcmp(vid, "1a86") == 0 && strcmp(pid, "7523") == 0;
}

/* The tty's /dev/serial/by-path/... link, which names the USB socket and so
 * survives reconnects, or "" if it has none (or none that fits WC_PORT_LEN).
 * udev lists the links sorted, so the choice is stable. Newer udev versions
 * add a ...-usbv2-... link next to the legacy one; either works. */
static std::string ByPathLink(struct udev_device *tty)
{
    struct udev_list_entry *entry;
    udev_list_entry_foreach(entry, udev_device_get_devlinks_list_entry(tty))
    {
        const char *link = udev_list_entry_get_name(entry);
        if (strncmp(link, "/dev/serial/by-path/", 20) == 0 && strlen(link) < WC_PORT_LEN)
        {
            return link;
        }
    }
    return "";
}

/* Registers a newly found device under the smallest free id, so it never
 * collides with (and silently replaces) an already registered one. Caller
 * holds g_globalMutex. */
static int RegisterDevice(const std::shared_ptr<Device> &device)
{
    int id = 0;
    while (g_devices.find(id) != g_devices.end())
        id++;

    g_devices[id] = device;
    return id;
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
try
{
    auto port = std::make_shared<SerialPort>();
    
    /* Allow up to ~6 seconds for competing SDK scans to release the port.
     * Each competing scan holds a port for up to ~3s (handshake timeout).
     * With multiple SDKs scanning simultaneously, worst case is several
     * sequential holds before we get a window. Workers run in parallel
     * so this does not multiply scan duration across ports. */
    port->SetRetryParams(120, 50);  /* 120 * 50ms = 6000ms total timeout, 50ms poll interval */
    
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
    ArmHandshake(tempDevice);
    StartStatusListener(tempDevice);

    // Perform handshake (just wait for a status broadcast)
    if (WaitForHandshake(tempDevice, kHandshakeTimeoutMs))
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

        /* Not a cover - possibly another program's device: stop listening and
         * leave its port settings as we found them */
        StopStatusListener(tempDevice);
        port->RestoreOriginalSettings();
        port->Close();
    }
}
catch (const std::exception &e)
{
    /* An exception escaping a std::thread would call std::terminate */
    WC_ERROR("ScanWorkerThread: unexpected exception on %s: %s", task.portName.c_str(), e.what());
}
catch (...)
{
    WC_ERROR("ScanWorkerThread: unexpected exception on %s", task.portName.c_str());
}

/* ============================================================================
 * PUBLIC SDK API IMPLEMENTATION
 * ============================================================================ */

WCAPI WC_ERROR_TYPE WCGetSDKVersion(char *version)
try
{
    if (!version)
    {
        return WC_ERROR_NULL_POINTER;
    }

    static_assert(sizeof(SDK_VERSION) <= WC_VERSION_LEN, "SDK_VERSION too long");
    memcpy(version, SDK_VERSION, sizeof(SDK_VERSION));
    return WC_SUCCESS;
}
WC_API_CATCH

WCAPI WC_ERROR_TYPE WCCoverScan(int *number, int *ids)
try
{
    if (!number || !ids)
    {
        return WC_ERROR_NULL_POINTER;
    }

    /* Only one scan at a time. g_globalMutex is not held while probing, so
     * calls on already-registered devices are never blocked by a scan. */
    std::lock_guard<std::mutex> scanLock(g_scanMutex);

    /* Ports that already belong to a registered device are not probed again:
     * an open one would just fail/hang, and re-registering it would give the
     * same device a second id. */
    std::map<std::string, int> connectedPorts;
    {
        std::lock_guard<std::mutex> lock(g_globalMutex);
        for (const auto &pair : g_devices)
        {
            if (!pair.second->portName.empty())
            {
                connectedPorts[ResolvePort(pair.second->portName)] = pair.first;
            }
        }
    }

    /* Create udev context */
    std::unique_ptr<struct udev, decltype(&udev_unref)> udev(udev_new(), &udev_unref);
    if (!udev)
    {
        return WC_ERROR_COMMUNICATION;
    }

    /* Create enumeration for tty devices */
    std::unique_ptr<struct udev_enumerate, decltype(&udev_enumerate_unref)> enumerate(
        udev_enumerate_new(udev.get()), &udev_enumerate_unref);
    if (!enumerate)
    {
        return WC_ERROR_COMMUNICATION;
    }

    /* Filter for tty subsystem */
    udev_enumerate_add_match_subsystem(enumerate.get(), "tty");
    udev_enumerate_scan_devices(enumerate.get());

    struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate.get());
    struct udev_list_entry *entry;

    /* Step 1: Collect all candidate CH340 devices. Ports are compared by
     * device node, but a device is registered under its by-path link if it
     * has one, so WCCoverOpen can reconnect after a replug. */
    std::vector<std::string> candidatePorts;
    std::map<std::string, std::string> portNames; /* device node -> name to register */
    udev_list_entry_foreach(entry, devices)
    {
        const char *path = udev_list_entry_get_name(entry);
        std::unique_ptr<struct udev_device, decltype(&udev_device_unref)> device(
            udev_device_new_from_syspath(udev.get(), path), &udev_device_unref);
        if (!device)
        {
            continue;
        }

        if (!IsCh340(device.get()))
        {
            continue;
        }

        /* Get the device node (e.g., /dev/ttyUSB0) */
        const char *deviceNode = udev_device_get_devnode(device.get());
        if (deviceNode)
        {
            std::string link = ByPathLink(device.get());
            WC_DEBUG("Found CH340 device: %s (%s)", deviceNode, link.empty() ? "no by-path link" : link.c_str());
            candidatePorts.push_back(std::string(deviceNode));
            portNames[deviceNode] = link.empty() ? std::string(deviceNode) : link;
        }
    }

    /* Step 2: Scan candidate devices in parallel, skipping ports that already
     * belong to a registered device. */
    std::vector<ScanWorkerTask> tasks;
    std::vector<std::thread> workerThreads;

    for (const auto &port : candidatePorts)
    {
        if (connectedPorts.find(port) != connectedPorts.end())
            continue;
        if ((int)tasks.size() >= WC_MAX_NUM)
            break;
        tasks.emplace_back(portNames[port].c_str());
    }

    /* Spawn worker threads for each candidate port */
    for (auto &task : tasks)
    {
        try
        {
            workerThreads.emplace_back(ScanWorkerThread, std::ref(task));
        }
        catch (...)
        {
            /* Destroying a joinable std::thread calls std::terminate - join the
             * workers already started before letting the error propagate. */
            for (auto &thread : workerThreads)
            {
                thread.join();
            }
            throw;
        }
    }

    /* Wait for all threads to complete */
    for (auto &thread : workerThreads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    /* Step 3: Forget registered devices whose port has disappeared (unless the
     * caller still has them open), report the remaining ones under their
     * existing id, and give newly found devices the smallest free id. */
    std::lock_guard<std::mutex> lock(g_globalMutex);

    for (auto it = g_devices.begin(); it != g_devices.end();)
    {
        bool present = std::find(candidatePorts.begin(), candidatePorts.end(),
                                 ResolvePort(it->second->portName)) != candidatePorts.end();
        if (!present && !it->second->isOpen)
        {
            WC_DEBUG("Scan: Device id=%d on %s is gone, removing", it->first, it->second->portName.c_str());
            it = g_devices.erase(it);
        }
        else
        {
            ++it;
        }
    }

    int count = 0;
    for (const auto &pair : g_devices)
    {
        if (count >= WC_MAX_NUM)
            break;
        ids[count] = pair.first;
        count++;
    }

    for (auto &task : tasks)
    {
        if (task.isValid && count < WC_MAX_NUM)
        {
            ids[count] = RegisterDevice(task.device);
            count++;
        }
    }

    *number = count;
    return WC_SUCCESS;
}
WC_API_CATCH

WCAPI WC_ERROR_TYPE WCCoverScanPort(const char *port, int *id)
try
{
    if (!port || !id)
    {
        return WC_ERROR_NULL_POINTER;
    }

    std::lock_guard<std::mutex> scanLock(g_scanMutex);

    /* Compare device nodes: the registered device may have been found on a
     * by-path link or by device node */
    char resolved[PATH_MAX];
    if (!realpath(port, resolved))
    {
        WC_DEBUG("WCCoverScanPort: %s does not exist", port);
        return WC_ERROR_COMMUNICATION;
    }

    {
        std::lock_guard<std::mutex> lock(g_globalMutex);
        for (const auto &pair : g_devices)
        {
            if (ResolvePort(pair.second->portName) == resolved)
            {
                *id = pair.first;
                return WC_SUCCESS;
            }
        }
    }

    /* A by-path link names a USB socket, not a device: if something else is
     * plugged in there now, leave it alone */
    std::unique_ptr<struct udev, decltype(&udev_unref)> udev(udev_new(), &udev_unref);
    if (!udev)
    {
        return WC_ERROR_COMMUNICATION;
    }

    const char *name = strrchr(resolved, '/');
    std::unique_ptr<struct udev_device, decltype(&udev_device_unref)> tty(
        udev_device_new_from_subsystem_sysname(udev.get(), "tty", name ? name + 1 : resolved),
        &udev_device_unref);
    if (!tty || !IsCh340(tty.get()))
    {
        WC_DEBUG("WCCoverScanPort: %s is not a CH340 port", resolved);
        return WC_ERROR_COMMUNICATION;
    }

    /* Keep the name as given: a by-path link then follows the cover to
     * whatever node it gets after a reconnect in the same socket, so
     * WCCoverOpen can reconnect without a rescan */
    ScanWorkerTask task(port);
    ScanWorkerThread(task);
    if (!task.isValid)
    {
        WC_DEBUG("WCCoverScanPort: No cover on %s", resolved);
        return WC_ERROR_COMMUNICATION;
    }

    std::lock_guard<std::mutex> lock(g_globalMutex);
    *id = RegisterDevice(task.device);

    WC_INFO("Found cover on %s (id=%d)", port, *id);
    return WC_SUCCESS;
}
WC_API_CATCH

WCAPI WC_ERROR_TYPE WCCoverGetPort(int id, char *port)
try
{
    if (!port)
    {
        return WC_ERROR_NULL_POINTER;
    }

    auto device = FindDevice(id);
    if (!device)
    {
        return WC_ERROR_INVALID_ID;
    }

    std::string result = device->portName;

    /* Found on a by-path link already: that is the name to keep. Otherwise
     * (registered by device node, e.g. WCCoverScanPort("/dev/ttyUSB0"))
     * look the link up. */
    std::string node = ResolvePort(device->portName);
    std::unique_ptr<struct udev, decltype(&udev_unref)> udev(udev_new(), &udev_unref);
    const char *name = strrchr(node.c_str(), '/');
    if (result.rfind("/dev/serial/by-path/", 0) != 0 && udev && name)
    {
        std::unique_ptr<struct udev_device, decltype(&udev_device_unref)> tty(
            udev_device_new_from_subsystem_sysname(udev.get(), "tty", name + 1), &udev_device_unref);
        std::string link = tty ? ByPathLink(tty.get()) : "";
        if (!link.empty())
        {
            result = link;
        }
    }

    if (result.size() >= WC_PORT_LEN)
    {
        return WC_ERROR_INVALID_STATE;
    }
    memcpy(port, result.c_str(), result.size() + 1);
    return WC_SUCCESS;
}
WC_API_CATCH

WCAPI WC_ERROR_TYPE WCCoverOpen(int id)
try
{
    WC_DEBUG("WCCoverOpen: Opening device id=%d", id);

    auto device = FindDevice(id);
    if (!device)
    {
        WC_ERROR("WCCoverOpen: Device id=%d not found", id);
        return WC_ERROR_INVALID_ID;
    }

    WC_DEBUG("WCCoverOpen: Found device, portName=%s", device->portName.c_str());

    /* Per-device lock only: opening can wait up to ~10s for a busy port and
     * must not block calls on other devices. */
    std::lock_guard<std::mutex> io(device->ioMutex);

    if (device->isOpen && !device->port->IsHungUp())
    {
        WC_DEBUG("WCCoverOpen: Device already open");
        return WC_SUCCESS;
    }

    if (device->isOpen)
    {
        /* The connection was lost (e.g. USB unplugged and plugged back in):
         * drop the dead port and open it again */
        WC_ERROR("WCCoverOpen: Connection to %s was lost, reopening", device->portName.c_str());
        StopStatusListener(device);
        device->port->Close();
        device->isOpen = false;
    }

    /* Create a new SerialPort instance if needed */
    if (!device->port)
    {
        WC_DEBUG("WCCoverOpen: Creating new SerialPort instance");
        device->port = std::make_shared<SerialPort>();
    }

    /* Always update retry params before open - port may have been created during
     * scan with scan-phase params (120*50ms=6s). Open needs a longer budget to
     * outlast the full scan+open overlap window across all SDKs.
     * 20 * 500ms = 10 seconds total wait, polling every 50ms. */
    device->port->SetRetryParams(20, 500);

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
    ArmHandshake(device);
    StartStatusListener(device);

    // Perform handshake
    if (!WaitForHandshake(device, kHandshakeTimeoutMs))
    {
        WC_ERROR("WCCoverOpen: Handshake failed");
        StopStatusListener(device);
        device->port->Close();
        return WC_ERROR_COMMUNICATION;
    }

    /* Lets other programs' scans skip this port at once instead of waiting
     * for it to become free */
    device->port->MarkInUse(device->portName.c_str());

    device->isOpen = true;

    WC_INFO("[OK] Device opened");
    return WC_SUCCESS;
}
WC_API_CATCH

WCAPI WC_ERROR_TYPE WCCoverClose(int id)
try
{
    auto device = FindDevice(id);
    if (!device)
    {
        return WC_ERROR_INVALID_ID;
    }

    {
        std::lock_guard<std::mutex> io(device->ioMutex);

        /* The listener reads in short slices, so this returns quickly */
        StopStatusListener(device);

        if (device->port)
        {
            device->port->Close();
        }

        device->isOpen = false;
    }

    WC_INFO("[OK] Device closed");
    return WC_SUCCESS;
}
WC_API_CATCH

WCAPI WC_ERROR_TYPE WCCoverGetConfig(int id, WC_COVER_CONFIG *config)
try
{
    if (!config)
    {
        return WC_ERROR_NULL_POINTER;
    }

    auto device = FindDevice(id);
    if (!device)
    {
        return WC_ERROR_INVALID_ID;
    }

    if (ConnectionLost(device))
    {
        return WC_ERROR_COMMUNICATION;
    }

    std::lock_guard<std::mutex> lock(device->stateMutex);
    config->mask = 0; /* set the bits of the fields to change before passing it to SetConfig */
    config->openPositionAngle = device->openPositionAngle;
    config->closePositionAngle = device->closePositionAngle;
    config->brightness = device->brightness;
    config->heaterPower = device->heaterPower;
    config->asiairControl = device->asiairControl;

    return WC_SUCCESS;
}
WC_API_CATCH

WCAPI WC_ERROR_TYPE WCCoverSetConfig(int id, const WC_COVER_CONFIG *config)
try
{
    if (!config)
    {
        return WC_ERROR_NULL_POINTER;
    }

    auto device = FindDevice(id);
    if (!device)
    {
        return WC_ERROR_INVALID_ID;
    }

    /* Validate every requested field before sending anything, so an invalid
     * field can't leave the device with only part of the config applied.
     * (Written as !(in range) so NaN is rejected too.) */
    if ((config->mask & MASK_COVER_OPEN_POSITION) &&
        !(config->openPositionAngle >= 0.0f && config->openPositionAngle <= 359.9f))
    {
        return WC_ERROR_INVALID_PARAMETER;
    }

    /* Close position is sent as 10000 + x*100: from 300 degrees on that would
     * be the open-position command (40000 + x*100). Checked on the rounded
     * value that is actually sent (299.995 would round to 300.00). */
    if ((config->mask & MASK_COVER_CLOSE_POSITION) &&
        !(config->closePositionAngle >= 0.0f && lroundf(config->closePositionAngle * 100.0f) <= 29999))
    {
        return WC_ERROR_INVALID_PARAMETER;
    }

    if ((config->mask & MASK_COVER_BRIGHTNESS) &&
        (config->brightness < 0 || config->brightness > 255))
    {
        return WC_ERROR_INVALID_PARAMETER;
    }

    if ((config->mask & MASK_COVER_HEATER_POWER) &&
        (config->heaterPower < 0 || config->heaterPower > 3))
    {
        return WC_ERROR_INVALID_PARAMETER;
    }

    if ((config->mask & MASK_COVER_ASIAIR_CONTROL) && config->asiairControl < 0)
    {
        return WC_ERROR_INVALID_PARAMETER;
    }

    std::lock_guard<std::mutex> io(device->ioMutex);

    if (!device->isOpen)
    {
        return WC_ERROR_INVALID_STATE;
    }

    if (ConnectionLost(device))
    {
        return WC_ERROR_COMMUNICATION;
    }

    {
        /* Don't send config commands into a running move */
        std::lock_guard<std::mutex> lock(device->stateMutex);
        if (device->isMoving)
        {
            return WC_ERROR_INVALID_STATE;
        }
    }

    if (config->mask & MASK_COVER_OPEN_POSITION)
    {
        // Send command: 40000 + x*100
        int angle = 40000 + (int)lroundf(config->openPositionAngle * 100.0f);
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "%d\n", angle);
        if (!SendCommand(device, cmd))
        {
            return WC_ERROR_COMMUNICATION;
        }
        std::lock_guard<std::mutex> lock(device->stateMutex);
        device->openPositionAngle = config->openPositionAngle;
    }

    if (config->mask & MASK_COVER_CLOSE_POSITION)
    {
        // Send command: 10000 + x*100
        int angle = 10000 + (int)lroundf(config->closePositionAngle * 100.0f);
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "%d\n", angle);
        if (!SendCommand(device, cmd))
        {
            return WC_ERROR_COMMUNICATION;
        }
        std::lock_guard<std::mutex> lock(device->stateMutex);
        device->closePositionAngle = config->closePositionAngle;
    }

    if (config->mask & MASK_COVER_BRIGHTNESS)
    {
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
        std::lock_guard<std::mutex> lock(device->stateMutex);
        device->brightness = config->brightness;
    }

    if (config->mask & MASK_COVER_HEATER_POWER)
    {
        // Send heater power command: 2000, 2050, 2100, 2150
        static const char *heaterCommands[] = {"2000\n", "2050\n", "2100\n", "2150\n"};
        if (!SendCommand(device, heaterCommands[config->heaterPower]))
        {
            return WC_ERROR_COMMUNICATION;
        }
        std::lock_guard<std::mutex> lock(device->stateMutex);
        device->heaterPower = config->heaterPower;
    }

    if (config->mask & MASK_COVER_ASIAIR_CONTROL)
    {
        // Send asiair command: 1500003 / 1500004
        if (!SendCommand(device, config->asiairControl == 0 ? "1500004\n" : "1500003\n"))
        {
            return WC_ERROR_COMMUNICATION;
        }
        std::lock_guard<std::mutex> lock(device->stateMutex);
        device->asiairControl = config->asiairControl != 0;
    }

    return WC_SUCCESS;
}
WC_API_CATCH

WCAPI WC_ERROR_TYPE WCCoverGetStatus(int id, WC_COVER_STATUS *status)
try
{
    if (!status)
    {
        return WC_ERROR_NULL_POINTER;
    }

    auto device = FindDevice(id);
    if (!device)
    {
        return WC_ERROR_INVALID_ID;
    }

    if (ConnectionLost(device))
    {
        return WC_ERROR_COMMUNICATION;
    }

    std::lock_guard<std::mutex> lock(device->stateMutex);

    /* Determine cover state based on current position. The open angle may be
     * below the close angle, so compare along the open direction. */
    float direction = device->openPositionAngle >= device->closePositionAngle ? 1.0f : -1.0f;
    if (device->isMoving)
    {
        status->coverState = 3; /* MOVING */
    }
    else if ((device->currentPositionAngle - device->closePositionAngle) * direction <= 1.0f)
    {
        status->coverState = 0; /* CLOSED */
    }
    else if ((device->openPositionAngle - device->currentPositionAngle) * direction <= 1.0f)
    {
        status->coverState = 1; /* OPEN */
    }
    else
    {
        status->coverState = 2; /* INTERMEDIATE */
    }

    status->currentPositionAngle = device->currentPositionAngle;
    status->closePositionAngle = device->closePositionAngle;
    status->openPositionAngle = device->openPositionAngle;

    return WC_SUCCESS;
}
WC_API_CATCH

WCAPI WC_ERROR_TYPE WCCoverGetVersion(int id, WC_VERSION *version)
try
{
    if (!version)
    {
        return WC_ERROR_NULL_POINTER;
    }

    auto device = FindDevice(id);
    if (!device)
    {
        return WC_ERROR_INVALID_ID;
    }

    if (ConnectionLost(device))
    {
        return WC_ERROR_COMMUNICATION;
    }

    std::lock_guard<std::mutex> lock(device->stateMutex);
    version->firmware = device->firmwareVersion;

    // Model
    strncpy(version->model, device->modelType.c_str(), sizeof(version->model) - 1);
    version->model[sizeof(version->model) - 1] = '\0';

    return WC_SUCCESS;
}
WC_API_CATCH

/* Send an open (1001) or close (1000) command and mark the cover as moving */
static WC_ERROR_TYPE MoveCover(int id, const char *command)
{
    auto device = FindDevice(id);
    if (!device)
    {
        return WC_ERROR_INVALID_ID;
    }

    std::lock_guard<std::mutex> io(device->ioMutex);

    if (!device->isOpen)
    {
        return WC_ERROR_INVALID_STATE;
    }

    if (ConnectionLost(device))
    {
        return WC_ERROR_COMMUNICATION;
    }

    if (!SendCommand(device, command))
    {
        return WC_ERROR_COMMUNICATION;
    }

    /* The listener ignores status messages arriving within a short grace
     * period from now - they may have been sent before the device started
     * moving. */
    std::lock_guard<std::mutex> lock(device->stateMutex);
    device->isMoving = true;
    device->movingSince = std::chrono::steady_clock::now();

    return WC_SUCCESS;
}

WCAPI WC_ERROR_TYPE WCCoverOpenCover(int id)
try
{
    /* Command to open cover: 1001 */
    return MoveCover(id, "1001");
}
WC_API_CATCH

WCAPI WC_ERROR_TYPE WCCoverCloseCover(int id)
try
{
    /* Command to close cover: 1000 */
    return MoveCover(id, "1000");
}
WC_API_CATCH
