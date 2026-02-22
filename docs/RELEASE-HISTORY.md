# IOnode v0.2.1 - Inverted Sensor Support

## Inverted Flag for NTC and LDR Sensors

The `inverted` flag, previously only available for relay devices, is now supported for `ntc_10k` and `ldr` sensors. This controls which side of the voltage divider the sensor is wired on (3.3V side vs GND side).

**Web UI:** The "Inverted" checkbox in the Add Device form now appears for `relay`, `ntc_10k`, and `ldr` device kinds. Previously it was only shown for relays.

**Backend:** The LDR sensor reading now respects the `inverted` flag. When inverted, the percentage is flipped (`100% - value`), accounting for LDRs wired on the opposite side of the divider. NTC already supported this in the backend via the resistance calculation formula — this release exposes it in the web UI.

**No changes needed for existing devices.** The `inverted` flag defaults to `false`, so all current NTC and LDR devices behave exactly as before.

---

## Files Changed

- `src/web_config.cpp` - Show "Inverted" checkbox for ntc_10k and ldr in Add Device form
- `src/devices.cpp` - LDR reading respects `dev->inverted` flag

---

## Compatibility

All 4 targets build: ESP32-C6, ESP32-S3, ESP32-C3, classic ESP32. No breaking changes.

---

# IOnode v0.2.0 - Fleet Management

IOnode goes from single-node tool to fleet-manageable platform. Six major additions, one principle: **IOnode stays dumb, the network stays smart.**

---

## CLI Fleet Management Tool

New `ionode` CLI for managing your entire fleet from the terminal. 28 commands across 5 categories: fleet discovery, hardware access, configuration, events, and live monitoring.

```bash
ionode discover              # find all nodes
ionode ls                    # compact fleet table
ionode info ionode-01        # deep dive on one node
ionode read ionode-01 temp   # read a sensor
ionode write ionode-01 fan 1 # set an actuator
ionode watch                 # live heartbeat + event stream
```

Features:
- True color (24-bit) output matching the [ionode.io](https://ionode.io) website palette
- WiFi signal bars, ADC mini-bars, status indicators, spinner animations
- XDG-compliant config file (`~/.config/ionode/config`)
- `--no-color`, `--json`, `--server` flags
- `NO_COLOR` env var and non-TTY auto-detection
- Clean error messages with usage hints for every command
- Device deduplication on multi-reply discovery

Requires the [`nats` CLI](https://github.com/nats-io/natscli) and [`jq`](https://jqlang.github.io/jq/).

Install: `sudo ln -sf $(pwd)/cli/ionode /usr/local/bin/ionode`

Full reference: [docs/CLI.md](CLI.md)

---

## Actuator State Persistence

Relay and digital output devices now survive reboots. When you set a relay ON, it stays ON after power loss or restart. State is saved as the `"v"` field in `devices.json` with a 5-second debounce to protect flash from rapid toggling. PWM and RGB are intentionally excluded - resuming a PWM mid-value on boot could be dangerous, and RGB is cosmetic.

---

## NATS Remote Configuration

New `{device_name}.config.>` wildcard subscription enables full remote management without touching the web UI or reflashing:

- `config.device.add` / `config.device.remove` / `config.device.list` - manage the device registry
- `config.tag.set` / `config.tag.get` - fleet grouping (see below)
- `config.heartbeat.set` - adjust heartbeat interval
- `config.event.set` / `config.event.clear` / `config.event.list` - threshold events (see below)
- `config.name.set` - rename the node (triggers reboot)
- `config.get` - dump current config (WiFi password excluded)

Example - provision 10 nodes from one terminal:

```bash
for i in $(seq 1 10); do
  node="ionode-$(printf '%02d' $i)"
  ionode tag "$node" greenhouse
  ionode device add "$node" temp ntc_10k 2 --unit C
  ionode device add "$node" fan relay 8 --inverted
  ionode event set "$node" temp --above 28 --cooldown 30
done
```

Full protocol: [docs/NATS-API.md](NATS-API.md)

---

## Tags and Group Discovery

Nodes can be tagged for fleet grouping. Set a tag via CLI, web UI, NATS, or `config.json`:

```bash
ionode tag ionode-01 greenhouse
ionode ls --tag greenhouse
```

Tagged nodes subscribe to `_ion.group.{tag}` and respond with their full capabilities, just like `_ion.discover`. Tags can be changed at runtime without reboot - the group subscription is updated live.

---

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

RSSI is also included in the capabilities/discovery response for fast fleet-level signal strength visibility.

Monitor with the CLI:

```bash
ionode watch --heartbeats
```

---

## Threshold Events

Sensors can fire NATS notifications when values cross a threshold. Edge-detected with configurable cooldown - fires once on crossing, re-arms only when the value returns to the safe side.

```bash
ionode event set ionode-01 chip_temp --above 45 --cooldown 30
ionode watch --events
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

Events persist across reboots (stored as flat keys in `devices.json`). Configurable via NATS, CLI, web API (`/api/devices/event`), and visible on sensor cards in the web UI.

---

## Other Changes

- **RSSI in capabilities** - `WiFi.RSSI()` added to discovery/capabilities response
- **Debounced flash writes** - `devices.json` flushes at most every 5s, `config.json` every 2s
- **NATS reconnect counter** tracked across session
- **`config.json.example`** updated with `tag` and `heartbeat_interval` fields
- **Web UI** - tag field in Config tab, extended Status tab with fleet telemetry
- **Documentation** - restructured into `docs/` with NATS-API contract, CLI reference, and this changelog

---

## Compatibility

All 4 targets build and are tested: ESP32-C6, ESP32-S3, ESP32-C3, classic ESP32. No breaking changes to existing config or device files - new fields are optional and default safely.
