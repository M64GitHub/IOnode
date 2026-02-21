# IOnode Setup Guide

How to set up the NATS server and tools for IOnode fleet management.

## Contents

- [1. NATS Server](#1-nats-server)
- [2. NATS CLI](#2-nats-cli)
- [3. IOnode CLI](#3-ionode-cli)
- [4. Fleet Dashboard (Web)](#4-fleet-dashboard-web)
- [5. Network Architecture](#5-network-architecture)
- [6. Firewall Ports](#6-firewall-ports)

---

## 1. NATS Server

IOnode requires a [NATS](https://nats.io) server on your network. All nodes connect to it, and all management tools (CLI, web dashboard, scripts) communicate through it.

### Install

**Linux / macOS / Windows:**
```bash
# Download from https://github.com/nats-io/nats-server/releases
# Or install via package manager:

# macOS
brew install nats-server

# Linux (apt)
curl -sf https://binaries.nats.dev/nats-io/nats-server/v2@latest | sh
sudo mv nats-server /usr/local/bin/

# Docker
docker run -p 4222:4222 nats:latest
```

### Start

For the firmware and CLI, **no configuration file is needed** - just start the server:

```bash
nats-server
```

That's it. The server listens on port 4222 by default. IOnode firmware, the `ionode` CLI, scripts, and any NATS client can connect immediately.

### WebSocket Configuration (Fleet Dashboard only)

> **A config file is only required if you want to use the [Fleet Dashboard](#4-fleet-dashboard-web).** Browsers can't speak raw TCP, so the NATS server must expose a WebSocket port.

Create `nats-server.conf`:

```
# nats-server.conf

# Client connections (IOnode firmware, CLI, scripts)
port: 4222

# WebSocket (required for Fleet Dashboard)
websocket {
  port: 8080
  no_tls: true
}
```

Start with the config:
```bash
nats-server -c nats-server.conf
```

Now you have two ports:
- **4222** - standard NATS (firmware, CLI, scripts)
- **8080** - WebSocket (browser dashboard)

### Docker

```yaml
# docker-compose.yml
services:
  nats:
    image: nats:latest
    ports:
      - "4222:4222"
      - "8080:8080"    # only needed for Fleet Dashboard
    volumes:
      - ./nats-server.conf:/nats-server.conf
    command: ["-c", "/nats-server.conf"]
```

Without the dashboard, you can skip the config and just run:

```bash
docker run -p 4222:4222 nats:latest
```

### Verify NATS is Running

```bash
# Check the server is listening
nats server check connection --server nats://localhost:4222

# Or simply:
nats sub test --server nats://localhost:4222 &
nats pub test "hello" --server nats://localhost:4222
# Should print: hello
```

---

## 2. NATS CLI

The `nats` CLI is required for the IOnode CLI tool and useful for debugging.

### Install

```bash
# Go install
go install github.com/nats-io/natscli/nats@latest

# Or download binary from:
# https://github.com/nats-io/natscli/releases
```

### Configure Default Server

Instead of passing `--server` every time:

```bash
nats context add ionode --server nats://192.168.1.100:4222 --select
```

Now `nats req`, `nats sub`, etc. use this server by default.

### Quick Test with IOnode

```bash
# Discover all nodes
nats req _ion.discover "" --replies=0 --timeout=3s

# Read a sensor
nats req ionode-01.hal.system.temperature ""

# Watch heartbeats
nats sub _ion.heartbeat
```

---

## 3. IOnode CLI

### Install

```bash
cd IOnode
sudo ln -sf "$(pwd)/cli/ionode" /usr/local/bin/ionode
```

### Dependencies

- `nats` CLI (see above)
- `jq` - `apt install jq` / `brew install jq`

### Configure NATS Server

Three ways (in priority order):

**Option A: Command flag**
```bash
ionode --server nats://192.168.1.100:4222 discover
```

**Option B: Environment variable**
```bash
export IONODE_NATS_URL="nats://192.168.1.100:4222"
ionode discover
```

**Option C: Config file** (recommended)
```bash
mkdir -p ~/.config/ionode
echo "NATS_URL=nats://192.168.1.100:4222" > ~/.config/ionode/config
ionode discover
```

### Verify

```bash
ionode --version    # should print: ionode 0.2.0
ionode discover     # should find your nodes
```

Full CLI reference: [CLI.md](CLI.md)

---

## 4. Fleet Dashboard (Web)

### Prerequisites

Your NATS server **must** have WebSocket enabled. This is the `websocket { port: 8080 }` block in your config (see [WebSocket Configuration](#websocket-configuration-fleet-dashboard-only) above).

### Open

Just open `web/dashboard/index.html` in a browser. Works from:
- `file://` - double-click the file
- Local server - `python3 -m http.server 8000` in the `web/dashboard/` directory
- Hosted - GitHub Pages, nginx, any static file host

### Connect

Enter your NATS WebSocket URL in the connection bar:

```
ws://192.168.1.100:8080
```

The format is:
- `ws://` for unencrypted (local network)
- `wss://` for TLS (if you configure TLS on NATS)

The URL is saved in your browser's localStorage for next time.

### Troubleshooting

**"Failed to connect"**

1. Is the NATS server running? Check: `nats server check connection`
2. Is WebSocket enabled? Look for `websocket { port: 8080 }` in your config
3. Is the port accessible? From the machine running the browser: `curl -v ws://192.168.1.100:8080`
4. Firewall? Ensure port 8080 (or your chosen WS port) is open
5. Docker? Ensure the port is exposed: `-p 8080:8080`

**"Connected but no nodes"**

1. Are any IOnode devices on the network? Check: `nats req _ion.discover "" --replies=0 --timeout=3s`
2. Are they connected to the same NATS server?
3. Wait for heartbeats - nodes publish every 60s by default. You can also check the serial monitor for NATS connection status.

**CORS issues**

NATS WebSocket does not have CORS restrictions by default. If you're running behind a reverse proxy, ensure it passes WebSocket upgrade headers.

---

## 5. Firewall Ports

| Port | Protocol | Purpose | Who connects | Required |
|------|----------|---------|-------------|----------|
| 4222 | TCP | NATS client | ESP32 nodes, CLI, scripts | Always |
| 8080 | TCP (WS) | NATS WebSocket | Browser dashboard | Only for Fleet Dashboard |

Both ports must be accessible from the devices/machines that need them. On a home network this is usually not an issue. On a VPS or cloud server, add firewall rules.
