# IOnode v0.2.0 - Testing Guide

## Prerequisites

- IOnode flashed and connected to WiFi + NATS
- `nats` CLI installed ([nats-io/natscli](https://github.com/nats-io/natscli))
- NATS server running and reachable
- Serial monitor open: `pio device monitor`

All examples assume device name `ionode-01`. Replace with your actual device name.

---

## 1. Verify Baseline

Confirm the node is online and responding:

```bash
# Discovery - should return full capabilities JSON
nats req _ion.discover '' --timeout 3s

# Direct capabilities
nats req ionode-01.capabilities '' --timeout 3s

# Config dump (no wifi_pass in response)
nats req ionode-01.config.get '' --timeout 3s
```

Expected: JSON with `device`, `version` ("0.2.0"), `chip`, `ip`, `hal`, `devices` array.

---

## 2. NATS Remote Device Management

### 2a. List current devices

```bash
nats req ionode-01.config.device.list '' --timeout 3s
```

Expected: JSON array of registered devices (chip_temp, clock_hour, etc.).

### 2b. Add a relay device

```bash
nats req ionode-01.config.device.add '{"n":"test_relay","k":"relay","p":5,"u":"","i":false}' --timeout 3s
```

Expected: `{"ok":true}`

Verify it appears:

```bash
nats req ionode-01.config.device.list '' --timeout 3s
```

### 2c. Add a sensor

```bash
nats req ionode-01.config.device.add '{"n":"test_temp","k":"ntc_10k","p":2,"u":"C","i":false}' --timeout 3s
```

Expected: `{"ok":true}`

### 2d. Remove a device

```bash
nats req ionode-01.config.device.remove '{"n":"test_temp"}' --timeout 3s
```

Expected: `{"ok":true}`

Verify it's gone:

```bash
nats req ionode-01.config.device.list '' --timeout 3s
```

### 2e. Error cases

```bash
# Duplicate name
nats req ionode-01.config.device.add '{"n":"test_relay","k":"relay","p":6}' --timeout 3s
# Expected: {"error":"register_failed",...}

# Missing fields
nats req ionode-01.config.device.add '{"k":"relay","p":6}' --timeout 3s
# Expected: {"error":"missing_field","detail":"n (name)"}

# Unknown kind
nats req ionode-01.config.device.add '{"n":"x","k":"bogus","p":6}' --timeout 3s
# Expected: {"error":"unknown_kind","detail":"bogus"}
```

### 2f. Clean up test relay

```bash
nats req ionode-01.config.device.remove '{"n":"test_relay"}' --timeout 3s
```

---

## 3. Actuator State Persistence

### 3a. Add a relay and set it ON

```bash
nats req ionode-01.config.device.add '{"n":"persist_test","k":"relay","p":5,"u":"","i":false}' --timeout 3s
nats req ionode-01.hal.persist_test.set '1' --timeout 3s
```

### 3b. Verify state is saved

Wait 6 seconds (5s debounce), then check serial output for `Devices: saved`.

Or via web UI: Config tab > devices.json should show `"v":1` on the relay entry.

### 3c. Reboot and verify restore

```bash
nats req ionode-01.config.get '' --timeout 3s   # confirm it's alive
# Now reboot:
curl -X POST http://<device-ip>/api/reboot
```

After reboot, check the relay state:

```bash
nats req ionode-01.hal.persist_test.get '' --timeout 3s
```

Expected: `1` (relay restored to ON).

### 3d. Clean up

```bash
nats req ionode-01.hal.persist_test.set '0' --timeout 3s
nats req ionode-01.config.device.remove '{"n":"persist_test"}' --timeout 3s
```

---

## 4. Tags and Group Discovery

### 4a. Set a tag

```bash
nats req ionode-01.config.tag.set 'greenhouse' --timeout 3s
```

Expected: `{"ok":true}`

### 4b. Verify tag in config

```bash
nats req ionode-01.config.tag.get '' --timeout 3s
```

Expected: `{"tag":"greenhouse"}`

### 4c. Verify tag in discovery

```bash
nats req _ion.discover '' --timeout 3s
```

Expected: Response includes `"tag":"greenhouse"`.

### 4d. Group query

```bash
nats req _ion.group.greenhouse '' --timeout 3s
```

Expected: Same capabilities response as discovery (the node responds to its group subject).

### 4e. Change tag at runtime

```bash
nats req ionode-01.config.tag.set 'kitchen' --timeout 3s
```

Serial should show unsubscribe of old group and subscribe to new one.

```bash
# Old group should get no response
nats req _ion.group.greenhouse '' --timeout 2s
# Expected: timeout / no responders

# New group works
nats req _ion.group.kitchen '' --timeout 3s
# Expected: capabilities response
```

### 4f. Clear tag

```bash
nats req ionode-01.config.tag.set '' --timeout 3s
```

### 4g. Verify persistence

Set tag, reboot, verify it comes back:

```bash
nats req ionode-01.config.tag.set 'test_persist' --timeout 3s
# Wait 3s for config debounce, then reboot
curl -X POST http://<device-ip>/api/reboot
# After reboot:
nats req ionode-01.config.tag.get '' --timeout 5s
# Expected: {"tag":"test_persist"}
```

Clean up:

```bash
nats req ionode-01.config.tag.set '' --timeout 3s
```

---

## 5. Health Heartbeat

### 5a. Subscribe and observe

```bash
# In a separate terminal:
nats sub '_ion.heartbeat'
```

Expected: A JSON message every 60 seconds (default) containing `device`, `version`, `uptime`, `heap`, `rssi`, `sensors`, `actuators`, etc.

### 5b. Change interval

```bash
nats req ionode-01.config.heartbeat.set '5' --timeout 3s
```

Now heartbeats should arrive every 5 seconds in the subscriber terminal.

### 5c. Disable heartbeat

```bash
nats req ionode-01.config.heartbeat.set '0' --timeout 3s
```

Heartbeat messages should stop.

### 5d. Restore default

```bash
nats req ionode-01.config.heartbeat.set '60' --timeout 3s
```

### 5e. Validation

```bash
nats req ionode-01.config.heartbeat.set '9999' --timeout 3s
# Expected: {"error":"invalid_value","detail":"0-3600 seconds (0=disabled)"}
```

---

## 6. New HAL System Subjects

```bash
# WiFi signal strength
nats req ionode-01.hal.system.rssi '' --timeout 3s
# Expected: e.g. -52

# Last reset reason
nats req ionode-01.hal.system.reset_reason '' --timeout 3s
# Expected: e.g. software, power_on, watchdog, etc.

# NATS reconnect count
nats req ionode-01.hal.system.nats_reconnects '' --timeout 3s
# Expected: e.g. 1
```

---

## 7. Threshold Events

### 7a. Subscribe to events

In a separate terminal:

```bash
nats sub 'ionode-01.events.>'
```

### 7b. Set an event on chip_temp

```bash
# Set threshold just below current chip temp to trigger immediately
# First, check current temp:
nats req ionode-01.hal.system.temperature '' --timeout 3s
# e.g. returns "38.5"

# Set threshold below current value so it fires:
nats req ionode-01.config.event.set '{"n":"chip_temp","t":30.0,"d":"above","cd":10}' --timeout 3s
```

Expected: `{"ok":true}`

Within 1 second, the subscriber should receive:

```json
{"event":"threshold","device":"ionode-01","sensor":"chip_temp","value":38.5,"threshold":30.0,"direction":"above","unit":"C"}
```

### 7c. Verify edge detection (fires once, then re-arms)

After the first fire, no more events should arrive (the value stays above threshold). The event won't re-fire until the value drops below 30.0 and rises above again, or until the cooldown expires and the event is re-armed.

### 7d. List configured events

```bash
nats req ionode-01.config.event.list '' --timeout 3s
```

Expected: JSON array showing chip_temp with threshold, direction, cooldown, armed status.

### 7e. Clear an event

```bash
nats req ionode-01.config.event.clear '{"n":"chip_temp"}' --timeout 3s
```

Expected: `{"ok":true}`

Verify:

```bash
nats req ionode-01.config.event.list '' --timeout 3s
# Expected: []
```

### 7f. Event persistence

```bash
nats req ionode-01.config.event.set '{"n":"chip_temp","t":50.0,"d":"above","cd":30}' --timeout 3s
# Wait 6s for debounce flush, then reboot
curl -X POST http://<device-ip>/api/reboot
# After reboot:
nats req ionode-01.config.event.list '' --timeout 5s
# Expected: chip_temp event config restored
```

Clean up:

```bash
nats req ionode-01.config.event.clear '{"n":"chip_temp"}' --timeout 3s
```

### 7g. Error cases

```bash
# Missing device name
nats req ionode-01.config.event.set '{"t":28}' --timeout 3s
# Expected: {"error":"missing_field","detail":"n (device name)"}

# Invalid direction
nats req ionode-01.config.event.set '{"n":"chip_temp","t":28,"d":"sideways","cd":10}' --timeout 3s
# Expected: {"error":"invalid_direction","detail":"use 'above' or 'below'"}

# Not a sensor
nats req ionode-01.config.event.set '{"n":"rgb_led","t":28,"d":"above","cd":10}' --timeout 3s
# Expected: {"error":"not_sensor",...}
```

---

## 8. Web UI

Open `http://<device-ip>/` in a browser.

### Config tab
- [ ] Tag field is visible below Timezone
- [ ] Tag value loads/saves correctly
- [ ] devices.json editor shows `"v":` on relay entries and `"et"/"ed"/"ec"` on sensors with events

### Devices tab
- [ ] Sensor cards with events show: `Event: above 30.0 (cd:10s) [armed]`
- [ ] Relay ON/OFF toggles work and persist (check after reboot)

### Status tab
- [ ] Tag shown (or "(none)" if empty)
- [ ] Heartbeat interval shown (e.g. "every 60s" or "disabled")
- [ ] NATS Reconnects count shown
- [ ] Events Fired count shown

---

## 9. Full Fleet Workflow (End-to-End)

Run this complete sequence to validate all features together:

```bash
# 1. Tag the node
nats req ionode-01.config.tag.set 'greenhouse' --timeout 3s

# 2. Add devices
nats req ionode-01.config.device.add '{"n":"temp","k":"ntc_10k","p":2,"u":"C"}' --timeout 3s
nats req ionode-01.config.device.add '{"n":"fan","k":"relay","p":8,"i":true}' --timeout 3s

# 3. Set up threshold event
nats req ionode-01.config.event.set '{"n":"temp","t":28.0,"d":"above","cd":10}' --timeout 3s

# 4. Set heartbeat to 10s for testing
nats req ionode-01.config.heartbeat.set '10' --timeout 3s

# 5. Start listeners (each in a separate terminal)
nats sub '*.events.>'
nats sub '_ion.heartbeat'

# 6. Turn on the fan
nats req ionode-01.hal.fan.set '1' --timeout 3s

# 7. Verify fan state
nats req ionode-01.hal.fan.get '' --timeout 3s
# Expected: 1

# 8. Query the group
nats req _ion.group.greenhouse '' --timeout 3s
# Expected: capabilities with tag, temp sensor, fan actuator

# 9. Reboot - fan should restore ON
curl -X POST http://<device-ip>/api/reboot
# Wait for reconnect...
nats req ionode-01.hal.fan.get '' --timeout 10s
# Expected: 1

# 10. Clean up
nats req ionode-01.hal.fan.set '0' --timeout 3s
nats req ionode-01.config.event.clear '{"n":"temp"}' --timeout 3s
nats req ionode-01.config.device.remove '{"n":"temp"}' --timeout 3s
nats req ionode-01.config.device.remove '{"n":"fan"}' --timeout 3s
nats req ionode-01.config.tag.set '' --timeout 3s
nats req ionode-01.config.heartbeat.set '60' --timeout 3s
```

---

## Quick Reference - All New NATS Subjects

| Subject | Payload | Description |
|---------|---------|-------------|
| `{name}.config.get` | _(empty)_ | Dump config (no wifi_pass) |
| `{name}.config.device.list` | _(empty)_ | List all devices |
| `{name}.config.device.add` | `{"n":"x","k":"relay","p":5,"u":"","i":false}` | Register device |
| `{name}.config.device.remove` | `{"n":"x"}` | Remove device |
| `{name}.config.tag.set` | `greenhouse` | Set fleet tag |
| `{name}.config.tag.get` | _(empty)_ | Get current tag |
| `{name}.config.heartbeat.set` | `60` | Set interval (0=off) |
| `{name}.config.event.set` | `{"n":"x","t":28.0,"d":"above","cd":10}` | Configure threshold event |
| `{name}.config.event.clear` | `{"n":"x"}` | Remove event |
| `{name}.config.event.list` | _(empty)_ | List configured events |
| `{name}.config.name.set` | `new-name` | Rename node (reboots) |
| `{name}.hal.system.rssi` | _(empty)_ | WiFi RSSI |
| `{name}.hal.system.reset_reason` | _(empty)_ | Last reset reason |
| `{name}.hal.system.nats_reconnects` | _(empty)_ | Reconnect count |
| `_ion.group.{tag}` | _(empty)_ | Query all nodes in group |
| `_ion.heartbeat` | _(subscribe only)_ | Periodic health messages |
| `{name}.events.{sensor}` | _(subscribe only)_ | Threshold event notifications |
