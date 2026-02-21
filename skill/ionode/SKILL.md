---
name: ionode
description: >
  Control IOnode hardware nodes on your network. Use when the user wants to
  read sensors, control GPIO pins, relays, PWM outputs, or ADC inputs on ESP32
  microcontrollers. Also use when the user references "ionode", "ion node",
  "ESP32 sensor nodes", or wants to interact with physical hardware over NATS.
tools:
  - Bash
  - Read
metadata:
  openclaw:
    requires:
      binaries:
        - nats
    env:
      - IONODE_NATS_URL
---

# IOnode - NATS-Addressable Hardware Nodes for OpenClaw

IOnode turns any ESP32 into a NATS-addressable hardware node. Every GPIO pin,
ADC channel, sensor, and actuator becomes reachable via request/reply over NATS.

- **Website & docs:** https://ionode.io
- **GitHub:** https://github.com/M64GitHub/IOnode
- **Firmware version:** 0.2.0

## Prerequisites

### Required
- **`nats` CLI** - https://github.com/nats-io/natscli (must be in PATH)
- **NATS server** accessible from both OpenClaw and IOnode devices (default port 4222)
- One or more IOnode devices on the same network

### Recommended: `ionode` CLI

The `ionode` CLI provides 28 fleet management commands with formatted, colored
output. **Always check if it's installed before using raw NATS commands:**

```bash
command -v ionode >/dev/null 2>&1 && echo "ionode CLI available" || echo "ionode CLI not found"
```

**If `ionode` is NOT installed**, tell the user:

> The `ionode` CLI is not installed. It provides fleet management, colored output,
> and easier commands. To install it:
>
> ```bash
> # From the IOnode repo
> git clone https://github.com/M64GitHub/IOnode
> sudo ln -sf "$(pwd)/IOnode/cli/ionode" /usr/local/bin/ionode
> ```
>
> I can use raw `nats req` commands in the meantime - everything still works.

**If `ionode` IS installed**, prefer it over raw `nats req` for all operations.

### Environment

Set `IONODE_NATS_URL` if NATS is not at `localhost:4222`:

```bash
export IONODE_NATS_URL="nats://192.168.1.100:4222"
```

The `ionode` CLI and raw `nats` commands both respect this variable.

---

## Quick Reference: ionode CLI

```bash
# Discovery & monitoring
ionode discover                           # find all nodes on the network
ionode ls                                 # compact fleet table
ionode info <device>                      # deep dive - health, HAL, devices
ionode status <device>                    # quick health check
ionode watch                              # live heartbeat + event stream
ionode watch --tag <tag>                  # monitor a tagged group

# Read sensors
ionode read <device> <sensor>             # read a registered sensor
ionode read <device> chip_temp            # chip temperature (always available)

# Control actuators
ionode write <device> <actuator> <value>  # set an actuator
ionode set <device> <actuator> <value>    # alias for write

# Raw hardware access
ionode gpio <device> <pin> get            # read GPIO pin
ionode gpio <device> <pin> set <0|1>      # write GPIO pin
ionode adc <device> <pin>                 # read ADC (0-4095, with bar graph)
ionode pwm <device> <pin> set <0-255>     # set PWM output
ionode pwm <device> <pin> get             # read last PWM value

# Fleet configuration
ionode tag <device> <tag>                 # set fleet tag
ionode device add <dev> <name> <kind> <pin> [--unit X] [--inverted]
ionode device remove <device> <name>      # remove a device
ionode event set <dev> <sensor> --above|--below <threshold> [--cooldown <sec>]
ionode event clear <device> <sensor>      # remove threshold event
ionode event list <device>                # list configured events
```

Full CLI reference: https://github.com/M64GitHub/IOnode/blob/main/docs/CLI.md

---

## Quick Reference: Raw NATS (fallback)

Use these when the `ionode` CLI is not installed. All subjects use request/reply.

```bash
NATS_URL="${IONODE_NATS_URL:-nats://localhost:4222}"

# Discovery
nats req _ion.discover "" --replies=0 --timeout=3s --server "$NATS_URL"
nats req <device>.capabilities "" --server "$NATS_URL"

# Sensors
nats req <device>.hal.<sensor> "" --server "$NATS_URL"
nats req <device>.hal.system.temperature "" --server "$NATS_URL"

# Actuators
nats req <device>.hal.<actuator>.set "<value>" --server "$NATS_URL"

# GPIO / ADC / PWM
nats req <device>.hal.gpio.<pin>.get "" --server "$NATS_URL"
nats req <device>.hal.gpio.<pin>.set "1" --server "$NATS_URL"
nats req <device>.hal.adc.<pin>.read "" --server "$NATS_URL"
nats req <device>.hal.pwm.<pin>.set "128" --server "$NATS_URL"

# System
nats req <device>.hal.system.heap "" --server "$NATS_URL"
nats req <device>.hal.system.uptime "" --server "$NATS_URL"
nats req <device>.hal.device.list "" --server "$NATS_URL"
```

---

## Core HAL Subjects

These are always available on every IOnode - no registration required.

### GPIO
```bash
ionode gpio <device> <pin> get             # → 0 or 1
ionode gpio <device> <pin> set <0|1>       # → ok
```

### ADC (Analog Read)
```bash
ionode adc <device> <pin>                  # → 0-4095 (12-bit, with bar graph)
```

### PWM Output
```bash
ionode pwm <device> <pin> set <0-255>      # → ok
ionode pwm <device> <pin> get              # → last written value
```

### UART / Serial Bridge
```bash
nats req <device>.hal.uart.read ""         # → last received line
nats req <device>.hal.uart.write "GET_DATA" # → ok
```
Requires a `serial_text` device registered on the node.

### System Info
```bash
ionode status <device>    # shows all of the below in one formatted view

# Or individually:
nats req <device>.hal.system.temperature ""     # chip temp °C
nats req <device>.hal.system.heap ""            # free heap bytes
nats req <device>.hal.system.uptime ""          # seconds since boot
nats req <device>.hal.system.rssi ""            # WiFi signal dBm
nats req <device>.hal.system.reset_reason ""    # last reset reason
nats req <device>.hal.system.nats_reconnects "" # reconnect count
```

### Device List
```bash
ionode info <device>                        # formatted device list + system info
nats req <device>.hal.device.list ""        # raw JSON array
```

---

## Registered Sensors & Actuators

Devices registered on the node (via web UI, CLI, or NATS) are auto-routed
under `{device}.hal.{name}`:

```bash
ionode read ionode-01 room_temp             # → 24.5
ionode write ionode-01 fan 1                # → ok
ionode read ionode-01 light                 # → 67.2
```

### Supported Device Kinds

**Sensors:**

| Kind | Notes |
|------|-------|
| `digital_in` | digitalRead → 0/1 |
| `analog_in` | analogRead → 0–4095 |
| `ntc_10k` | NTC thermistor → °C, EMA-smoothed |
| `ldr` | Light sensor → 0–100% |
| `internal_temp` | Chip temperature, always pre-registered |
| `clock_hour` | Current hour (0–23) from NTP |
| `clock_minute` | Current minute (0–59) from NTP |
| `clock_hhmm` | HHMM format (e.g. 1430) |
| `nats_value` | Subscribes to a NATS subject, stores last value |
| `serial_text` | Reads lines from UART1, parses numeric value |

**Actuators:**

| Kind | Notes |
|------|-------|
| `digital_out` | digitalWrite |
| `relay` | digitalWrite with optional inversion |
| `pwm` | analogWrite 0–255 |
| `rgb_led` | Built-in RGB LED, packed 0xRRGGBB value |

### Pre-registered Devices (always available)
- `chip_temp` - internal chip temperature in °C
- `clock_hour` - current hour (0–23)
- `clock_minute` - current minute (0–59)
- `clock_hhmm` - time as HHMM (e.g. 1830)
- `rgb_led` - built-in RGB LED (boards with RGB LED only)

### Registering New Devices

```bash
# Via CLI (preferred)
ionode device add ionode-01 room_temp ntc_10k 2 --unit C
ionode device add ionode-01 fan relay 8 --inverted

# Via NATS
nats req ionode-01.config.device.add '{"n":"room_temp","k":"ntc_10k","p":2,"u":"C"}'
```

---

## Fleet Management

### Tags & Group Discovery

```bash
ionode tag ionode-01 greenhouse
ionode discover --tag greenhouse            # all greenhouse nodes

# Raw NATS
nats req ionode-01.config.tag.set 'greenhouse'
nats req _ion.group.greenhouse ''           # all tagged nodes respond
```

Tags update live - no reboot needed.

### Health Heartbeats

Nodes publish periodic health reports to `_ion.heartbeat` (default: every 60s):

```bash
ionode watch                                # live heartbeat + event stream
ionode watch --tag greenhouse               # filter by tag
```

Heartbeat JSON includes: device, tag, version, uptime, heap, rssi,
nats_reconnects, sensors, actuators, events_fired.

### Threshold Events

Sensors fire NATS notifications when values cross a threshold:

```bash
ionode event set ionode-01 chip_temp --above 45 --cooldown 30
ionode event list ionode-01
ionode watch                                # events appear in the stream

# Raw NATS
nats req ionode-01.config.event.set '{"n":"chip_temp","t":45,"d":"above","cd":30}'
nats sub 'ionode-01.events.>'
```

Edge-detected with configurable cooldown. Events persist across reboots.

### Remote Configuration

All configuration is available over NATS - add/remove devices, set tags,
configure events, rename nodes. No reflash needed.

Full protocol reference: https://github.com/M64GitHub/IOnode/blob/main/docs/NATS-API.md

### Fleet Dashboard (Web)

A single-file HTML dashboard connects to NATS via WebSocket for live fleet
monitoring. Located at `web/dashboard/index.html` in the IOnode repo.

Requires NATS WebSocket enabled:
```
websocket {
  port: 8080
  no_tls: true
}
```

---

## Automation Patterns

IOnode has no on-device rule engine. Automation logic runs here in OpenClaw -
as shell scripts, background jobs, or monitoring loops. This is intentional:
the logic lives where it can be updated and extended without reflashing.

### Polling loop
```bash
while true; do
  TEMP=$(ionode read ionode-01 room_temp)
  if (( $(echo "$TEMP > 28" | bc -l) )); then
    ionode write ionode-01 fan 1
  else
    ionode write ionode-01 fan 0
  fi
  sleep 30
done
```

### Cross-node automation
```bash
LIGHT=$(ionode read ionode-01 light)
if (( $(echo "$LIGHT < 20" | bc -l) )); then
  ionode gpio ionode-02 8 set 1
fi
```

### Cross-domain: CI status → physical LED
```bash
nats sub "ci.build.status" | while read -r line; do
  if echo "$line" | grep -q '"status":"failed"'; then
    ionode gpio ionode-01 4 set 1    # red warning LED
  else
    ionode gpio ionode-01 4 set 0
  fi
done
```

### Fleet-wide operations
```bash
# Read temperature from every node
ionode discover --json | jq -r '.[].device' | while read node; do
  echo -n "$node: "
  ionode read "$node" chip_temp
done

# Tag all nodes in a batch
for i in $(seq -w 1 10); do
  ionode tag "ionode-$i" greenhouse
done
```

### RGB LED control
```bash
# Red (0xFF0000 = 16711680)
ionode write ionode-01 rgb_led 16711680
# Green (0x00FF00 = 65280)
ionode write ionode-01 rgb_led 65280
# Off
ionode write ionode-01 rgb_led 0
```

---

## WireClaw Interoperability

IOnode and [WireClaw](https://wireclaw.io) share the same `.hal.` protocol.
OpenClaw talks to both identically for hardware access:

```bash
ionode read wireclaw-01 chip_temp            # works on WireClaw too
ionode gpio wireclaw-01 4 get                # same HAL, same commands
```

For WireClaw-specific features (on-device AI rules, Telegram, tool-calling
loop), use the WireClaw skill instead.

---

## Reserved HAL Keywords

Users cannot name their devices any of these words:
`gpio`, `adc`, `pwm`, `dac`, `uart`, `system`, `device`, `config`

## Notes

- Always discover nodes first (`ionode discover`) if you don't know what's on the network.
- All NATS responses are plain strings (bare floats, "ok", "0"/"1") or JSON.
- IOnode has no rule engine - all automation logic runs here in OpenClaw.
- The on-device web UI at `http://{device-ip}/` is useful for manual control
  and adding/removing devices without reflashing.
- NATS server must be accessible from both OpenClaw and the IOnode devices.
