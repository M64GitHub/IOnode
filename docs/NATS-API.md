# IOnode NATS API Contract

Version: 0.2.0

This document is the single source of truth for every operation IOnode supports over NATS. The CLI, web UI, and any future tools implement this contract — nothing more, nothing less.

Every operation is defined as: what NATS subject to call, what to send, what comes back, and what it means.

---

## Conventions

- `{name}` = device name configured on the node (e.g. `ionode-01`)
- `{dev}` = registered device/sensor/actuator name (e.g. `temp`, `fan`)
- `{pin}` = GPIO pin number
- `{tag}` = fleet group tag (e.g. `greenhouse`)
- All requests use NATS request/reply unless marked *(subscribe)*
- All payloads are plain text or JSON as noted
- Empty payload is shown as `""`
- Timeouts: single-node requests 2s, discovery/group requests 2s with `--replies=0`

---

## 1. Discovery & Inventory

Operations for finding nodes and understanding what they can do.

### Discover All Nodes

Find every IOnode/WireClaw node on the network.

| | |
|---|---|
| **Subject** | `_ion.discover` |
| **Payload** | `""` |
| **Response** | Capabilities JSON (one per node) |
| **Expects** | Multiple replies (use `--replies=0 --timeout=2s`) |
| **CLI** | `ionode discover` / `ionode ls` |
| **Web** | Fleet tab auto-populates on connect |

### Query Single Node

Get capabilities of a specific node.

| | |
|---|---|
| **Subject** | `{name}.capabilities` |
| **Payload** | `""` |
| **Response** | Capabilities JSON |
| **CLI** | `ionode info {name}` |
| **Web** | Node detail panel |

### Query Group

Find all nodes with a specific tag.

| | |
|---|---|
| **Subject** | `_ion.group.{tag}` |
| **Payload** | `""` |
| **Response** | Capabilities JSON (one per matching node) |
| **Expects** | Multiple replies |
| **CLI** | `ionode ls --tag {tag}` / `ionode group {tag}` |
| **Web** | Fleet tab filter by tag |

### Capabilities Response Format

Every discovery/capabilities/group response returns this structure:

```json
{
  "device": "ionode-01",
  "firmware": "ionode",
  "version": "0.2.0",
  "chip": "ESP32-C6",
  "free_heap": 156000,
  "ip": "192.168.1.42",
  "tag": "greenhouse",
  "hal": {
    "gpio": true,
    "adc": true,
    "pwm": true,
    "dac": false,
    "uart": true,
    "system_temp": true
  },
  "devices": [
    {"name": "chip_temp", "kind": "internal_temp", "value": 38.1, "unit": "C"},
    {"name": "clock_hour", "kind": "clock_hour", "value": 14.0, "unit": "h"},
    {"name": "temp", "kind": "ntc_10k", "value": 23.4, "unit": "C"},
    {"name": "fan", "kind": "relay", "value": 1.0, "unit": ""}
  ]
}
```

Fields:
- `device` — node name
- `firmware` — `"ionode"` or `"wireclaw"` (both speak the same protocol)
- `version` — firmware version string
- `chip` — ESP32 variant (`ESP32`, `ESP32-C3`, `ESP32-C6`, `ESP32-S3`)
- `free_heap` — free heap memory in bytes
- `ip` — current IP address
- `tag` — fleet group tag (empty string if untagged)
- `hal` — available hardware abstraction features
- `devices` — registered sensors and actuators with current values

---

## 2. Hardware Access (HAL)

Direct hardware read/write operations. These work on any node, even with zero devices registered.

### GPIO

| Operation | Subject | Payload | Response | Notes |
|-----------|---------|---------|----------|-------|
| Read pin | `{name}.hal.gpio.{pin}.get` | `""` | `0` or `1` | Sets pin to INPUT |
| Write pin | `{name}.hal.gpio.{pin}.set` | `0` or `1` | `ok` | Sets pin to OUTPUT |

**CLI:** `ionode gpio {name} {pin} get` / `ionode gpio {name} {pin} set {value}`

### ADC

| Operation | Subject | Payload | Response | Notes |
|-----------|---------|---------|----------|-------|
| Read ADC | `{name}.hal.adc.{pin}.read` | `""` | `0`–`4095` | 12-bit raw value |

**CLI:** `ionode adc {name} {pin}`

### PWM

| Operation | Subject | Payload | Response | Notes |
|-----------|---------|---------|----------|-------|
| Set PWM | `{name}.hal.pwm.{pin}.set` | `0`–`255` | `ok` | 8-bit duty cycle |
| Read PWM | `{name}.hal.pwm.{pin}.get` | `""` | `0`–`255` | Cached last-written value |

**CLI:** `ionode pwm {name} {pin} set {value}` / `ionode pwm {name} {pin} get`

### UART

| Operation | Subject | Payload | Response | Notes |
|-----------|---------|---------|----------|-------|
| Read UART | `{name}.hal.uart.read` | `""` | last line | Requires `serial_text` device |
| Write UART | `{name}.hal.uart.write` | text | `ok` | Requires `serial_text` device |

**CLI:** `ionode uart {name} read` / `ionode uart {name} write {text}`

### System Queries

| Operation | Subject | Payload | Response | Notes |
|-----------|---------|---------|----------|-------|
| Chip temperature | `{name}.hal.system.temperature` | `""` | `38.1` | Degrees C |
| Free heap | `{name}.hal.system.heap` | `""` | `156000` | Bytes |
| Uptime | `{name}.hal.system.uptime` | `""` | `3600` | Seconds since boot |
| WiFi RSSI | `{name}.hal.system.rssi` | `""` | `-52` | Signal strength dBm |
| Reset reason | `{name}.hal.system.reset_reason` | `""` | `software` | Last reset reason |
| NATS reconnects | `{name}.hal.system.nats_reconnects` | `""` | `1` | Reconnect count |

**CLI:** `ionode status {name}` (queries all system subjects and formats output)

---

## 3. Registered Devices

Operations on named sensors and actuators registered in `devices.json`.

### Read Sensor / Get Actuator State

| | |
|---|---|
| **Subject** | `{name}.hal.{dev}` or `{name}.hal.{dev}.get` |
| **Payload** | `""` |
| **Response** | Current value as plain text (e.g. `23.4`, `1`, `67.2`) |
| **CLI** | `ionode read {name} {dev}` |
| **Web** | Live value on device card |

### Set Actuator

| | |
|---|---|
| **Subject** | `{name}.hal.{dev}.set` |
| **Payload** | Value as plain text (`0`/`1` for relay/digital, `0`–`255` for PWM, `0xRRGGBB` for RGB) |
| **Response** | `ok` |
| **CLI** | `ionode write {name} {dev} {value}` |
| **Web** | Toggle/slider/color picker on device card |

### Device Info

| | |
|---|---|
| **Subject** | `{name}.hal.{dev}.info` |
| **Payload** | `""` |
| **Response** | JSON: `{"name":"temp","kind":"ntc_10k","value":23.4,"pin":2,"unit":"C"}` |
| **CLI** | `ionode read {name} {dev} --info` |

### List All Devices

| | |
|---|---|
| **Subject** | `{name}.hal.device.list` |
| **Payload** | `""` |
| **Response** | JSON array of all registered devices with current values |
| **CLI** | `ionode devices {name}` |
| **Web** | Devices panel in node detail view |

### Supported Device Kinds

| Kind | Type | Description |
|------|------|-------------|
| `digital_in` | sensor | `digitalRead` → 0/1 |
| `analog_in` | sensor | `analogRead` → 0–4095 |
| `ntc_10k` | sensor | 10K NTC thermistor, Steinhart-Hart, EMA-smoothed |
| `ldr` | sensor | Light-dependent resistor → 0–100% |
| `internal_temp` | sensor | ESP32 on-die temperature |
| `clock_hour` | sensor | Current hour 0–23 (NTP) |
| `clock_minute` | sensor | Current minute 0–59 (NTP) |
| `clock_hhmm` | sensor | HHMM format (e.g. 1430) |
| `nats_value` | sensor | Subscribes to NATS subject, stores last value |
| `serial_text` | sensor | Reads UART1 lines, parses numeric value |
| `digital_out` | actuator | `digitalWrite` |
| `relay` | actuator | `digitalWrite` with optional inversion |
| `pwm` | actuator | `analogWrite` 0–255 |
| `rgb_led` | actuator | Built-in RGB LED, packed `0xRRGGBB` |

---

## 4. Remote Configuration

Manage node configuration over NATS without touching the web UI or reflashing.

All config subjects use the `{name}.config.>` wildcard namespace.

### Get Full Config

| | |
|---|---|
| **Subject** | `{name}.config.get` |
| **Payload** | `""` |
| **Response** | Config JSON (WiFi password excluded) |
| **CLI** | `ionode config {name}` |
| **Web** | Node configuration panel |

### Device Registry Management

| Operation | Subject | Payload | Response |
|-----------|---------|---------|----------|
| Add device | `{name}.config.device.add` | `{"n":"temp","k":"ntc_10k","p":2,"u":"C","i":false}` | `{"ok":true}` |
| Remove device | `{name}.config.device.remove` | `{"n":"temp"}` | `{"ok":true}` |
| List devices | `{name}.config.device.list` | `""` | JSON array |

**Payload fields for device.add:**
- `n` — device name (required)
- `k` — device kind (required, see Supported Device Kinds)
- `p` — pin number (required, 255 for virtual)
- `u` — unit string (optional, default `""`)
- `i` — inverted flag (optional, default `false`)
- `bd` — baud rate (optional, for `serial_text` kind only)
- `ns` — NATS subject (optional, for `nats_value` kind only)

**CLI:** `ionode device add {name} {dev_name} {kind} {pin} [--unit C] [--inverted]`
**CLI:** `ionode device remove {name} {dev_name}`
**CLI:** `ionode device list {name}`

### Tag Management

| Operation | Subject | Payload | Response |
|-----------|---------|---------|----------|
| Set tag | `{name}.config.tag.set` | `greenhouse` | `{"ok":true}` |
| Get tag | `{name}.config.tag.get` | `""` | `{"tag":"greenhouse"}` |

Tags update the group subscription live (no reboot). Set empty string to clear tag.

**CLI:** `ionode tag {name} {tag}` / `ionode tag {name}` (get)

### Heartbeat Configuration

| Operation | Subject | Payload | Response |
|-----------|---------|---------|----------|
| Set interval | `{name}.config.heartbeat.set` | `60` | `{"ok":true}` |

Value in seconds, range 0–3600. Set 0 to disable heartbeat.

**CLI:** `ionode heartbeat {name} {seconds}`

### Rename Node

| Operation | Subject | Payload | Response | Notes |
|-----------|---------|---------|----------|-------|
| Rename | `{name}.config.name.set` | `new-name` | `{"ok":true}` | Triggers reboot |

**CLI:** `ionode rename {name} {new_name}`

---

## 5. Monitoring

### Health Heartbeat

| | |
|---|---|
| **Subject** | `_ion.heartbeat` *(subscribe)* |
| **Direction** | Node → network (periodic publish) |
| **Interval** | Configurable, default 60s |
| **CLI** | `ionode watch` / `ionode watch --heartbeats` |
| **Web** | Fleet tab, live node status indicators |

Heartbeat payload:

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

A node is considered **online** if a heartbeat was received within 2× its configured interval. After 3× the interval with no heartbeat, consider it **offline**.

### Threshold Events

| | |
|---|---|
| **Subject** | `{name}.events.{sensor}` *(subscribe)* |
| **Direction** | Node → network (edge-triggered) |
| **CLI** | `ionode watch` / `ionode watch --events` |
| **Web** | Event log panel, notification toast |

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

Events are edge-detected: fire once when the value crosses the threshold, re-arm only when the value returns to the safe side. Configurable cooldown prevents repeated firing.

### Event Configuration

| Operation | Subject | Payload | Response |
|-----------|---------|---------|----------|
| Set event | `{name}.config.event.set` | `{"n":"temp","t":28,"d":"above","cd":10}` | `{"ok":true}` |
| Clear event | `{name}.config.event.clear` | `{"n":"temp"}` | `{"ok":true}` |
| List events | `{name}.config.event.list` | `""` | JSON array |

**Event payload fields:**
- `n` — sensor name (required)
- `t` — threshold value (required)
- `d` — direction: `"above"` or `"below"` (required)
- `cd` — cooldown in seconds (required)

**CLI:** `ionode event set {name} {sensor} --above {value} --cooldown {seconds}`
**CLI:** `ionode event clear {name} {sensor}`
**CLI:** `ionode event list {name}`

Events persist across reboots (stored in `devices.json`).

---

## 6. CLI Command Reference

Complete command map for `ionode.sh`. Every command maps to one or more NATS operations defined above.

### Fleet

| Command | Description | NATS Operation |
|---------|-------------|----------------|
| `ionode discover` | List all nodes (verbose) | `_ion.discover` |
| `ionode ls` | List all nodes (compact table) | `_ion.discover` |
| `ionode ls --tag {tag}` | List nodes in group | `_ion.group.{tag}` |
| `ionode group {tag}` | Query group (verbose) | `_ion.group.{tag}` |
| `ionode info {name}` | Node details | `{name}.capabilities` |
| `ionode status` | Fleet health overview | `_ion.discover` + heartbeat cache |
| `ionode status {name}` | Single node health | `{name}.hal.system.*` (multiple) |

### Hardware

| Command | Description | NATS Operation |
|---------|-------------|----------------|
| `ionode read {name} {dev}` | Read sensor | `{name}.hal.{dev}` |
| `ionode write {name} {dev} {val}` | Set actuator | `{name}.hal.{dev}.set` |
| `ionode gpio {name} {pin} get` | Read GPIO | `{name}.hal.gpio.{pin}.get` |
| `ionode gpio {name} {pin} set {val}` | Write GPIO | `{name}.hal.gpio.{pin}.set` |
| `ionode adc {name} {pin}` | Read ADC | `{name}.hal.adc.{pin}.read` |
| `ionode pwm {name} {pin} set {val}` | Set PWM | `{name}.hal.pwm.{pin}.set` |
| `ionode pwm {name} {pin} get` | Read PWM | `{name}.hal.pwm.{pin}.get` |
| `ionode uart {name} read` | Read UART | `{name}.hal.uart.read` |
| `ionode uart {name} write {text}` | Write UART | `{name}.hal.uart.write` |
| `ionode devices {name}` | List registered devices | `{name}.hal.device.list` |

### Configuration

| Command | Description | NATS Operation |
|---------|-------------|----------------|
| `ionode config {name}` | Dump config | `{name}.config.get` |
| `ionode tag {name} {tag}` | Set tag | `{name}.config.tag.set` |
| `ionode tag {name}` | Get tag | `{name}.config.tag.get` |
| `ionode heartbeat {name} {sec}` | Set heartbeat interval | `{name}.config.heartbeat.set` |
| `ionode rename {name} {new}` | Rename node (reboots) | `{name}.config.name.set` |
| `ionode device add {name} ...` | Register device | `{name}.config.device.add` |
| `ionode device remove {name} {dev}` | Remove device | `{name}.config.device.remove` |
| `ionode device list {name}` | List devices (config) | `{name}.config.device.list` |

### Events

| Command | Description | NATS Operation |
|---------|-------------|----------------|
| `ionode event set {name} {sensor} ...` | Configure threshold | `{name}.config.event.set` |
| `ionode event clear {name} {sensor}` | Remove threshold | `{name}.config.event.clear` |
| `ionode event list {name}` | List configured events | `{name}.config.event.list` |

### Monitoring

| Command | Description | NATS Operation |
|---------|-------------|----------------|
| `ionode watch` | Live heartbeats + events | sub `_ion.heartbeat` + `*.events.>` |
| `ionode watch --heartbeats` | Heartbeats only | sub `_ion.heartbeat` |
| `ionode watch --events` | Events only | sub `*.events.>` |
| `ionode watch --tag {tag}` | Filter by tag | sub + filter on `tag` field |

---

## 7. Web UI Operation Map

How the fleet web dashboard maps to NATS operations.

### On Connect

1. Subscribe to `_ion.heartbeat` — builds/updates fleet state continuously
2. Subscribe to `*.events.>` — populates event log
3. Request `_ion.discover` — initial fleet population

### Fleet Tab

| UI Element | NATS Source | Update |
|------------|-------------|--------|
| Node cards (name, chip, tag, IP) | `_ion.discover` | On connect |
| Online/offline indicator | `_ion.heartbeat` | Live (2× interval = stale, 3× = offline) |
| Heap, RSSI, uptime | `_ion.heartbeat` | Live |
| Filter by tag | Client-side filter on heartbeat `tag` field | - |

### Node Detail Panel (click a node)

| UI Element | NATS Source | Update |
|------------|-------------|--------|
| Full capabilities | `{name}.capabilities` | On open |
| Sensor values | `{name}.hal.{dev}` per sensor | Poll or manual refresh |
| Actuator controls | `{name}.hal.{dev}.set` | On user action |
| Device list | `{name}.hal.device.list` | On open |
| System info | `{name}.hal.system.*` | On open |

### Configuration Panel (per node)

| UI Element | NATS Operation | Notes |
|------------|----------------|-------|
| Tag field | `config.tag.set` / `config.tag.get` | Live update, no reboot |
| Heartbeat interval | `config.heartbeat.set` | |
| Add device form | `config.device.add` | |
| Remove device button | `config.device.remove` | Confirm dialog |
| Event configuration | `config.event.set` / `config.event.clear` | Per sensor |
| Rename | `config.name.set` | Warns about reboot |
| Config dump | `config.get` | Read-only display |

### Event Log

| UI Element | NATS Source | Update |
|------------|-------------|--------|
| Event feed | `*.events.>` subscription | Live, newest at top |
| Event details | Payload fields | Inline display |

---

## 8. Error Handling

### NATS Timeout

If a node doesn't respond within 2 seconds, it is considered unreachable.

- **CLI:** Print `[timeout] {name} did not respond` and continue
- **Web:** Mark node as unreachable in UI

### Invalid Payload

Nodes return `{"error":"..."}` for malformed requests.

- **CLI:** Print error message from response
- **Web:** Show error toast

### Discovery with Zero Replies

`_ion.discover` may return zero replies if no nodes are online.

- **CLI:** Print `No IOnode devices found on the network`
- **Web:** Show empty state with setup instructions

---

## 9. Implementation Notes

### Actuator State Persistence

Relay and digital output (`relay`, `digital_out`) states are persisted in `devices.json` as the `"v"` field. On boot, saved values are restored automatically. PWM and RGB LED values are NOT persisted — resuming arbitrary PWM values on boot could be unsafe.

Persistence uses debounced saves (5-second delay) to protect flash from rapid writes.

### Online Event

When a node connects to NATS, it publishes an online event to `{name}.events` (note: no sensor suffix). To capture both threshold events AND online events, subscribe to both `{name}.events.>` and `{name}.events`.

### NATS Reconnect Counter

The `nats_reconnects` counter increments on every NATS connection, including the first one after boot. A freshly booted node that connects successfully will report `nats_reconnects: 1`. This is expected behavior — it counts total connections, not just reconnections.

---

## 10. Future Considerations (Not in v0.2.0)

Documented here for planning purposes. These are not part of the current contract.

### JetStream Persistence

Publishing heartbeats and events to JetStream streams would enable historical queries (last 24h of heartbeats, event history). This is a server-side configuration change — nodes publish to the same subjects, a JetStream stream captures them. Zero firmware changes.

### Bulk Operations

CLI operations that target multiple nodes (e.g. `ionode tag --all greenhouse`, `ionode device add --tag greenhouse ...`). Requires iterating over discovery results and issuing per-node commands.

### Config Templates / Profiles

Named configuration presets that can be applied to nodes: `ionode provision {name} --template greenhouse`. Template defines tag, devices, events. Stored as JSON files in `~/.config/ionode/templates/`.

### Firmware Version Tracking

Fleet inventory report showing firmware versions across all nodes, identifying nodes that need updating.

### Fleet Dashboard (Web)

A single-file HTML dashboard that connects to NATS via WebSocket. See the [Fleet Dashboard](#) section in the main README. Requires NATS WebSocket listener enabled.

### Daemon (IOnode Hub)

Persistent process that subscribes to heartbeats and events, maintains fleet state, implements complex rules, provides a REST API for the web dashboard, and enables JetStream history queries. This would be a separate project.
