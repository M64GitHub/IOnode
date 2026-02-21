# IOnode CLI Reference

`ionode` is a fleet management tool for NATS-addressable hardware nodes. It wraps NATS request/reply operations into human-friendly commands with colored, formatted output.

Every command maps directly to NATS subjects documented in [NATS-API.md](NATS-API.md).

---

## Install

```bash
# Symlink to PATH
sudo ln -sf "$(pwd)/cli/ionode" /usr/local/bin/ionode

# Verify
ionode --version
```

### Dependencies

| Tool | Required | Install |
|------|----------|---------|
| [nats CLI](https://github.com/nats-io/natscli) | yes | `go install github.com/nats-io/natscli/nats@latest` |
| [jq](https://jqlang.github.io/jq/) | yes | `apt install jq` / `brew install jq` |
| bash 4+ | yes | Included on Linux/macOS |

---

## Configuration

The CLI resolves the NATS server URL in this priority order:

1. **`--server` / `-s` flag** - `ionode --server nats://10.0.0.1:4222 discover`
2. **`IONODE_NATS_URL` env** - `export IONODE_NATS_URL="nats://192.168.1.100:4222"`
3. **Config file** - `~/.config/ionode/config`
4. **Default** - `nats://localhost:4222`

### Config File

XDG-compliant location: `~/.config/ionode/config`

```bash
mkdir -p ~/.config/ionode
cat > ~/.config/ionode/config << 'EOF'
NATS_URL=nats://192.168.1.100:4222
EOF
```

---

## Global Options

| Option | Description |
|--------|-------------|
| `--server`, `-s` `<url>` | NATS server URL |
| `--no-color` | Disable colored output |
| `--json` | Raw JSON output (implies `--no-color`) |
| `--version`, `-V` | Show version |
| `--help`, `-h` | Show help |

Colors are also disabled when:
- `NO_COLOR` env is set ([no-color.org](https://no-color.org))
- stdout is not a TTY (piping to another command)

---

## Fleet Commands

### `ionode discover`

Find all IOnode/WireClaw nodes on the network. Shows device cards with chip type, IP, heap, firmware version, and registered devices.

```
$ ionode discover

━━ Fleet Discovery

  ionode-01  ESP32-S3
    IP 192.168.178.43    heap 192 KB      v0.2.0
    chip_temp clock_hour clock_minute clock_hhmm rgb_led

  1 node(s) found  ·  nats://localhost:4222
```

**NATS:** `_ion.discover` (multi-reply)

### `ionode ls [--tag <tag>]`

Compact fleet table. Columns: device name, chip, tag, heap, RSSI, sensor count, actuator count.

```
$ ionode ls

━━ Fleet

  DEVICE             CHIP         TAG    HEAP     RSSI      SENS    ACT
────────────────────────────────────────────────────────────────────────
  ionode-01          ESP32-S3     -      192 KB   -74dB     4       1

  1 node(s)
```

Filter by tag:

```
$ ionode ls --tag greenhouse
```

**NATS:** `_ion.discover` or `_ion.group.{tag}` (multi-reply)

### `ionode info <device>`

Deep dive on a single node. Queries capabilities plus all system HAL endpoints (temperature, heap, uptime, RSSI, reset reason, reconnect count). Shows HAL feature matrix and full device list.

```
$ ionode info ionode-01

━━ Node Detail  ·  ionode-01

  Firmware:        IOnode v0.2.0
  Chip:            ESP32-S3
  IP:              192.168.178.43
  Tag:             -
  Free heap:       189 KB
  Uptime:          3h 6m
  WiFi signal:     ▂▄▆█ -74dBm
  Last reset:      power_on
  NATS reconnects: 2

  HAL: gpio adc pwm dac uart system_temp

  Devices:
    chip_temp        internal_temp  37.2C
    clock_hour       clock_hour     0.0h
    rgb_led          rgb_led        0
```

**NATS:** `{name}.capabilities` + `{name}.hal.system.*`

### `ionode group <tag>`

Alias for `ionode ls --tag <tag>`.

### `ionode status [<device>]`

Without arguments: fleet overview (same as `ionode ls`).

With a device name: focused health display with color-coded temperature and heap.

```
$ ionode status ionode-01

━━ Status  ·  ionode-01

  Chip temp:       37.2°C
  Free heap:       192 KB
  Uptime:          3h 6m
  WiFi signal:     ▂▄▆█ -74dBm
  Last reset:      power_on
  NATS reconnects: 2
```

**NATS:** `{name}.hal.system.*` (multiple queries)

---

## Hardware Commands

### `ionode read <device> <sensor> [--info]`

Read a registered sensor value. With `--info`, shows full device metadata (name, kind, pin, unit, value).

```
$ ionode read ionode-01 chip_temp
  chip_temp  37.2 C

$ ionode read ionode-01 chip_temp --info
  Name:            chip_temp
  Kind:            internal_temp
  Pin:             255
  Value:           37.2 C
```

**NATS:** `{name}.hal.{dev}` / `{name}.hal.{dev}.info`

### `ionode write <device> <actuator> <value>`

Set an actuator value.

```
$ ionode write ionode-01 rgb_led 16711680
  rgb_led  ← 16711680

$ ionode write ionode-01 fan 1
  fan  ← 1
```

Values: `0`/`1` for relay/digital_out, `0`–`255` for PWM, `0xRRGGBB` (as decimal) for RGB LED.

**NATS:** `{name}.hal.{dev}.set`

### `ionode gpio <device> <pin> get|set [value]`

Direct GPIO access without registering a device.

```
$ ionode gpio ionode-01 0 get
  GPIO 0  1  (HIGH)

$ ionode gpio ionode-01 8 set 1
  GPIO 8  ← 1  (HIGH)
```

**NATS:** `{name}.hal.gpio.{pin}.get` / `{name}.hal.gpio.{pin}.set`

### `ionode adc <device> <pin>`

Read raw 12-bit ADC value with a visual bar.

```
$ ionode adc ionode-01 2
  ADC 2  604  ██░░░░░░░░░░░░░░░░░░  14%
```

**NATS:** `{name}.hal.adc.{pin}.read`

### `ionode pwm <device> <pin> get|set [value]`

Read or write PWM duty cycle (0–255).

```
$ ionode pwm ionode-01 3 set 128
  PWM 3  ← 128/255  (50%)

$ ionode pwm ionode-01 3 get
  PWM 3  128/255  (50%)
```

**NATS:** `{name}.hal.pwm.{pin}.set` / `{name}.hal.pwm.{pin}.get`

### `ionode uart <device> read|write [text]`

Read last received UART line or send text. Requires a `serial_text` device registered on the node.

```
$ ionode uart ionode-01 read
  UART ←  23.5

$ ionode uart ionode-01 write "AT+RST"
  UART →  AT+RST
```

**NATS:** `{name}.hal.uart.read` / `{name}.hal.uart.write`

### `ionode devices <device>`

List all registered devices with current values.

```
$ ionode devices ionode-01

━━ Devices  ·  ionode-01

  NAME             KIND           VALUE      UNIT
────────────────────────────────────────────────────
  chip_temp        internal_temp  37.2       C
  clock_hour       clock_hour     14.0       h
  rgb_led          rgb_led        0

  5 device(s)
```

**NATS:** `{name}.hal.device.list`

---

## Configuration Commands

### `ionode config <device>`

Dump the current node configuration (WiFi password excluded).

```
$ ionode config ionode-01

━━ Config  ·  ionode-01

  Device name:     ionode-01
  WiFi SSID:       MyNetwork
  NATS host:       192.168.1.100:4222
  Timezone:        CET-1CEST,M3.5.0,M10.5.0/3
  Tag:             -
  Heartbeat:       every 60s
```

**NATS:** `{name}.config.get`

### `ionode tag <device> [tag]`

Get or set the fleet tag.

```
$ ionode tag ionode-01
  ionode-01  tag: (none)

$ ionode tag ionode-01 greenhouse
  ionode-01  tag → #greenhouse
```

**NATS:** `{name}.config.tag.get` / `{name}.config.tag.set`

### `ionode heartbeat <device> <seconds>`

Set the heartbeat publish interval. 0 disables heartbeat.

```
$ ionode heartbeat ionode-01 30
  ionode-01  heartbeat → every 30s

$ ionode heartbeat ionode-01 0
  ionode-01  heartbeat → disabled
```

**NATS:** `{name}.config.heartbeat.set`

### `ionode rename <device> <new_name>`

Rename the device. Saves to flash and reboots.

```
$ ionode rename ionode-01 greenhouse-01
  ⚠  Rename will reboot the device.
  ionode-01  →  greenhouse-01  (rebooting...)
```

**NATS:** `{name}.config.name.set`

### `ionode device add <device> <name> <kind> <pin> [options]`

Register a new sensor or actuator on the node.

Options:
- `--unit <U>` - unit string (e.g. `C`, `%`, `ppm`)
- `--inverted` - invert digital logic
- `--baud <N>` - baud rate (for `serial_text` kind)
- `--nats <subject>` - NATS subject (for `nats_value` kind)

```
$ ionode device add ionode-01 temp ntc_10k 2 --unit C
  +  temp  ntc_10k  pin 2

$ ionode device add ionode-01 fan relay 8 --inverted
  +  fan  relay  pin 8
```

**NATS:** `{name}.config.device.add`

### `ionode device remove <device> <name>`

Remove a registered device.

```
$ ionode device remove ionode-01 temp
  −  temp  removed
```

**NATS:** `{name}.config.device.remove`

### `ionode device list <device>`

Alias for `ionode devices <device>`.

---

## Event Commands

### `ionode event set <device> <sensor> --above|--below <value> [--cooldown <s>]`

Configure a threshold event. Fires a NATS notification when the sensor value crosses the threshold. Edge-detected with auto-reset. Default cooldown: 10 seconds.

```
$ ionode event set ionode-01 chip_temp --above 45 --cooldown 30
  ⚡  chip_temp  ▲ 45  cd=30s
```

**NATS:** `{name}.config.event.set`

### `ionode event clear <device> <sensor>`

Remove a threshold event.

```
$ ionode event clear ionode-01 chip_temp
  −  chip_temp  event cleared
```

**NATS:** `{name}.config.event.clear`

### `ionode event list <device>`

List all configured events with armed/fired status.

```
$ ionode event list ionode-01

━━ Events  ·  ionode-01

  ⚡  chip_temp       ▲ 45.0  cd=30s  ● armed

  1 event(s)
```

**NATS:** `{name}.config.event.list`

---

## Monitor Commands

### `ionode watch [--heartbeats] [--events] [--tag <tag>]`

Live monitoring stream. Subscribes to heartbeat and/or event subjects and displays formatted output in real time.

```
$ ionode watch

━━ Live Monitor
  Mode: heartbeats + events
  Server: nats://localhost:4222
  Press Ctrl+C to stop
────────────────────────────────────────────────────────────────

  14:23:01  ♥  ionode-01       #greenhouse  ↑3h 6m  ⬡189 KB  ▂▄▆█
  14:23:05  ⚡ EVENT  ionode-01  chip_temp  ▲ 45.0 → 46.2C
  14:24:01  ♥  ionode-01       #greenhouse  ↑3h 7m  ⬡189 KB  ▂▄▆█
```

Modes:
- `ionode watch` - heartbeats + events (default)
- `ionode watch --heartbeats` - heartbeats only
- `ionode watch --events` - events only
- `ionode watch --tag greenhouse` - filter by tag

**NATS:** `_ion.heartbeat` + `*.events.>` (subscribe)

---

## Output

### Color Palette

The CLI uses true color (24-bit ANSI) matching the [ionode.io](https://ionode.io) website:

| Element | Color | Hex |
|---------|-------|-----|
| Accent / brand | Orange | `#ff8c00` |
| Sensors | Blue | `#4a9eff` |
| Actuators | Purple | `#b06aff` |
| OK / online | Green | `#00d4aa` |
| Error / offline | Red | `#ff4757` |
| Labels | Dim | `#8b92a8` |
| Muted | Dark | `#4a5068` |

### Visual Elements

- **Signal bars:** `▂▄▆█` - color-coded by RSSI strength
- **ADC bars:** `██░░░░░░░░░░░░░░░░░░` - proportional to 12-bit range
- **Event indicators:** `▲` above / `▼` below thresholds
- **Status dots:** `●` green (armed/online) / orange (fired) / red (offline)
- **Spinner:** `⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏` - during network operations

---

## NATS Subject Mapping

Every CLI command maps to one or more NATS operations. See [NATS-API.md](NATS-API.md) for the complete protocol specification.

| Command | NATS Subject |
|---------|-------------|
| `discover` | `_ion.discover` |
| `ls` | `_ion.discover` |
| `ls --tag X` | `_ion.group.X` |
| `info` | `{name}.capabilities` + `{name}.hal.system.*` |
| `status` | `{name}.hal.system.*` |
| `read` | `{name}.hal.{dev}` |
| `write` | `{name}.hal.{dev}.set` |
| `gpio get/set` | `{name}.hal.gpio.{pin}.get/set` |
| `adc` | `{name}.hal.adc.{pin}.read` |
| `pwm get/set` | `{name}.hal.pwm.{pin}.get/set` |
| `uart read/write` | `{name}.hal.uart.read/write` |
| `devices` | `{name}.hal.device.list` |
| `config` | `{name}.config.get` |
| `tag` | `{name}.config.tag.get/set` |
| `heartbeat` | `{name}.config.heartbeat.set` |
| `rename` | `{name}.config.name.set` |
| `device add/remove` | `{name}.config.device.add/remove` |
| `event set/clear/list` | `{name}.config.event.*` |
| `watch` | `_ion.heartbeat` + `*.events.>` |

---

## Error Handling

All commands provide clean error messages with usage hints:

```
$ ionode info
error: missing argument
  usage: ionode info <device>

$ ionode gpio ionode-01 5 set
error: missing value
  usage: ionode gpio <device> <pin> set <0|1>
```

Unreachable nodes show a timeout message:

```
$ ionode read offline-node chip_temp
  ⏱  offline-node - no response
```
