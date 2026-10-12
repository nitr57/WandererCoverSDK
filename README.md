# WandererCover SDK

A C/C++ SDK for controlling Wanderer Cover devices via serial communication. Provides an easy-to-use API for device discovery, cover control, and configuration.

## Features

- **Device Discovery**: Automatic scanning and enumeration of connected Wanderer Cover devices
- **Cover Control**: Open and close cover with angle positioning
- **Configuration**: Set brightness, heater power, ASIAIR control, and position angles
- **Status Monitoring**: Query the cover position and state, and the configuration

## Requirements

- GCC/Clang with C++17 support
- CMake 3.22 or higher
- libudev library

### Install Dependencies (Ubuntu/Debian)

```bash
sudo apt-get install build-essential cmake libudev-dev
```

## Building from Source

### Clone and Build

```bash
cd WandererCoverSDK
mkdir build && cd build
cmake ..
make
```

### Run Tests

```bash
./test_wanderer_cover
```

## Usage

### Basic Example

```c
#include "WandererCoverSDK.h"
#include <stdio.h>
#include <unistd.h>

int main(void) {
    // Get SDK version
    char version[WC_VERSION_LEN];
    WCGetSDKVersion(version);
    printf("SDK Version: %s\n", version);

    // Scan for devices - ids must have room for WC_MAX_NUM entries
    int count = 0;
    int ids[WC_MAX_NUM];
    if (WCCoverScan(&count, ids) != WC_SUCCESS || count == 0) {
        printf("No devices found\n");
        return 1;
    }

    // Open first device
    int id = ids[0];
    if (WCCoverOpen(id) != WC_SUCCESS) {
        printf("Failed to open device\n");
        return 1;
    }

    // Get device version
    WC_VERSION version_info;
    WCCoverGetVersion(id, &version_info);
    printf("Model: %s, Firmware: %u\n", version_info.model, version_info.firmware);

    // Open the cover and wait until it has stopped moving
    WC_COVER_STATUS status;
    if (WCCoverOpenCover(id) == WC_SUCCESS) {
        do {
            usleep(500000);
            WCCoverGetStatus(id, &status);
        } while (status.coverState == WC_COVER_STATE_MOVING);
        printf("Cover at %.2f degrees (state %d)\n", status.currentPositionAngle, status.coverState);
    }

    // Set brightness
    WC_COVER_CONFIG config;
    WCCoverGetConfig(id, &config);      // sets config.mask to 0
    config.mask = MASK_COVER_BRIGHTNESS;
    config.brightness = 200;
    WCCoverSetConfig(id, &config);

    // Close device
    WCCoverClose(id);

    return 0;
}
```

## API Reference

Every function returns a `WC_ERROR_TYPE`: `WC_SUCCESS` (0) on success, otherwise
`WC_ERROR_INVALID_ID`, `WC_ERROR_INVALID_PARAMETER`, `WC_ERROR_INVALID_STATE`,
`WC_ERROR_COMMUNICATION` or `WC_ERROR_NULL_POINTER`.

- Calls that need an opened device return `WC_ERROR_INVALID_STATE` when it is not opened.
- If the USB connection of an opened device is lost, calls return
  `WC_ERROR_COMMUNICATION` until `WCCoverOpen` reconnects it.
- Numbers are exchanged with the device in a locale-independent way, so the SDK
  works in applications that use a decimal comma.

- While a device is open, the SDK holds an advisory lock (`flock`) on
  `/tmp/ch34x-<port>.lock` (e.g. `/tmp/ch34x-ttyUSB0.lock`). Scans that find a
  port busy skip it at once if this marker is held, and otherwise wait for it
  (up to 6 s), assuming another scan is briefly probing it. Other SDKs for
  CH34x-based devices can follow the same convention.

### Device Management

#### `WCCoverScan(int *number, int *ids)`
Scan for Wanderer Cover devices on CH340 USB serial ports. `ids` must hold
`WC_MAX_NUM` entries; `number` receives the number of ids written. Devices
already known keep their id across rescans; unplugged devices that are not
opened are dropped. Ports that are not a cover get their previous settings back.
Devices are registered under their `/dev/serial/by-path/...` link where there is
one, so `WCCoverOpen` reconnects after the cover is unplugged and plugged back
into the same socket, even if it comes back as a different `/dev/ttyUSBn`.

#### `WCCoverGetPort(int id, char *port)`
Get a port name for a scanned device that can be saved and passed to
`WCCoverScanPort` later. `port` must hold `WC_PORT_LEN` bytes. This is the
device's `/dev/serial/by-path/...` link, which stays the same across reconnects
and reboots as long as the cover is plugged into the same USB socket (or its
device node if there is no such link). The CH340 has no USB serial number, so
`/dev/serial/by-id/...` is the same for every CH340 device and cannot be used.

#### `WCCoverScanPort(const char *port, int *id)`
Probe only the given port instead of every CH340 port, so devices of other
programs are not touched. `id` receives the device id. A port that already
belongs to a registered device returns that device's id without probing it again.
The device keeps the port name it was found on, so with a by-path link
`WCCoverOpen` reconnects after a replug the same way as for `WCCoverScan`.
If nothing on the port is a CH340 or no cover answers there, it returns
`WC_ERROR_COMMUNICATION`; fall back to `WCCoverScan`:

```c
int id, count, ids[WC_MAX_NUM];
if (WCCoverScanPort(savedPort, &id) != WC_SUCCESS) {
    if (WCCoverScan(&count, ids) != WC_SUCCESS || count == 0)
        return -1;
    id = ids[0];
}
WCCoverGetPort(id, savedPort);   /* remember for next time */
```

#### `WCCoverOpen(int id)` / `WCCoverClose(int id)`
Open or close the connection to a scanned device.

### Device Information

#### `WCGetSDKVersion(char *version)`
SDK version string; `version` must hold `WC_VERSION_LEN` bytes.

#### `WCCoverGetVersion(int id, WC_VERSION *version)`
Firmware version and model name.

### Cover Control

#### `WCCoverOpenCover(int id)` / `WCCoverCloseCover(int id)`
Move the cover to the configured open or close position. Returns as soon as the
move has started; `coverState` is `WC_COVER_STATE_MOVING` until it has finished.

### Status and Configuration

#### `WCCoverGetStatus(int id, WC_COVER_STATUS *status)`

```c
typedef struct {
    int coverState;              /* WC_COVER_STATE_CLOSED, _OPEN, _INTERMEDIATE or _MOVING */
    float currentPositionAngle;  /* Current motor position angle */
    float closePositionAngle;    /* Closed position angle */
    float openPositionAngle;     /* Open position angle */
} WC_COVER_STATUS;
```

#### `WCCoverGetConfig(int id, WC_COVER_CONFIG *config)`
Current configuration. Sets `config->mask` to 0.

#### `WCCoverSetConfig(int id, const WC_COVER_CONFIG *config)`
Apply the fields selected by `config->mask`. All selected fields are validated
before anything is sent. Fails with `WC_ERROR_INVALID_STATE` while the cover is
moving.

```c
typedef struct {
    unsigned int mask;           /* MASK_COVER_* bits of the fields to set */
    float openPositionAngle;     /* Open position angle in degrees (0 - 359.9) */
    float closePositionAngle;    /* Close position angle in degrees (0 - below 300) */
    int brightness;              /* Light brightness (1-255, 0 - off) */
    int heaterPower;             /* Heater power level (0-3) */
    int asiairControl;           /* ASIAIR control: 0 - off, others - on */
} WC_COVER_CONFIG;
```

```c
#define MASK_COVER_BRIGHTNESS      0x01
#define MASK_COVER_HEATER_POWER    0x02
#define MASK_COVER_ASIAIR_CONTROL  0x04
#define MASK_COVER_OPEN_POSITION   0x08
#define MASK_COVER_CLOSE_POSITION  0x10
#define MASK_COVER_ALL             0x1F
```

## License

MIT License - See [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please ensure:
- Code follows existing style and conventions
- All changes are tested
- License header is included in new files
- Commit messages are clear and descriptive

## Support

For issues, questions, or suggestions, please open an issue on the project repository.

---

**Version:** 1.5.0
