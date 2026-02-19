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

IOnode is a lightweight firmware that turns any ESP32 into a NATS-addressable
hardware node. Every GPIO pin, ADC channel, sensor, and actuator becomes
reachable via simple request/reply.

You issue NATS requests. IOnode executes them directly on hardware and replies
instantly. Registered sensors and actuators are auto-routed under the `.hal.`
namespace.

## How to Talk to IOnode

All communication uses `nats req` (request/reply). Subjects follow a flat,
human-readable pattern:

```bash
nats req <device>.hal.<subject> "<payload>"

# Examples:
nats req ionode-01.hal.system.temperature ""   # -> "33.2"
nats req ionode-01.hal.gpio.4.get ""           # -> "0"
nats req ionode-01.hal.gpio.4.set "1"          # -> "ok"
nats req ionode-01.hal.temperature ""          # -> "24.5"  (registered sensor)
nats req ionode-01.hal.fan.set "1"             # -> "ok"    (registered actuator)
```

### Wrapper Script

A convenience wrapper is available at `scripts/ion.sh`:

```bash
scripts/ion.sh discover                        # find all IOnode devices
scripts/ion.sh caps <device>                   # query device capabilities
scripts/ion.sh read <device> <sensor>          # read a registered sensor
scripts/ion.sh set <device> <actuator> <value> # set a registered actuator
scripts/ion.sh gpio <device> <pin> get|set [value]
scripts/ion.sh adc <device> <pin>
scripts/ion.sh pwm <device> <pin> <value>
scripts/ion.sh sub <device>                    # subscribe to device events
```

## Discovery

```bash
# Find all IOnode (and WireClaw HAL-capable) devices on the network
scripts/ion.sh discover
# or: nats req "_ion.discover" "" --replies=0 --timeout=3s

# Query a specific device
scripts/ion.sh caps ionode-01
# or: nats req ionode-01.capabilities ""
```

Returns: device name, firmware version, chip, IP, free heap, HAL capabilities,
and all registered sensors/actuators with current values.

## Core HAL Subjects

These are always available on every IOnode - no registration required.

### GPIO
```bash
nats req <device>.hal.gpio.<pin>.get ""     # -> "0" or "1"
nats req <device>.hal.gpio.<pin>.set "1"    # -> "ok"
nats req <device>.hal.gpio.<pin>.set "0"    # -> "ok"
```

### ADC (Analog Read)
```bash
nats req <device>.hal.adc.<pin>.read ""     # -> "2048"  (raw 0-4095)
```

### PWM Output
```bash
nats req <device>.hal.pwm.<pin>.set "128"   # -> "ok"  (0-255)
nats req <device>.hal.pwm.<pin>.get ""      # -> "128" (last written value)
```

### UART / Serial Bridge
```bash
nats req <device>.hal.uart.read ""          # -> last received line
nats req <device>.hal.uart.write "GET_DATA" # -> "ok"
```
Requires a `serial_text` device registered on the node.

### System Info
```bash
nats req <device>.hal.system.temperature "" # -> "33.2"  (chip temp °C)
nats req <device>.hal.system.heap ""        # -> "156000" (free bytes)
nats req <device>.hal.system.uptime ""      # -> "3672"   (seconds)
```

### Device List
```bash
nats req <device>.hal.device.list ""        # -> JSON array of all registered devices
```

## Registered Sensors & Actuators

Any device registered in the node's `devices.json` is automatically routed
under `{device}.hal.{name}`:

```bash
# Read a sensor (returns bare float string)
nats req ionode-01.hal.temperature ""       # -> "24.5"
nats req ionode-01.hal.light ""             # -> "67.2"

# Read with full metadata
nats req ionode-01.hal.temperature.info ""  # -> {"name":"temperature","kind":"ntc_10k","value":24.5,"unit":"C","pin":4}

# Set an actuator
nats req ionode-01.hal.fan.set "1"          # -> "ok"
nats req ionode-01.hal.led_strip.set "128"  # -> "ok"  (PWM, 0-255)

# Read actuator state
nats req ionode-01.hal.fan.get ""           # -> "1"
```

### Supported Device Kinds

| Kind | Type | Notes |
|------|------|-------|
| `digital_in` | sensor | digitalRead → 0/1 |
| `analog_in` | sensor | analogRead → 0-4095 |
| `ntc_10k` | sensor | NTC thermistor → °C, EMA-smoothed |
| `ldr` | sensor | Light sensor → 0-100% |
| `internal_temp` | sensor | Chip temperature, always pre-registered |
| `serial_text` | sensor | Last UART line received |
| `digital_out` | actuator | digitalWrite |
| `relay` | actuator | digitalWrite with optional inversion |
| `pwm` | actuator | analogWrite 0-255 |

### Pre-registered Sensors (always available, no registration needed)
- `chip_temp` - Internal chip temperature in °C

## Reserved HAL Keywords

Users cannot name their devices any of these words (validated at registration):
`gpio`, `adc`, `pwm`, `dac`, `uart`, `system`, `device`, `config`

## Working with Fleets

IOnode nodes are designed to be deployed in quantity. Use discovery to find
all nodes, then address each by its device name.

```bash
# Discover all nodes
scripts/ion.sh discover

# Read temperature from multiple nodes
for node in ionode-01 ionode-02 ionode-03; do
  echo -n "$node: "
  scripts/ion.sh read $node temperature
done

# Turn on all relays named "fan"
for node in ionode-01 ionode-02 ionode-03; do
  scripts/ion.sh set $node fan 1
done
```

## Automation Patterns

IOnode has no on-device rule engine. Automation logic runs in OpenClaw -
as shell scripts, background jobs, or loops. This is intentional: the logic
lives where it can be updated, monitored, and extended without reflashing.

### Polling loop
```bash
# Check temperature every 30 seconds, act if above threshold
while true; do
  TEMP=$(scripts/ion.sh read ionode-01 temperature)
  if (( $(echo "$TEMP > 28" | bc -l) )); then
    scripts/ion.sh set ionode-01 fan 1
  else
    scripts/ion.sh set ionode-01 fan 0
  fi
  sleep 30
done
```

### Cross-node automation
```bash
# Read from one node, act on another
LIGHT=$(scripts/ion.sh read ionode-01 light)
if (( $(echo "$LIGHT < 20" | bc -l) )); then
  scripts/ion.sh gpio ionode-02 8 set 1
fi
```

### Cross-domain: digital trigger → physical action
```bash
# React to a GitHub CI failure → turn on a warning LED
nats sub "ci.build.status" | while read -r line; do
  if echo "$line" | grep -q '"status":"failed"'; then
    scripts/ion.sh gpio ionode-01 4 set 1
  else
    scripts/ion.sh gpio ionode-01 4 set 0
  fi
done
```

### Subscribe to events
```bash
# Monitor all events from a node
scripts/ion.sh sub ionode-01
# or: nats sub "ionode-01.events"
```

## WireClaw Interoperability

IOnode and WireClaw share the same `.hal.` protocol. OpenClaw can read sensors
from a WireClaw node using the same `ion.sh` script:

```bash
scripts/ion.sh read wireclaw-01 chip_temp    # works on WireClaw too
scripts/ion.sh gpio wireclaw-01 4 get        # same HAL, same commands
```

For WireClaw-specific features (AI tools, rules engine, Telegram), use the
WireClaw skill instead.

## Examples

**Read all sensors on a node:**
```bash
scripts/ion.sh caps ionode-01
# or: nats req ionode-01.hal.device.list ""
```

**Temperature-controlled fan:**
```bash
TEMP=$(scripts/ion.sh read ionode-01 room_temp)
[ $(echo "$TEMP > 25" | bc -l) -eq 1 ] && scripts/ion.sh set ionode-01 fan 1
```

**Blink a GPIO:**
```bash
for i in $(seq 5); do
  scripts/ion.sh gpio ionode-01 5 set 1; sleep 0.5
  scripts/ion.sh gpio ionode-01 5 set 0; sleep 0.5
done
```

**Dim an LED strip via PWM:**
```bash
for val in 0 32 64 128 192 255; do
  scripts/ion.sh pwm ionode-01 3 $val; sleep 0.2
done
```

**Read a UART-attached sensor:**
```bash
nats req ionode-01.hal.uart.write "READ" --server $IONODE_NATS_URL
sleep 0.1
nats req ionode-01.hal.uart.read "" --server $IONODE_NATS_URL
```

**Monitor chip temperature across a fleet:**
```bash
nats req "_ion.discover" "" --replies=0 --timeout=3s | \
  jq -r '.device' | while read node; do
    echo -n "$node chip_temp: "
    scripts/ion.sh read $node chip_temp
  done
```

## Notes

- NATS server must be accessible from both OpenClaw and the IOnode devices.
  Default port 4222. Set `IONODE_NATS_URL` env var if non-default.
- All responses are plain strings (bare floats, "ok", "0"/"1") or error JSON.
- IOnode has no rule engine. All automation logic runs here in OpenClaw.
- Always discover capabilities first if you don't know what sensors are registered.
- The web UI at `http://{device}.local/` is useful for manual control and
  adding/removing devices without reflashing.
