# IOnode v0.3.0 - I2C Sensors & OLED Display

I2C support unlocks the largest sensor ecosystem and OLED displays. Five sensor drivers, a text display, and raw bus access - all manageable via NATS, CLI, and web UI.

---

## I2C Sensor Drivers

Raw Wire.h drivers for five sensor families. No external libraries - each driver is 50-150 lines of register reads, keeping flash usage under 18KB total.

| Kind | Sensor | Channels | Description |
|------|--------|----------|-------------|
| `i2c_bme280` | BME280 | 3 | Temperature (pin=0), humidity (pin=1), pressure (pin=2) |
| `i2c_bh1750` | BH1750 | 1 | Ambient light in lux |
| `i2c_sht31` | SHT31 | 2 | Temperature (pin=0), humidity (pin=1) |
| `i2c_ads1115` | ADS1115 | 4 | 16-bit ADC, channels via pin 0-3 |
| `i2c_generic` | Any | 1 | Raw register read with configurable address, register, length, scale |

### Multi-Value Sensors

Sensors that return multiple values (BME280, SHT31, ADS1115) use the `pin` field as a channel selector. Register one device per channel:

```bash
ionode device add ionode-01 bme_temp i2c_bme280 0 --unit C --i2c-addr 118
ionode device add ionode-01 bme_humi i2c_bme280 1 --unit % --i2c-addr 118
ionode device add ionode-01 bme_pres i2c_bme280 2 --unit hPa --i2c-addr 118
```

Per-address reading cache ensures multiple channel devices don't cause redundant I2C transactions. Each channel device gets its own EMA smoothing, sparkline history, and threshold events.

### Generic I2C Sensor

For any register-based I2C device without a dedicated driver:

```bash
ionode device add ionode-01 custom_pres i2c_generic 16 --unit hPa --i2c-addr 80 --reg-len 2 --scale 0.01
```

Reads register `pin` (0-255) from `i2c_addr`, combines bytes big-endian, multiplies by `scale`.

---

## SSD1306 OLED Display

128x64 and 128x32 OLED displays as actuator devices. Text-only with built-in 5x7 font (21 chars per line).

```bash
ionode device add ionode-01 display ssd1306 0 --i2c-addr 60 --template "T:{bme_temp}C H:{bme_humi}%\nP:{bme_pres}hPa"
```

### Template System

Templates auto-refresh every 5 seconds. `{device_name}` tokens are replaced with live sensor readings:

| Token | Value |
|-------|-------|
| `{bme_temp}` | Current value of the `bme_temp` device |
| `{ip}` | Node IP address |
| `{heap}` | Free heap in KB |
| `{uptime}` | Uptime string (e.g. `1d 4h`) |
| `{name}` | Device name |

Raw text (no interpolation) can be sent via NATS with `!` prefix:

```bash
ionode write ionode-01 display "!Hello World"
```

### Display Sizes

- `pin=0` - 128x64 (8 lines x 21 chars)
- `pin=1` - 128x32 (4 lines x 21 chars)

---

## I2C Bus Access (HAL)

Raw I2C bus operations via NATS, even without registered devices.

| Subject | Payload | Response |
|---------|---------|----------|
| `{name}.hal.i2c.scan` | - | `[60,104,118]` |
| `{name}.hal.i2c.{addr}.detect` | - | `true` / `false` |
| `{name}.hal.i2c.{addr}.read` | `{"reg":0,"len":2}` | `[0,255]` |
| `{name}.hal.i2c.{addr}.write` | `{"reg":0,"data":[1,2]}` | `ok` |
| `{name}.hal.i2c.recover` | - | `ok` |

Addresses in subjects are decimal (e.g., `i2c.60.detect` for 0x3C).

### CLI

```bash
ionode i2c ionode-01 scan                    # scan bus, formatted table
ionode i2c ionode-01 detect 60               # check specific address
ionode i2c ionode-01 read 118 --reg 0 --len 2  # read register
ionode i2c ionode-01 write 60 --reg 0 --data 1,2  # write register
ionode i2c ionode-01 recover                 # bus recovery (9x SCL toggle)
```

---

## I2C Pin Mapping

Fixed pins per chip variant (same pattern as UART):

| Chip | SDA | SCL |
|------|-----|-----|
| ESP32-C6 | 6 | 7 |
| ESP32-S3 | 8 | 9 |
| ESP32-C3 | 4 | 6 |
| Classic ESP32 | 21 | 22 |

Bus is reference-counted: initialized on first I2C device registration, deinitialized when the last I2C device is removed.

---

## Web UI Updates

- **I2C device types** in the Add Device dropdown (i2c_bme280, i2c_bh1750, i2c_sht31, i2c_ads1115, i2c_generic, ssd1306)
- **I2C form fields** - address, channel, unit, template, generic settings (register, length, scale)
- **I2C scan** button - scans the bus and shows detected addresses
- **SSD1306 control card** - text input with send and clear buttons
- **`/api/i2c/scan`** endpoint - returns detected I2C addresses
- **`/api/devices/display`** endpoint - send text to SSD1306 displays

---

## Device Registry Changes

### New Device Kinds

| Kind | Type | Description |
|------|------|-------------|
| `i2c_generic` | sensor | Raw I2C register read with configurable scale |
| `i2c_bme280` | sensor | BME280 temperature/humidity/pressure |
| `i2c_bh1750` | sensor | BH1750 ambient light (lux) |
| `i2c_sht31` | sensor | SHT31 temperature/humidity |
| `i2c_ads1115` | sensor | ADS1115 16-bit ADC |
| `ssd1306` | actuator | SSD1306 OLED text display |

### New Persistence Fields

| JSON Key | Field | Description |
|----------|-------|-------------|
| `ia` | `i2c_addr` | I2C slave address (0-127) |
| `dt` | `disp_template` | Display template string (SSD1306) |
| `rl` | `i2c_reg_len` | Register read length for i2c_generic (1 or 2) |
| `sc` | `i2c_scale` | Scale multiplier for i2c_generic |

### NATS device.add Payload

```json
{"n":"bme_temp","k":"i2c_bme280","p":0,"u":"C","i":false,"ia":118}
{"n":"display","k":"ssd1306","p":0,"u":"","i":false,"ia":60,"dt":"T:{bme_temp}C"}
{"n":"custom","k":"i2c_generic","p":16,"u":"hPa","i":false,"ia":80,"rl":2,"sc":0.01}
```

---

## Files Created

| File | Purpose |
|------|---------|
| `include/i2c_devices.h` | I2C subsystem header |
| `src/i2c_devices.cpp` | I2C bus management + BME280/BH1750/SHT31/ADS1115/generic drivers |
| `src/i2c_display.cpp` | SSD1306 OLED driver + template engine + 5x7 font |

## Files Modified

- `include/devices.h` - New enum entries, struct fields, updated function signatures
- `src/devices.cpp` - I2C device kinds, read/write, init/deinit, persistence
- `src/nats_hal.cpp` - I2C HAL handler (scan, detect, read, write, recover)
- `src/nats_config.cpp` - I2C kind mapping, `ia`/`dt`/`rl`/`sc` field parsing
- `src/web_config.cpp` - I2C device form, scan endpoint, display endpoint
- `src/main.cpp` - `displayPoll()` in loop, `"i2c":true` in capabilities
- `cli/ionode` - `i2c` command routing and help
- `cli/lib/cmd_hardware.sh` - `cmd_i2c` function (scan, detect, read, write, recover)
- `cli/lib/cmd_config.sh` - `--i2c-addr`, `--channel`, `--template`, `--reg-len`, `--scale` flags

---

## Compatibility

All 4 targets build: ESP32-C6, ESP32-S3, ESP32-C3, classic ESP32. Flash usage well within 2MB budget (~16KB added).

No breaking changes. Existing `devices.json` files work unchanged - persistence uses string kind names, not enum values. New fields default safely (`i2c_addr=0`, `i2c_reg_len=1`, `i2c_scale=1.0`).
