# IOnode

![Version](https://img.shields.io/badge/firmware-v0.3.0-ff8c00) ![License](https://img.shields.io/badge/license-MIT-blue) ![Platform](https://img.shields.io/badge/platform-ESP32-333)

**Flash any ESP32. It speaks NATS.**  |  [ionode.io](https://ionode.io) | [Flash from browser](https://ionode.io/flash.html)


IOnode turns any ESP32 into a NATS-addressable hardware node. Every GPIO pin, ADC channel, sensor, and actuator becomes reachable over the network via simple request/reply. No SDK, no cloud, no account.

Flash it, name it, point it at a NATS server - done. Read sensors from a script, toggle relays from the CLI, monitor your fleet from a web dashboard, or pair it with [OpenClaw](https://github.com/openclaw/openclaw) for natural language control. The intelligence lives wherever you want it. IOnode just makes the hardware available.

**Supported chips:** ESP32-C6 · ESP32-S3 · ESP32-C3 · ESP32

**Supports:** GPIO · ADC · PWM · Relays · NTC & LDR sensors · I2C (BME280, BH1750, SHT31, ADS1115) · SSD1306 OLED display · RGB LED · UART

```
Your laptop / server / Raspberry Pi
    |
    +-- NATS server
          |-- ionode-01.hal.*      <-- BME280 + OLED display, relay
          |-- ionode-02.hal.*      <-- NTC temp, LDR, PWM fan
          +-- ionode-03.hal.*      <-- ADS1115 ADC, GPIO, UART
```

## Contents

- [Quick Start](#quick-start)
- [CLI Tool](#cli-tool) - manage your fleet from the terminal
- [Fleet Dashboard](#fleet-dashboard) - live web UI for monitoring
- [Web UI (On-Device)](#web-ui-on-device) - per-node configuration
- [Fleet Management](#fleet-management) - tags, heartbeats, events, remote config
- [NATS Subject Reference](#nats-subject-reference) - the full protocol
- [Device Kinds](#device-kinds) - sensors &amp; actuators
- [Adding a New Sensor Type](#adding-a-new-sensor-type) - built to be hacked
- [Building &amp; Flashing](#building--flashing)
- [OpenClaw Integration](#openclaw-integration) - natural language control
- [Integrations](#integrations) - scripts, Node-RED, Home Assistant, anything

**Documentation:** [`docs/`](docs/) - [Setup Guide](docs/SETUP.md) · [CLI Reference](docs/CLI.md) · [GPIO & Actuators](docs/GPIO.md) · [Standard Sensors](docs/IOnode-Standard-Sensors.md) · [I2C Sensors](docs/I2C-Sensors.md) · [I2C Display](docs/I2C-Display.md) · [Release Notes](docs/RELEASE-NOTES.md)

---

## Quick Start

### What you need

- An **ESP32** board (C6, S3, C3, or classic) 
- A **NATS server** running on your network - [setup guide](docs/SETUP.md)
- [PlatformIO](https://platformio.org/) for building, or use the [browser-based flasher](https://ionode.io/flash.html)

### 1. Flash

```bash
git clone https://github.com/M64GitHub/IOnode && cd IOnode
pio run -t upload          # builds + flashes (default: ESP32-C6)
pio run -t uploadfs        # uploads LittleFS (config template + devices.json)
```

Or flash directly from your browser at **[ionode.io/flash.html](https://ionode.io/flash.html)** - no tools required.

### 2. Configure

On first boot, IOnode starts a WiFi AP called **IOnode-Setup**. Connect and fill in the form at `192.168.4.1`:

| Field | Example |
|-------|---------|
| WiFi SSID | `MyNetwork` |
| WiFi Password | `hunter2` |
| Device Name | `ionode-01` |
| NATS Host | `192.168.1.100` |
| NATS Port | `4222` |
| Timezone | `CET-1CEST,M3.5.0,M10.5.0/3` |

Or pre-create `data/config.json` before flashing - see [docs/SETUP.md](docs/SETUP.md) for the full config reference.

### 3. Talk to it

Once connected, your hardware is on the network:

```bash
# Using the CLI (recommended)
ionode discover                         # find all nodes
ionode read ionode-01 chip_temp         # read a sensor
ionode gpio ionode-01 8 set 1           # set a GPIO high
ionode status ionode-01                 # system health

# Or raw NATS
nats req ionode-01.hal.system.temperature ""
nats req ionode-01.hal.gpio.8.set "1"
```

That's it. Your ESP32 speaks NATS.

---

## CLI Tool

`ionode` is a fleet management CLI with colored, formatted output. Requires [nats-cli](https://github.com/nats-io/natscli) and [`jq`](https://jqlang.github.io/jq/).

```bash
sudo ln -sf "$(pwd)/cli/ionode" /usr/local/bin/ionode
```

### Discovery &amp; monitoring

```bash
ionode discover                  # find all nodes on the network
ionode ls                        # compact fleet table with RSSI, heap, chip
ionode info ionode-01            # deep dive - system health, HAL, devices
ionode status ionode-01          # quick health check
ionode watch                     # live heartbeat + event stream
```

### Hardware access

```bash
ionode read ionode-01 temp       # read a sensor
ionode write ionode-01 fan 1     # set an actuator
ionode gpio ionode-01 4 get      # raw GPIO
ionode adc ionode-01 2           # raw ADC with bar graph
```

### Fleet configuration

```bash
ionode tag ionode-01 greenhouse                            # fleet grouping
ionode device add ionode-01 temp ntc_10k 2 --unit C       # register a sensor
ionode event set ionode-01 temp --above 28 --cooldown 30  # threshold alert
ionode watch --tag greenhouse                              # monitor a group
```

Supports `--no-color`, `--json`, `--server`, and `NO_COLOR` env.

Full reference: [`docs/CLI.md`](docs/CLI.md)

---

## Fleet Dashboard

A single-file HTML dashboard that connects directly to NATS via WebSocket. No backend, no build system, no dependencies beyond a browser.

Open `web/dashboard/index.html`, enter your NATS WebSocket URL, and the dashboard discovers and displays your fleet automatically.

### Features

- **Fleet overview** - node cards with online/offline status, chip type, tag, heap, RSSI, signal bars
- **Live updates** - heartbeat subscriptions keep everything current in real-time
- **Node detail panel** - click a node to see all devices, read sensors, toggle actuators, set RGB colors
- **Actuator controls** - toggle switches for relays, sliders for PWM, color picker for RGB LEDs
- **Tag filtering** - filter the fleet view by tag
- **Event log** - live feed of threshold events and online notifications
- **Quick config** - set tags and heartbeat intervals from the detail panel

> **Prerequisite:** Your NATS server needs WebSocket enabled. Add to `nats-server.conf`:
>
> ```
> websocket {
>   port: 8080
>   no_tls: true
> }
> ```
>
> See [docs/SETUP.md](docs/SETUP.md) for the full NATS server setup.

---

## Web UI (On-Device)

Each IOnode serves a configuration and control interface on port 80. Access it at `http://{device-ip}/` from any browser. This is for single-node management - for fleet-wide operations, use the [CLI](#cli-tool) or [Fleet Dashboard](#fleet-dashboard).

| Tab | What it does |
|-----|-------------|
| **Config** | WiFi, NATS, device name, timezone, tag. Live `devices.json` editor. |
| **Devices** | All registered devices with kind-appropriate widgets (toggles, sliders, color pickers). Add/remove devices. |
| **Pins** | Direct GPIO/ADC/PWM access. No registration needed. Quick wiring checks. |
| **Status** | Version, uptime, heap, WiFi signal, NATS state, tag, heartbeat, events fired. |

Screenshots and walkthrough: [ionode.io/web-ui.html](https://ionode.io/web-ui.html)

---

## Fleet Management

IOnode supports fleet-level operations - tagging, group queries, health monitoring, threshold alerts, and full remote configuration - all via NATS. No reflash required.

Full fleet management guide with CLI examples, dashboard screenshots, and provisioning workflows: [ionode.io/fleet.html](https://ionode.io/fleet.html)

### Tags &amp; Group Discovery

Tag nodes for fleet grouping. Tagged nodes respond to group queries:

```bash
ionode tag ionode-01 greenhouse
ionode discover --tag greenhouse        # all greenhouse nodes respond

# Or raw NATS
nats req ionode-01.config.tag.set 'greenhouse'
nats req _ion.group.greenhouse ''
```

Tags update live - no reboot needed.

### Health Heartbeat

Nodes publish periodic health reports to `_ion.heartbeat` (default: every 60s, configurable 0–3600s):

```json
{
  "device": "ionode-01", "tag": "greenhouse", "version": "0.2.1",
  "uptime": 3600, "heap": 245000, "rssi": -52,
  "nats_reconnects": 0, "sensors": 4, "actuators": 2, "events_fired": 3
}
```

Monitor live: `ionode watch` or subscribe in the [Fleet Dashboard](#fleet-dashboard).

### Threshold Events

Sensors can fire NATS notifications when values cross a threshold. Edge-detected with configurable cooldown:

```bash
ionode event set ionode-01 chip_temp --above 45 --cooldown 30
ionode watch                            # see events as they fire

# Or raw NATS
nats req ionode-01.config.event.set '{"n":"chip_temp","t":45,"d":"above","cd":30}'
nats sub 'ionode-01.events.>'
```

Events persist across reboots. Configurable via CLI, NATS, web API, and the on-device web UI.

### Actuator State Persistence

Relay and digital output states survive reboots. State is saved to `devices.json` with a 5-second debounce to protect flash. PWM and RGB LED values are NOT persisted - resuming arbitrary analog values on boot could be unsafe.

### Remote Configuration

The full device registry, tags, heartbeat, and events can be managed remotely via `{name}.config.>` NATS subjects.

Complete protocol reference: [`docs/NATS-API.md`](docs/NATS-API.md)

---

## NATS Subject Reference

All subjects are prefixed with the device name (e.g. `ionode-01`). Payloads are plain text or simple values. Responses come back via NATS request/reply.

Full protocol specification with payload formats and error handling: [`docs/NATS-API.md`](docs/NATS-API.md)

### Core HAL (always available, zero config)

| Subject | Payload | Response | Notes |
|---------|---------|----------|-------|
| `{name}.hal.gpio.{pin}.get` | - | `0` or `1` | Sets pin to INPUT, reads |
| `{name}.hal.gpio.{pin}.set` | `0` or `1` | `ok` | Sets pin to OUTPUT, writes |
| `{name}.hal.adc.{pin}.read` | - | `0`-`4095` | 12-bit raw ADC |
| `{name}.hal.pwm.{pin}.set` | `0`-`255` | `ok` | 8-bit PWM output |
| `{name}.hal.pwm.{pin}.get` | - | `0`-`255` | Last written PWM value (cached) |
| `{name}.hal.uart.read` | - | last line | Requires a `serial_text` device |
| `{name}.hal.uart.write` | text | `ok` | Requires a `serial_text` device |
| `{name}.hal.i2c.scan` | - | `[60,118]` | Detected I2C addresses |
| `{name}.hal.i2c.{addr}.detect` | - | `true`/`false` | Addresses in decimal |
| `{name}.hal.i2c.{addr}.read` | `{"reg":N,"len":N}` | `[bytes]` | Read I2C register |
| `{name}.hal.i2c.{addr}.write` | `{"reg":N,"data":[...]}` | `ok` | Write I2C register |
| `{name}.hal.system.temperature` | - | `38.1` | Chip temp in °C |
| `{name}.hal.system.heap` | - | `156000` | Free heap bytes |
| `{name}.hal.system.uptime` | - | `3600` | Seconds since boot |
| `{name}.hal.system.rssi` | - | `-52` | WiFi signal strength |
| `{name}.hal.system.reset_reason` | - | `software` | Last reset reason |
| `{name}.hal.system.nats_reconnects` | - | `1` | NATS reconnect count |
| `{name}.hal.device.list` | - | JSON array | All registered devices + values |

### Registered Devices

| Subject | Payload | Response |
|---------|---------|----------|
| `{name}.hal.{dev}` | - | Sensor value or actuator state |
| `{name}.hal.{dev}.get` | - | Same as above |
| `{name}.hal.{dev}.set` | value | `ok` (actuators only) |
| `{name}.hal.{dev}.info` | - | JSON: name, kind, value, pin, unit |

### Discovery &amp; Fleet

| Subject | Payload | Response | Notes |
|---------|---------|----------|-------|
| `_ion.discover` | - | Capabilities JSON | All nodes respond |
| `{name}.capabilities` | - | Capabilities JSON | Single node |
| `_ion.group.{tag}` | - | Capabilities JSON | All nodes with matching tag |
| `_ion.heartbeat` | _(subscribe)_ | Health JSON | Periodic, default every 60s |
| `{name}.events.{sensor}` | _(subscribe)_ | Threshold event JSON | Edge-detected alerts |

### Remote Configuration

| Subject | Payload | Response |
|---------|---------|----------|
| `{name}.config.get` | - | Config JSON (password excluded) |
| `{name}.config.device.list` | - | JSON array of devices |
| `{name}.config.device.add` | `{"n":"x","k":"relay","p":5}` | `{"ok":true}` |
| `{name}.config.device.remove` | `{"n":"x"}` | `{"ok":true}` |
| `{name}.config.tag.set` | `greenhouse` | `{"ok":true}` |
| `{name}.config.tag.get` | - | `{"tag":"greenhouse"}` |
| `{name}.config.heartbeat.set` | `60` | `{"ok":true}` |
| `{name}.config.event.set` | `{"n":"x","t":28,"d":"above","cd":10}` | `{"ok":true}` |
| `{name}.config.event.clear` | `{"n":"x"}` | `{"ok":true}` |
| `{name}.config.event.list` | - | JSON array |
| `{name}.config.name.set` | `new-name` | `{"ok":true}` (reboots) |

---

## Device Kinds

### Sensors

| Kind | What it does |
|------|--------------|
| `digital_in` | `digitalRead(pin)` → 0 or 1 |
| `analog_in` | `analogRead(pin)` → 0–4095 (12-bit) |
| `ntc_10k` | 10K NTC thermistor, Steinhart-Hart, EMA-smoothed |
| `ldr` | Light-dependent resistor → 0–100% |
| `internal_temp` | ESP32 on-die temperature sensor |
| `clock_hour` | Current hour (0–23) from NTP |
| `clock_minute` | Current minute (0–59) from NTP |
| `clock_hhmm` | HHMM format (e.g. 1430 = 2:30 PM) |
| `nats_value` | Subscribes to a NATS subject, stores last value |
| `serial_text` | Reads lines from UART1, parses numeric value |
| `i2c_generic` | Raw I2C register read, configurable addr/reg/len/scale |
| `i2c_bme280` | BME280 temp/humidity/pressure, channel via pin 0/1/2 |
| `i2c_bh1750` | BH1750 ambient light (lux) |
| `i2c_sht31` | SHT31 temp/humidity, channel via pin 0/1 |
| `i2c_ads1115` | ADS1115 16-bit ADC, channel via pin 0-3 |

### Actuators

| Kind | What it does |
|------|--------------|
| `digital_out` | `digitalWrite(pin, val)` |
| `relay` | `digitalWrite` with optional inversion |
| `pwm` | `analogWrite(pin, 0-255)` |
| `rgb_led` | Built-in RGB LED, packed `0xRRGGBB` value |
| `ssd1306` | SSD1306 OLED text display, template-driven |

### Registering Devices

Register via the CLI, NATS, or the on-device web UI:

```bash
# CLI
ionode device add ionode-01 room_temp ntc_10k 2 --unit C
ionode device add ionode-01 fan relay 8 --inverted

# NATS
nats req ionode-01.config.device.add '{"n":"room_temp","k":"ntc_10k","p":2,"u":"C"}'
nats req ionode-01.config.device.add '{"n":"fan","k":"relay","p":8,"i":true}'
```

Or edit `data/devices.json` directly:

```json
[
  {"n":"room_temp", "k":"ntc_10k",    "p":2,  "u":"C",  "i":false},
  {"n":"light",     "k":"ldr",        "p":3,  "u":"%",  "i":false},
  {"n":"door",      "k":"digital_in", "p":7,  "u":"",   "i":false},
  {"n":"heater",    "k":"relay",      "p":8,  "u":"",   "i":true},
  {"n":"fan",       "k":"pwm",        "p":9,  "u":"",   "i":false},
  {"n":"co2",       "k":"serial_text","p":255,"u":"ppm", "i":false, "bd":9600}
]
```

Fields: `n`=name, `k`=kind, `p`=pin (255=virtual), `u`=unit, `i`=inverted, `ns`=nats_subject (for `nats_value`), `bd`=baud (for `serial_text`).

---

## Adding a New Sensor Type

IOnode is designed to be forked. Adding a sensor type is a two-step process - no NATS code, no registration boilerplate.

Detailed walkthrough: [ionode.io/learn-more.html](https://ionode.io/learn-more.html)

### 1. Add the enum in `include/devices.h`

```c
enum DeviceKind {
    // ... existing kinds ...
    DEV_SENSOR_MY_SENSOR,      // <-- add before the actuators
    /* Actuators */
    DEV_ACTUATOR_DIGITAL,
    // ...
};
```

### 2. Add the read case + string mapping in `src/devices.cpp`

```c
// In deviceReadSensor():
case DEV_SENSOR_MY_SENSOR: {
    result = readMyHardware(dev->pin);
    record_history = true;
    break;
}

// In deviceKindName():
case DEV_SENSOR_MY_SENSOR: return "my_sensor";

// In kindFromString():
if (strcmp(s, "my_sensor") == 0) return DEV_SENSOR_MY_SENSOR;
```

That's it. Your sensor is now:
- Persisted in `devices.json` as `{"k":"my_sensor"}`
- Readable via `ionode read ionode-01 my_sensor`
- Listed in discovery responses and `device.list`
- Background-polled for history every 5 minutes
- Controllable via the web UI

The HAL router handles everything else.

---

## Building &amp; Flashing

Requires [PlatformIO](https://platformio.org/). Or use the **[browser-based flasher](https://ionode.io/flash.html)** - no toolchain needed.

```bash
# Build (default: ESP32-C6)
pio run

# Build for a specific chip
pio run -e esp32-c6        # default target, USB-CDC, RGB LED
pio run -e esp32-s3        # more RAM, RGB LED
pio run -e esp32-c3        # smallest, cheapest, RISC-V
pio run -e esp32           # classic, no on-die temp sensor

# Flash firmware + filesystem
pio run -t upload
pio run -t uploadfs

# Serial monitor
pio device monitor         # 115200 baud
```

All targets use a 2MB partition layout (works on 2MB and 4MB flash):

| Partition | Size | Purpose |
|-----------|------|---------|
| app0 | 1.69 MB | Firmware |
| spiffs | 256 KB | LittleFS (config + devices) |
| nvs | 20 KB | Non-volatile storage |

### Serial Commands

Type over serial at 115200 baud:

```
/status    - WiFi, heap, uptime, NATS state, device count
/devices   - List all devices with current values
/debug     - Toggle debug logging
/reboot    - Restart
/setup     - Launch config portal (AP mode)
/help      - List commands
```

Full setup walkthrough: [docs/SETUP.md](docs/SETUP.md)

---

## OpenClaw Integration

[OpenClaw](https://github.com/openclaw/openclaw) is an AI agent that can orchestrates your NATS-connected devices using natural language. With the IOnode skill installed, OpenClaw discovers your nodes automatically and can read sensors, control actuators, and write custom automation - all from a chat interface (Telegram, Discord, etc.).

```
You: "read temperature across the fleet"
Echo: ionode-01: 24.5°C  ionode-02: 21.8°C  ionode-03: 27.1°C
      3 nodes, 63ms avg RTT ⚡

You: "if any node goes above 30°C, message me"
Echo: Done. Monitoring chip_temp on all 3 nodes.
      I'll ping you on Telegram if anything crosses 30°C. 🌡️

You: "set the RGB LED to red when the CI build fails"
Echo: I'll poll your GitHub Actions workflow every 60s.
      Red LED on failure, green on success. Running now. ⚡
```

### Install the Skill

Manually copy `skill/ionode/` to `~/.openclaw/workspace/skills/ionode/`.

Or ask OpenClaw to install it from this repo.

### What OpenClaw Can Do

- **Discover &amp; monitor** - find nodes, check health, watch heartbeats
- **Read sensors** - temperature, light, GPIO, ADC, anything registered
- **Control hardware** - relays, PWM, RGB LEDs, raw GPIO
- **Custom automation** - write scripts, monitor thresholds, cross-domain triggers (CI → LED, calendar → dimmer)
- **Fleet operations** - read from all nodes, compare values, group commands

Full integration guide with examples: [ionode.io/openclaw-integration.html](https://ionode.io/openclaw-integration.html)

### WireClaw + IOnode Together

[WireClaw](https://wireclaw.io) is the sibling project - a full AI reasoning loop running directly on an ESP32, with an on-device rules engine and Telegram integration. Both share the same `.hal.` protocol, so OpenClaw talks to them identically. A mixed fleet just works:

```
"Read the temperature from wireclaw-01 and ionode-01 and compare them"
```

---

## Integrations

IOnode speaks plain NATS request/reply. Any system that can send a NATS message can interact with it. No SDK, no driver, no cloud account.

| System | How |
|--------|-----|
| **Shell scripts** | `nats req ionode-01.hal.temperature ""` - runs from cron, CI, anywhere |
| **Node-RED** | [node-red-contrib-nats](https://flows.nodered.org/node/node-red-contrib-nats) - IOnode as input/output nodes |
| **Home Assistant** | Any NATS integration - flat subject structure maps to HA entity naming |
| **OpenClaw** | Natural language - see [OpenClaw Integration](#openclaw-integration) |
| **Your own code** | Any language with a [NATS client library](https://nats.io/download/) - Go, Python, Rust, Zig, JS, ... |

The protocol is two lines:

```bash
nats req {device}.hal.{sensor} ""        # read
nats req {device}.hal.{actuator}.set "1" # write
```

---

## Project Structure

```
IOnode/
├── platformio.ini             Build config (pioarduino, 4 chip targets)
├── partitions.csv             2MB flash layout
├── include/
│   ├── version.h              IONODE_VERSION
│   ├── devices.h              Device registry structs & API
│   ├── i2c_devices.h          I2C subsystem header (pins, cache, drivers, display)
│   ├── nats_hal.h             HAL NATS handler
│   ├── nats_config.h          Remote config NATS handler
│   ├── web_config.h           Web UI server
│   └── setup_portal.h         Config portal
├── src/
│   ├── main.cpp               Setup, loop, NATS, serial commands
│   ├── devices.cpp            Registry, sensor reading, persistence, events
│   ├── i2c_devices.cpp        I2C bus management + sensor drivers (BME280, BH1750, SHT31, ADS1115)
│   ├── i2c_display.cpp        SSD1306 OLED driver + template engine + 5x7 font
│   ├── nats_hal.cpp           HAL request router (gpio/adc/pwm/uart/i2c/system)
│   ├── nats_config.cpp        Remote config router (config.> subjects)
│   ├── web_config.cpp         Web UI server + REST API
│   └── setup_portal.cpp       WiFi AP + captive portal
├── lib/nats/                  nats_atoms - embedded NATS client library
├── data/                      LittleFS filesystem template
├── cli/                       Fleet management CLI
├── web/dashboard/             Fleet web dashboard (single HTML file)
├── docs/                      Full documentation
│   ├── SETUP.md               Setup guide - start here
│   ├── NATS-API.md            Protocol reference (source of truth)
│   ├── CLI.md                 CLI command reference
│   └── RELEASE-NOTES.md       Changelog
├── skill/ionode/              OpenClaw integration skill
└── README.md
```

---

## Links

- **Website:** [ionode.io](https://ionode.io)
- **Flash from browser:** [ionode.io/flash.html](https://ionode.io/flash.html)
- **Documentation:** [`docs/`](docs/) · [Setup](docs/SETUP.md) · [NATS API](docs/NATS-API.md) · [CLI](docs/CLI.md)
- **WireClaw:** [wireclaw.io](https://wireclaw.io) - AI agent on ESP32, same protocol
- **OpenClaw:** [openclaw](https://github.com/openclaw/openclaw) - Natural language hardware control

*Part of the [WireClaw](https://github.com/M64GitHub/WireClaw) ecosystem.*
