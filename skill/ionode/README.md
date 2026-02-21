# IOnode — OpenClaw Skill

Control IOnode ESP32 hardware nodes from OpenClaw via NATS. Read sensors,
toggle relays, manage your fleet — from a chat interface or automation scripts.

## Requirements

- **[NATS CLI](https://github.com/nats-io/natscli)** — `nats` binary in PATH
- **NATS server** accessible from both OpenClaw and IOnode devices (default port 4222)
- One or more IOnode devices on the same network

### Recommended: `ionode` CLI

The `ionode` CLI provides 28 fleet management commands with formatted output.
Install it for the best experience:

```bash
git clone https://github.com/M64GitHub/IOnode
sudo ln -sf "$(pwd)/IOnode/cli/ionode" /usr/local/bin/ionode
```

The skill works with raw `nats req` commands as a fallback, but the CLI
is strongly recommended for discovery, fleet management, and readable output.

## Installation

```bash
openclaw install ionode
```

Or manually copy `skill/ionode/` to `~/.openclaw/workspace/skills/ionode/`.

## Configuration

Set `IONODE_NATS_URL` if your NATS server is not at `localhost:4222`:

```bash
export IONODE_NATS_URL="nats://192.168.1.100:4222"
```

## Quick Start

```bash
ionode discover                         # find all nodes
ionode read ionode-01 chip_temp         # read a sensor
ionode write ionode-01 fan 1            # toggle a relay
ionode gpio ionode-01 4 get             # raw GPIO read
ionode status ionode-01                 # system health
ionode tag ionode-01 greenhouse         # fleet tagging
ionode event set ionode-01 chip_temp --above 45  # threshold alert
ionode watch                            # live heartbeat + event stream
```

See `SKILL.md` for the full subject reference, fleet management, and automation patterns.

## Links

- 🌐 [ionode.io](https://ionode.io) — Website & docs
- 📖 [NATS API Reference](https://github.com/M64GitHub/IOnode/blob/main/docs/NATS-API.md)
- 📖 [CLI Reference](https://github.com/M64GitHub/IOnode/blob/main/docs/CLI.md)
- 🔌 [WireClaw](https://wireclaw.io) — AI agent on ESP32, same HAL protocol
