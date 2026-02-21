# IOnode Fleet Dashboard

A single-page web dashboard for monitoring and controlling your IOnode fleet. Connects directly to NATS via WebSocket - no backend, no build step, no dependencies.

## Quick Start

1. **Enable WebSocket on your NATS server** (see [docs/SETUP.md](../../docs/SETUP.md))
2. **Open `index.html`** in a browser (file://, local server, or hosted)
3. **Enter your NATS WebSocket URL** (e.g. `ws://192.168.1.100:8080`)
4. **Click Connect** - your fleet appears

## Features

- **Fleet overview** - node cards with chip type, heap, RSSI, uptime, online/offline status
- **Live heartbeats** - cards flash on heartbeat, status updates in real time
- **Node detail** - click a node to see sensors, control actuators, configure settings
- **Actuator controls** - toggle relays, PWM sliders, RGB color picker
- **Quick config** - set tags and heartbeat interval from the panel
- **Event log** - live feed of threshold events and online notifications
- **Tag filtering** - filter fleet view by group tag
- **Responsive** - works on mobile

## How It Works

The dashboard speaks the exact same NATS protocol as the CLI. No translation layer.

On connect:
1. Requests `_ion.discover` to populate the fleet
2. Subscribes to `_ion.heartbeat` for live updates
3. Subscribes to `*.events.>` and `*.events` for event log

On node detail:
- Queries `{name}.hal.device.list` for fresh sensor values
- Sends `{name}.hal.{dev}.set` for actuator controls
- Sends `{name}.config.*` for configuration changes

## Tech

- Single HTML file with inline CSS and JS
- Vendored [nats.ws](https://github.com/nats-io/nats.ws) ESM library (lib/)
- No build tools, no frameworks, no npm install
- Colors match [ionode.io](https://ionode.io) website palette exactly
- Saves last NATS URL to localStorage
