# IOnode - OpenClaw Skill

Read sensors, control GPIO pins, relays, and PWM outputs on IOnode ESP32
hardware nodes - directly from OpenClaw via NATS.

## Requirements

- [NATS CLI](https://github.com/nats-io/natscli) (`nats` binary in PATH)
- NATS server accessible from both OpenClaw and your IOnode devices (default port 4222)
- One or more IOnode devices on the same network

## Installation

```bash
openclaw install ionode
```

Or manually copy the `skill/ionode/` folder to `~/.openclaw/workspace/skills/ionode/`.

## Configuration

Set `IONODE_NATS_URL` if your NATS server is not at `localhost:4222`:

```bash
export IONODE_NATS_URL="nats://192.168.1.100:4222"
```

## Quick Start

```bash
# Discover all IOnode devices on the network
scripts/ion.sh discover

# Read chip temperature
scripts/ion.sh read ionode-01 chip_temp

# Read a registered sensor
scripts/ion.sh read ionode-01 temperature

# Toggle a relay
scripts/ion.sh set ionode-01 fan 1

# Read a raw GPIO pin
scripts/ion.sh gpio ionode-01 4 get

# Set a GPIO pin high
scripts/ion.sh gpio ionode-01 4 set 1

# Read raw ADC
scripts/ion.sh adc ionode-01 2

# Set PWM output
scripts/ion.sh pwm ionode-01 3 128

# Query device capabilities
scripts/ion.sh caps ionode-01
```

See `SKILL.md` for the full subject reference and automation patterns.

## Links

- [IOnode](https://ionode.io) - Project homepage
- [IOnode GitHub](https://github.com/M64GitHub/IOnode) - Source code
- [WireClaw](https://github.com/M64GitHub/WireClaw) - Full AI agent for ESP32
