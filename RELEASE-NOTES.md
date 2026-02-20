# IOnode v0.2.0 — Fleet Management

IOnode goes from single-node tool to fleet-manageable platform. Five new features, one principle: **IOnode stays dumb, the network stays smart.**

## Actuator State Persistence

Relay and digital output devices now survive reboots. When you set a relay ON, it stays ON after power loss or restart. State is saved to `devices.json` with a 5-second debounce to protect flash from rapid toggling. PWM and RGB are intentionally excluded — resuming a PWM mid-value on boot could be dangerous, and RGB is cosmetic.

## NATS Remote Configuration

New `{device_name}.config.>` wildcard subscription enables full remote management without touching the web UI or reflashing:

- `config.device.add` / `config.device.remove` / `config.device.list` — manage the device registry
- `config.tag.set` / `config.tag.get` — fleet grouping (see below)
- `config.heartbeat.set` — adjust heartbeat interval
- `config.event.set` / `config.event.clear` / `config.event.list` — threshold events (see below)
- `config.name.set` — rename the node (triggers reboot)
- `config.get` — dump current config (wifi password excluded)

Example:
```bash
nats req ionode-01.config.device.add '{"n":"fan","k":"relay","p":8,"i":true}'
nats req ionode-01.config.get ''
```

## Tags and Group Discovery

Nodes can be tagged for fleet grouping. Set a tag via web UI, NATS, or `config.json`:

```bash
nats req ionode-01.config.tag.set 'greenhouse'
```

Tagged nodes subscribe to `_ion.group.{tag}` and respond with their full capabilities, just like `_ion.discover`. This lets you query all nodes in a group with a single request:

```bash
nats req _ion.group.greenhouse ''
```

Tags appear in discovery responses and the Status tab. Tags can be changed at runtime without reboot — the group subscription is updated live.

## Health Heartbeat

Nodes publish periodic health reports to `_ion.heartbeat` (default: every 60 seconds, configurable 0-3600, 0 disables):

```json
{
  "device": "ionode-01",
  "tag": "greenhouse",
  "version": "0.2.0",
  "uptime": 3600,
  "heap": 245000,
  "rssi": -52,
  "nats_reconnects": 0,
  "sensors": 4,
  "actuators": 2,
  "events_fired": 3
}
```

New HAL system queries: `hal.system.rssi`, `hal.system.reset_reason`, `hal.system.nats_reconnects`.

The Status tab now shows heartbeat interval, NATS reconnect count, and events fired.

## Threshold Events

Sensors can fire NATS notifications when values cross a threshold. Edge-detected with configurable cooldown — fires once on crossing, re-arms only when the value returns to the safe side.

```bash
# Alert when chip_temp exceeds 45C, minimum 30s between alerts
nats req ionode-01.config.event.set '{"n":"chip_temp","t":45,"d":"above","cd":30}'

# Listen for events
nats sub 'ionode-01.events.>'
```

Event payload:
```json
{
  "event": "threshold",
  "device": "ionode-01",
  "sensor": "chip_temp",
  "value": 46.2,
  "threshold": 45.0,
  "direction": "above",
  "unit": "C"
}
```

Events persist across reboots (stored as flat keys in `devices.json`). Configurable via NATS, web API (`/api/devices/event`), and visible on sensor cards in the web UI.

## Other Changes

- Debounced flash writes — `devices.json` flushes at most every 5s, `config.json` every 2s
- NATS reconnect counter tracked across session
- `config.json.example` updated with `tag` and `heartbeat_interval` fields
- Web UI: tag field in Config tab, extended Status tab with fleet telemetry

## Compatibility

All 4 targets build and are tested: ESP32-C6, ESP32-S3, ESP32-C3, classic ESP32. No breaking changes to existing config or device files — new fields are optional and default safely.

## Full Fleet Workflow

```bash
# Tag and provision
nats req ionode-01.config.tag.set 'greenhouse'
nats req ionode-01.config.device.add '{"n":"temp","k":"ntc_10k","p":2,"u":"C"}'
nats req ionode-01.config.device.add '{"n":"fan","k":"relay","p":8,"i":true}'

# Set up event
nats req ionode-01.config.event.set '{"n":"temp","t":28.0,"d":"above","cd":10}'

# Monitor fleet
nats sub '_ion.heartbeat'
nats sub '*.events.>'

# Query group
nats req _ion.group.greenhouse ''
```
