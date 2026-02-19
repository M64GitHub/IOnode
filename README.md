# IOnode

**Flash any ESP32. It speaks NATS.**

IOnode is a lightweight firmware that turns any ESP32 into a NATS-addressable hardware node. Every GPIO pin, ADC channel, sensor, and actuator on the board becomes instantly reachable over the network via simple request/reply.

Flash it, name it, point it at a NATS server - and your hardware is on the network. Read sensors from a script, toggle a relay from Node-RED, or pair it with [OpenClaw](https://github.com/openclaw/openclaw) to orchestrate your entire node fleet with natural language. The intelligence lives wherever you want it. IOnode just makes the hardware available.

Built to be hacked — [add any sensor or hardware you want](#adding-a-new-sensor-type).

→ **[OpenClaw Integration](#openclaw-integration)** - control IOnode with natural language

```
Your laptop / server / Raspberry Pi
    |
    +-- NATS
          |-- ionode-01.hal.*      <-- temperature sensor, relay
          |-- ionode-02.hal.*      <-- ADC inputs, PWM outputs
          +-- ionode-03.hal.*      <-- UART bridge, GPIO
```

---

## Quick Start

### 1. Flash

```bash
git clone <this-repo> && cd IOnode
pio run -t upload          # builds + flashes (default: ESP32-C6)
pio run -t uploadfs        # uploads LittleFS (config template + devices.json)
```

### 2. Configure

On first boot with no config, IOnode starts a WiFi AP called **IOnode-Setup**. Connect to it, fill in the form at `192.168.4.1`:

| Field | Example |
|-------|---------|
| WiFi SSID | `MyNetwork` |
| WiFi Password | `hunter2` |
| Device Name | `ionode-01` |
| NATS Host | `192.168.1.100` |
| NATS Port | `4222` |
| Timezone | `CET-1CEST,M3.5.0,M10.5.0/3` |

Saves to `/config.json` on LittleFS, reboots, connects.

Or create `data/config.json` before flashing:

```json
{
  "wifi_ssid": "MyNetwork",
  "wifi_pass": "hunter2",
  "device_name": "ionode-01",
  "nats_host": "192.168.1.100",
  "nats_port": "4222",
  "timezone": "CET-1CEST,M3.5.0,M10.5.0/3"
}
```

Then `pio run -t uploadfs` to flash the filesystem.

### 3. First Commands

```bash
# Discover all IOnode/WireClaw nodes on the network
nats req _ion.discover "" --replies=0 --timeout=2s

# Read chip temperature
nats req ionode-01.hal.system.temperature ""

# Read a GPIO pin
nats req ionode-01.hal.gpio.8.get ""

# Set a GPIO pin high
nats req ionode-01.hal.gpio.8.set "1"

# Check free heap
nats req ionode-01.hal.system.heap ""
```

---

## Web UI

IOnode serves a configuration and control interface on port 80. Access it at `http://{device-name}.local/` or the device IP shown in the serial monitor on boot.

### Config tab

Network and system settings (WiFi, NATS, device name, timezone). Also contains a live `devices.json` editor - read-only by default, with an Edit button for power users who want to paste a full config in one shot. Saves directly to LittleFS and reloads devices immediately.

### Devices tab

All registered devices with kind-appropriate controls:

| Kind | Widget |
|------|--------|
| `ntc_10k`, `ldr`, `analog_in`, `digital_in` | Live value + unit, sparkline history |
| `internal_temp` | Live chip temperature, always present |
| `serial_text` | Last received UART line |
| `digital_out`, `relay` | ON / OFF toggle buttons |
| `pwm` | Slider 0–255 with live value display |

An **Add Device** form at the top lets you register new sensors and actuators without editing JSON. Fields adapt to the selected kind - relay shows an Inverted checkbox, serial_text shows a Baud Rate field instead of a pin number.

After any add or delete, the `devices.json` editor in the Config tab updates automatically.

### Pins tab

Direct hardware access without registering a device. Pick a pin number, a type (GPIO / ADC / PWM), and hit Read or Write. Useful for wiring verification and bring-up. PWM reads return the last written value (cached - PWM is write-only on the hardware side).

### Status tab

Version, device name, uptime, heap, WiFi SSID + signal strength, IP address, NATS connection state.

---

## NATS Subject Reference

All subjects are prefixed with the device name (e.g. `ionode-01`). Payloads are plain text or simple values. Responses come back via NATS request/reply.

### Core HAL (always available, zero config)

| Subject | Payload | Response | Notes |
|---------|---------|----------|-------|
| `{name}.hal.gpio.{pin}.get` | - | `0` or `1` | Sets pin to INPUT, reads |
| `{name}.hal.gpio.{pin}.set` | `0` or `1` | `ok` | Sets pin to OUTPUT, writes |
| `{name}.hal.adc.{pin}.read` | - | `0`-`4095` | 12-bit raw ADC |
| `{name}.hal.pwm.{pin}.set` | `0`-`255` | `ok` | 8-bit PWM output |
| `{name}.hal.dac.*` | - | error | Not available on C3/C6/S3 |
| `{name}.hal.uart.read` | - | last line | Requires a `serial_text` device |
| `{name}.hal.uart.write` | text | `ok` | Requires a `serial_text` device |
| `{name}.hal.system.temperature` | - | `38.1` | Chip temp in C |
| `{name}.hal.system.heap` | - | `156000` | Free heap bytes |
| `{name}.hal.system.uptime` | - | `3600` | Seconds since boot |
| `{name}.hal.device.list` | - | JSON array | All registered devices + values |

### Registered Devices (plugin sensors/actuators)

Any device registered in `devices.json` is automatically routed under `{name}.hal.{device_name}`:

| Subject | Payload | Response |
|---------|---------|----------|
| `{name}.hal.{dev}` | - | sensor value (`23.4`) or actuator state (`1`) |
| `{name}.hal.{dev}.get` | - | same as above |
| `{name}.hal.{dev}.set` | value | `ok` (actuators only) |
| `{name}.hal.{dev}.info` | - | JSON: name, kind, value, pin, unit |

### Discovery & Capabilities

| Subject | Response |
|---------|----------|
| `_ion.discover` | Capabilities JSON (all nodes respond) |
| `{name}.capabilities` | Capabilities JSON (single node) |

Capabilities response:

```json
{
  "device": "ionode-01",
  "firmware": "ionode",
  "version": "0.1.0",
  "chip": "ESP32-C6",
  "free_heap": 156000,
  "ip": "192.168.1.42",
  "hal": {
    "gpio": true, "adc": true, "pwm": true,
    "dac": false, "uart": true, "system_temp": true
  },
  "devices": [
    {"name": "chip_temp", "kind": "internal_temp", "value": 38.1, "unit": "C"},
    {"name": "clock_hour", "kind": "clock_hour", "value": 14.0, "unit": "h"}
  ]
}
```

---

## Registering Sensors & Actuators

Edit `data/devices.json` and upload with `pio run -t uploadfs`. Or let `devicesInit()` auto-register the built-ins (chip_temp, clock_hour, clock_minute, clock_hhmm) on first boot.

### devices.json format

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

Fields: `n`=name, `k`=kind, `p`=pin (255=virtual), `u`=unit, `i`=inverted, `ns`=nats_subject (for nats_value sensors), `bd`=baud (for serial_text).

### Supported device kinds

| Kind | Type | What it does |
|------|------|--------------|
| `digital_in` | sensor | `digitalRead(pin)` -> 0/1 |
| `analog_in` | sensor | `analogRead(pin)` -> 0-4095 |
| `ntc_10k` | sensor | 10K NTC thermistor via Steinhart-Hart, EMA-smoothed |
| `ldr` | sensor | Light-dependent resistor -> 0-100% |
| `internal_temp` | sensor | ESP32 on-die temperature sensor |
| `clock_hour` | sensor | Current hour (0-23) from NTP |
| `clock_minute` | sensor | Current minute (0-59) from NTP |
| `clock_hhmm` | sensor | HHMM format (e.g. 1430) |
| `nats_value` | sensor | Subscribes to a NATS subject, stores last value |
| `serial_text` | sensor | Reads lines from UART1, parses numeric value |
| `digital_out` | actuator | `digitalWrite(pin, val)` |
| `relay` | actuator | `digitalWrite` with optional inversion |
| `pwm` | actuator | `analogWrite(pin, 0-255)` |

Once registered, every device is automatically available via NATS:

```bash
nats req ionode-01.hal.room_temp ""       # -> 23.4
nats req ionode-01.hal.heater.set "1"     # -> ok
nats req ionode-01.hal.light ""           # -> 67.2
nats req ionode-01.hal.room_temp.info ""  # -> {"name":"room_temp","kind":"ntc_10k",...}
```

---

## Adding a New Sensor Type

IOnode is designed to be forked. Adding a sensor type is a two-step process.

### 1. Add the enum in `include/devices.h`

```c
enum DeviceKind {
    // ... existing kinds ...
    DEV_SENSOR_SERIAL_TEXT,
    DEV_SENSOR_MY_SENSOR,      // <-- add before the actuators
    /* Actuators */
    DEV_ACTUATOR_DIGITAL,
    // ...
};
```

### 2. Add the read case in `src/devices.cpp`

In `deviceReadSensor()`:

```c
case DEV_SENSOR_MY_SENSOR: {
    // Your hardware reading code here
    result = readMyHardware(dev->pin);
    record_history = true;
    break;
}
```

And add the string mapping in `deviceKindName()` and `kindFromString()`:

```c
// deviceKindName:
case DEV_SENSOR_MY_SENSOR: return "my_sensor";

// kindFromString:
if (strcmp(s, "my_sensor") == 0) return DEV_SENSOR_MY_SENSOR;
```

That's it. Your sensor is now:
- Persisted in `devices.json` as `{"k":"my_sensor"}`
- Readable via `nats req ionode-01.hal.my_sensor ""`
- Listed in `nats req ionode-01.hal.device.list ""`
- Included in discovery responses
- Background-polled for history every 5 minutes

No NATS code. No registration boilerplate. The HAL router handles it.

---

## Building & Flashing

Requires [PlatformIO](https://platformio.org/).

```bash
# Build (default target: ESP32-C6)
pio run

# Build for a specific chip
pio run -e esp32-c6
pio run -e esp32-c3
pio run -e esp32-s3
pio run -e esp32

# Flash firmware
pio run -t upload

# Flash filesystem (config.json + devices.json)
pio run -t uploadfs

# Serial monitor (115200 baud)
pio device monitor
```

### Chip Compatibility

| Chip | Board | Status | Notes |
|------|-------|--------|-------|
| ESP32-C6 | `esp32-c6-devkitc-1` | Default target | USB-CDC serial, RGB LED |
| ESP32-S3 | `esp32-s3-devkitc-1` | Supported | RGB LED, more RAM |
| ESP32-C3 | `esp32-c3-devkitm-1` | Supported | Smallest/cheapest RISC-V option |
| ESP32 | `esp32dev` | Supported | Classic, no on-die temp sensor |

All targets use a 2MB partition layout (works on 2MB and 4MB flash chips):

| Partition | Size | Purpose |
|-----------|------|---------|
| app0 | 1.69 MB | Firmware |
| spiffs | 256 KB | LittleFS (config + devices) |
| nvs | 20 KB | Non-volatile storage |

### Serial Commands

Once connected, type over serial at 115200 baud:

```
/status    - WiFi, heap, uptime, NATS state, device count
/devices   - List all devices with current values
/debug     - Toggle debug logging
/reboot    - Restart
/setup     - Launch config portal (AP mode)
/help      - List commands
```

---

## Integrations

IOnode speaks plain NATS request/reply. Any system that can send a NATS message can read a sensor or control an actuator. No SDK, no special driver, no cloud account.

### Scripts

The simplest integration - a shell script using the `nats` CLI:

```bash
# Read temperature and act on it
TEMP=$(nats req ionode-01.hal.temperature "" --server nats://192.168.1.100:4222)
if (( $(echo "$TEMP > 30" | bc -l) )); then
  nats pub ionode-01.hal.fan.set "1" --server nats://192.168.1.100:4222
fi
```

Run it from cron, a Raspberry Pi, a server - anywhere with the `nats` CLI installed.

### Node-RED

Use the [node-red-contrib-nats](https://flows.nodered.org/node/node-red-contrib-nats) node. Point a request node at `ionode-01.hal.temperature` and wire it to whatever logic you need. IOnode becomes just another input/output in your flow.

### Home Assistant

Any Home Assistant NATS integration can poll IOnode subjects as sensors or send commands to actuators. The flat subject structure (`{device}.hal.{name}`) maps cleanly to HA entity naming conventions.

### OpenClaw

[OpenClaw](https://github.com/openclaw/openclaw) is an AI agent that orchestrates NATS-connected hardware using natural language. See the [OpenClaw Integration](#openclaw-integration) chapter for full details.

### Anything else

If it speaks NATS, it works with IOnode. The protocol is two lines:

```bash
nats req {device}.hal.{sensor} ""        # read
nats req {device}.hal.{actuator}.set "1" # write
```

---

## OpenClaw Integration

[OpenClaw](https://github.com/openclaw/openclaw) is an AI agent that runs on your laptop or server and orchestrates NATS-connected devices using natural language. With the IOnode skill installed, OpenClaw discovers your nodes automatically and can read sensors, control actuators, and write persistent automation scripts - all from a chat interface.

```
You: "read gpio 1 from ionode-01 please"
OpenClaw: GPIO 1 on ionode-01 reads 0 (low). 158ms RTT. ⚡

You: "and gpio 0?"
OpenClaw: GPIO 0 reads 1 (high). 63ms RTT. Something's pulling that pin up!

You: "now set gpio 1 to high please!"
OpenClaw: GPIO 1 is now high. ⚡
```


> For *[WireClaw](https://wireclaw.io) users (AI reasoning loop on the chip, local rules engine, self-contained*)  
> Same `.hal.` protocol means OpenClaw talks to both interchangeably with the same commands.

### Install the IOnode Skill

```bash
openclaw install ionode
```

Or manually copy `skill/ionode/` to `~/.openclaw/workspace/skills/ionode/`.

Set your NATS server if not on localhost:

```bash
export IONODE_NATS_URL="nats://192.168.1.100:4222"
```

### What OpenClaw Can Do

**Discover your fleet:**
```
"What IOnode devices are on the network?"
```
OpenClaw runs `ion.sh discover`, parses capabilities, and summarizes every node - chip type, free heap, registered sensors, HAL features.

**Read sensors:**
```
"What's the temperature on ionode-01?"
"Check the light level and chip temp on all nodes"
```

**Control hardware:**
```
"Turn on the fan relay on ionode-02"
"Set the LED strip on ionode-01 to half brightness"
"Toggle GPIO 4 on ionode-01"
```

**Write automation scripts:**
```
"Every 30 seconds, check the temperature on ionode-01 and turn on the fan if it's above 28°C"
```
OpenClaw writes a shell script using `ion.sh`, runs it as a background job, and monitors it.

**Cross-domain automation:**
```
"If the GitHub CI build fails, set GPIO 4 on ionode-01 high"
"When the calendar shows a meeting starting, dim the LED strip on ionode-02"
```

### [WireClaw](https://wireclaw.io) + IOnode Together

[WireClaw](https://wireclaw.io) is the sibling project - a full AI agent running directly on an ESP32, with an on-device rules engine, Telegram integration, and LLM chat. Both projects share the same `.hal.` protocol, so OpenClaw addresses them identically for hardware access. A mixed fleet just works:

```
"Read the temperature from wireclaw-01 and ionode-01 and compare them"
"Turn off all relays named 'fan' across all devices on the network"
```

For WireClaw-specific features (on-device rules, Telegram, AI tools), use the [WireClaw skill](https://github.com/M64GitHub/WireClaw) alongside this one.

---

## Project Structure

```
IOnode/
+-- platformio.ini          Build config (pioarduino, 4 chip targets)
+-- partitions.csv          2MB flash layout
+-- include/
|   +-- version.h           IONODE_VERSION
|   +-- devices.h           Device registry structs & API
|   +-- nats_hal.h          HAL NATS handler
|   +-- setup_portal.h      Config portal
+-- src/
|   +-- main.cpp            Setup, loop, NATS, serial commands
|   +-- devices.cpp         Registry, sensor reading, persistence
|   +-- nats_hal.cpp        HAL request router (gpio/adc/pwm/uart/system)
|   +-- setup_portal.cpp    WiFi AP + captive portal + config form
+-- lib/nats/               nats_atoms - embedded NATS client library
+-- data/
    +-- config.json.example  Config template
    +-- devices.json         Device registry (empty on first flash)
```

---

*Part of the [WireClaw](https://github.com/M64GitHub/WireClaw) ecosystem.*
