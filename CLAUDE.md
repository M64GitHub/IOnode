# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

IOnode is an ESP32 firmware that turns any ESP32 into a NATS-addressable hardware node. Every GPIO pin, ADC channel, sensor, and actuator becomes reachable over the network via NATS request/reply. Written in C++ using the Arduino framework and built with PlatformIO.

Supported chips: ESP32-C6 (default), ESP32-S3, ESP32-C3, ESP32 (classic).

## Build Commands

```bash
pio run                    # build default target (esp32-c6)
pio run -e esp32-s3        # build for specific chip
pio run -e esp32-c3
pio run -e esp32
pio run -t upload          # build + flash firmware
pio run -t uploadfs        # upload LittleFS filesystem (config + devices.json)
pio device monitor         # serial monitor at 115200 baud
```

There are no unit tests or linting tools configured for this project.

## Architecture

### Firmware (C++, Arduino framework)

The firmware is a single-binary PlatformIO project. Key modules:

- **`src/main.cpp`** — Entry point. WiFi/NATS connection, config loading from LittleFS, main loop (sensor polling, heartbeat, serial commands, NATS reconnect). Global config variables (`cfg_wifi_ssid`, `cfg_device_name`, etc.) live here.
- **`src/devices.cpp` / `include/devices.h`** — Device registry. Manages up to 16 named sensors/actuators. Handles registration, persistence to `/devices.json` on LittleFS, sensor reading (with EMA smoothing and history), actuator control, and threshold event detection. The `DeviceKind` enum defines all supported sensor/actuator types.
- **`src/nats_hal.cpp` / `include/nats_hal.h`** — HAL router. Handles the `{name}.hal.>` wildcard NATS subscription and routes requests to GPIO, ADC, PWM, UART, I2C, system queries, and registered device operations.
- **`src/nats_config.cpp` / `include/nats_config.h`** — Remote configuration via `{name}.config.>` NATS subjects. Device add/remove, tag, heartbeat, event config, rename — all without reflash.
- **`src/i2c_devices.cpp` / `include/i2c_devices.h`** — I2C sensor drivers (BME280, BH1750, SHT31, ADS1115, generic).
- **`src/i2c_display.cpp`** — SSD1306/SH1106 OLED display driver with a template engine (token substitution from sensor values).
- **`src/dht_driver.cpp` / `include/dht_driver.h`** — DHT11/DHT22 single-wire temperature/humidity sensor driver.
- **`src/setup_portal.cpp`** — WiFi AP captive portal for initial device configuration.
- **`src/web_config.cpp`** — On-device HTTP web UI (port 80) for config, device management, GPIO access, and status.

### NATS Client Library (`lib/nats/`)

A custom lightweight NATS client library bundled in `lib/nats/`. Provides `nats_atoms.h` as the main API header. Subdirectories: `proto/` (protocol), `parse/` (message parsing), `json/` (JSON handling), `cpp/` (C++ wrapper), `transport/` (TCP transport).

### Adding a New Sensor Type

1. Add enum value in `include/devices.h` (`DeviceKind` enum, before the actuator entries)
2. Add read case in `deviceReadSensor()`, string mapping in `deviceKindName()`, and parse case in `kindFromString()` — all in `src/devices.cpp`

The HAL router, persistence, discovery, web UI, and polling all work automatically from the registry.

### CLI (`cli/ionode`)

A bash script that wraps `nats-cli` and `jq` for fleet management. Not part of the firmware build.

### Web Dashboard (`web/dashboard/`)

A single-file HTML dashboard connecting to NATS via WebSocket. No build system.

### Data Files (`data/`)

Uploaded to LittleFS via `pio run -t uploadfs`. Contains `devices.json` (device registry) and `config.json.example`.

## Key Conventions

- Device names are short strings (max 24 chars), kinds are snake_case strings mapped to `DeviceKind` enum values
- Pin 255 (`PIN_NONE`) is the sentinel for virtual sensors (no GPIO pin)
- Max 16 devices per node (`MAX_DEVICES`)
- NATS subjects follow the pattern `{device_name}.hal.{resource}` for hardware access, `{device_name}.config.{action}` for configuration
- Chip-specific code uses `CONFIG_IDF_TARGET_*` preprocessor guards
- Custom partition table in `partitions.csv` (2MB flash layout)
