# IOnode CLI

Fleet management for NATS-addressable hardware nodes.

## Install

```bash
# Clone the repo (if you haven't already)
git clone https://github.com/M64GitHub/IOnode && cd IOnode

# Symlink to PATH
sudo ln -sf "$(pwd)/cli/ionode" /usr/local/bin/ionode
```

### Dependencies

- **[nats CLI](https://github.com/nats-io/natscli)** — required
- **[jq](https://jqlang.github.io/jq/)** — required for structured output

## Configuration

Priority (highest to lowest):

1. `--server` / `-s` flag
2. `IONODE_NATS_URL` environment variable
3. Config file: `~/.config/ionode/config`
4. Default: `nats://localhost:4222`

### Config file

```bash
mkdir -p ~/.config/ionode
echo "NATS_URL=nats://192.168.1.100:4222" > ~/.config/ionode/config
```

### Environment

```bash
export IONODE_NATS_URL="nats://192.168.1.100:4222"
```

## Quick Start

```bash
ionode discover              # find all nodes
ionode ls                    # compact fleet table
ionode read ionode-01 temp   # read a sensor
ionode write ionode-01 fan 1 # set an actuator
ionode watch                 # live heartbeat + event stream
```

## Output

- **Color:** True color (24-bit) matching the [ionode.io](https://ionode.io) website palette
- `--no-color` — disable colors
- `--json` — raw JSON output for scripting
- `NO_COLOR` env var — respected per [no-color.org](https://no-color.org)
- Non-TTY stdout — colors auto-disabled when piping

## Commands

Run `ionode --help` for the full command reference.
