# WandererCover SDK

A C/C++ SDK for controlling Wanderer Cover devices via serial communication. Provides an easy-to-use API for device discovery, cover control, and configuration.

## Features

- **Device Discovery**: Automatic scanning and enumeration of connected Wanderer Cover devices
- **Cover Control**: Open and close cover with angle positioning
- **Configuration**: Set brightness, heater power, ASIAIR control, and position angles
- **Status Monitoring**: Query current cover position, input voltage, and configuration

## Requirements

- GCC/Clang with C++11 support
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

int main() {
    // Get SDK version
    char version[32];
    WCGetSDKVersion(version);
    printf("SDK Version: %s\n", version);
    
    // Scan for devices
    int count = 32;
    int ids[32];
    WCCoverScan(&count, ids);
    
    if (count <= 0) {
        printf("No devices found\n");
        return 1;
    }
    
    // Open first device
    int device_id = ids[0];
    if (WCCoverOpen(device_id) != WC_SUCCESS) {
        printf("Failed to open device\n");
        return 1;
    }
    
    // Get device version
    WC_VERSION version_info;
    WCCoverGetVersion(device_id, &version_info);
    printf("Model: %s, Firmware: %u\n", version_info.model, version_info.firmware);
    
    // Get device status
    WC_COVER_STATUS status;
    WCCoverGetStatus(device_id, &status);
    printf("Current Position: %.2f°\n", status.currentPositionAngle);
    printf("Input Voltage: %.2fV\n", status.inputVoltage);
    
    // Open cover
    WCCoverOpenCover(device_id);
    
    // Set brightness
    WC_COVER_CONFIG config;
    config.mask = MASK_COVER_BRIGHTNESS;
    config.brightness = 200;
    WCCoverSetConfig(device_id, &config);
    
    // Close device
    WCCoverClose(device_id);
    
    return 0;
}
```

## API Reference

### Device Management

#### `WCCoverScan(number, ids)`
Scan for available Wanderer Cover devices on the system.

**Parameters:**
- `int* number` - Pointer to count, returns number of devices found
- `int* ids` - Array to store device IDs

**Returns:** `WC_SUCCESS` on success, error code on failure

#### `WCCoverOpen(id)`
Open a connection to a Wanderer Cover device.

**Parameters:**
- `int id` - Device ID from WCCoverScan

**Returns:** `WC_SUCCESS` on success, error code on failure

#### `WCCoverClose(id)`
Close connection to a device and clean up resources.

**Parameters:**
- `int id` - Device ID

**Returns:** `WC_SUCCESS` on success, error code on failure

### Device Information

#### `WCGetSDKVersion(version)`
Get the SDK version string.

**Parameters:**
- `char* version` - Buffer to store version string (min 32 bytes)

**Returns:** `WC_SUCCESS` on success

#### `WCCoverGetVersion(id, version)`
Get device firmware version and model information.

**Parameters:**
- `int id` - Device ID
- `WC_VERSION* version` - Pointer to version structure

**Returns:** `WC_SUCCESS` on success, error code on failure

### Cover Control

#### `WCCoverOpenCover(id)`
Open the cover to the configured open position angle.

**Parameters:**
- `int id` - Device ID

**Returns:** `WC_SUCCESS` on success, error code on failure

#### `WCCoverCloseCover(id, angle)`
Close the cover to the specified angle.

**Parameters:**
- `int id` - Device ID
- `float angle` - Target angle in degrees (0-360)

**Returns:** `WC_SUCCESS` on success, error code on failure

### Status and Configuration

#### `WCCoverGetStatus(id, status)`
Get current cover status and position.

**Parameters:**
- `int id` - Device ID
- `WC_COVER_STATUS* status` - Pointer to status structure

**Returns:** `WC_SUCCESS` on success, error code on failure

**Status Structure:**
```c
typedef struct {
    float currentPositionAngle;  /* Current motor position angle */
    float inputVoltage;          /* Input voltage */
    float closePositionAngle;    /* Closed position angle */
    float openPositionAngle;     /* Open position angle */
} WC_COVER_STATUS;
```

#### `WCCoverGetConfig(id, config)`
Get current device configuration.

**Parameters:**
- `int id` - Device ID
- `WC_COVER_CONFIG* config` - Pointer to config structure

**Returns:** `WC_SUCCESS` on success, error code on failure

#### `WCCoverSetConfig(id, config)`
Set device configuration values.

**Parameters:**
- `int id` - Device ID
- `WC_COVER_CONFIG* config` - Pointer to config structure with mask set

**Returns:** `WC_SUCCESS` on success, error code on failure

**Config Structure:**
```c
typedef struct {
    unsigned int mask;           /* Bitmask indicating which fields to set */
    float openPositionAngle;     /* Open position angle in degrees */
    float closePositionAngle;    /* Close position angle in degrees */
    int brightness;              /* Brightness level (0-255) */
    int heaterPower;             /* Heater power level (0-3) */
    int asiairControl;           /* ASIAIR control setting (0-3) */
} WC_COVER_CONFIG;
```

**Configuration Masks:**
```c
#define MASK_COVER_BRIGHTNESS      0x01
#define MASK_COVER_HEATER_POWER    0x02
#define MASK_COVER_ASIAIR_CONTROL  0x04
#define MASK_COVER_OPEN_POSITION   0x08
#define MASK_COVER_CLOSE_POSITION  0x10
#define MASK_COVER_ALL             0x1F
```

## Error Codes

```c
typedef enum {
    WC_SUCCESS = 0,                 /* Success */
    WC_ERROR_INVALID_ID,            /* Device ID is invalid */
    WC_ERROR_INVALID_PARAMETER,     /* Invalid parameter */
    WC_ERROR_INVALID_STATE,         /* Device in invalid state */
    WC_ERROR_COMMUNICATION,         /* Communication error */
    WC_ERROR_NULL_POINTER,          /* Null pointer parameter */
} WC_ERROR_TYPE;
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

**Version:** 1.0.0  
**Last Updated:** December 2025
