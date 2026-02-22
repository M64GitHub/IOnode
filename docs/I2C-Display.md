# I2C Display (SSD1306 OLED)

IOnode can drive SSD1306 OLED displays over I2C. These tiny displays (128x64 or 128x32 pixels) cost a few dollars and show live sensor readings, system status, or any text you send. IOnode includes a built-in text renderer and a template system that auto-refreshes every 5 seconds with live data.

No libraries needed, no code to write. Wire it, register it, see data on screen.

---

## What You Need

- An ESP32 board running IOnode (any supported chip)
- An SSD1306 OLED display module (128x64 or 128x32, I2C version)
- 4 jumper wires (VCC, GND, SDA, SCL)
- The `ionode` CLI installed ([setup guide](SETUP.md))

> **Important:** Get the **I2C** version of the SSD1306 (4 pins: VCC, GND, SDA, SCL), not the SPI version (7 pins). The I2C modules are the most common ones sold on Amazon, AliExpress, etc.

---

## Wiring

Same as any I2C device - 4 wires to the same SDA/SCL pins used by sensors.

```
  ESP32                 SSD1306 OLED
  ─────                 ────────────
  3.3V  ──────────────  VCC
  GND   ──────────────  GND
  SDA   ──────────────  SDA
  SCL   ──────────────  SCL
```

SDA and SCL pins per chip:

| Chip | SDA Pin | SCL Pin |
|------|---------|---------|
| ESP32-C6 | GPIO 6 | GPIO 7 |
| ESP32-S3 | GPIO 8 | GPIO 9 |
| ESP32-C3 | GPIO 4 | GPIO 6 |
| ESP32 (classic) | GPIO 21 | GPIO 22 |

See the [I2C Sensors Guide](I2C-Sensors.md) for detailed wiring diagrams.

### SSD1306 I2C Address

Most SSD1306 modules use address **0x3C (decimal 60)**. Some use 0x3D (decimal 61). Run `ionode i2c <node> scan` to check.

---

## Quickstart: Hello World

The absolute minimum to get text on the screen.

### 1. Wire the display

Connect 3.3V, GND, SDA, and SCL as shown above.

### 2. Verify the connection

```bash
ionode i2c ionode-01 scan
```

You should see address `60` (or `61`) in the results.

### 3. Register the display

```bash
ionode device add ionode-01 display ssd1306 0 --i2c-addr 60
```

This registers a 128x64 display (pin `0` = 128x64) at address 60 named `display`.

### 4. Send text

```bash
ionode write ionode-01 display "!Hello World"
```

The `!` prefix means "send this text as-is" (raw mode, no template processing). You should see "Hello World" on the screen.

That's it. Four steps, three commands.

---

## Display Sizes

IOnode supports two common SSD1306 display sizes. The `pin` value when registering controls which size:

| Pin | Resolution | Lines | Chars per Line |
|-----|-----------|-------|----------------|
| 0 | 128x64 | 8 | 21 |
| 1 | 128x32 | 4 | 21 |

```bash
# 128x64 (default, most common)
ionode device add ionode-01 display ssd1306 0 --i2c-addr 60

# 128x32 (smaller module)
ionode device add ionode-01 display ssd1306 1 --i2c-addr 60
```

The built-in font is 5x7 pixels with 1 pixel spacing, giving 21 characters per line on both sizes.

---

## Sending Raw Text

Prefix your text with `!` to send it directly to the display without template processing:

```bash
ionode write ionode-01 display "!Line 1"
```

### Multi-line Text

Use `\n` for line breaks:

```bash
ionode write ionode-01 display "!Line 1\nLine 2\nLine 3"
```

Output on display:

```
Line 1
Line 2
Line 3
```

### Clear the Display

Send `0` to clear:

```bash
ionode write ionode-01 display 0
```

### Raw NATS

```bash
nats req ionode-01.hal.display.set "!Hello World"
nats req ionode-01.hal.display.set "0"
```

---

## Templates: Live Sensor Data

The real power of the display is templates. A template is a text string with `{tokens}` that get replaced with live sensor values every 5 seconds, automatically.

### Basic Template

First, register some sensors (see [I2C Sensors Guide](I2C-Sensors.md)):

```bash
ionode device add ionode-01 bme_temp i2c_bme280 --channel 0 --unit C --i2c-addr 118
ionode device add ionode-01 bme_humi i2c_bme280 --channel 1 --unit % --i2c-addr 118
ionode device add ionode-01 bme_pres i2c_bme280 --channel 2 --unit hPa --i2c-addr 118
```

Then register the display with a template:

```bash
ionode device add ionode-01 display ssd1306 0 --i2c-addr 60 \
  --template "T:{bme_temp}C H:{bme_humi}%\nP:{bme_pres}hPa"
```

The display shows:

```
T:23.5C H:45.2%
P:1013.2hPa
```

These values update every 5 seconds with fresh sensor readings. No polling, no scripts - IOnode handles it automatically.

### Template Tokens

Any registered device name can be used as a token. Plus these special tokens:

| Token | Value | Example |
|-------|-------|---------|
| `{device_name}` | Current reading of any registered sensor or actuator | `{bme_temp}` → `23.5` |
| `{ip}` | Node's IP address | `192.168.1.42` |
| `{heap}` | Free heap memory (KB) | `189` |
| `{uptime}` | Time since boot | `4h 37m` |
| `{name}` | Node's device name | `ionode-01` |

Unknown tokens display as `?token` so you can spot typos.

### Setting a Template at Registration

```bash
ionode device add ionode-01 display ssd1306 0 --i2c-addr 60 \
  --template "{name} {ip}\nT:{bme_temp}C H:{bme_humi}%\nP:{bme_pres}hPa\nHeap:{heap}KB Up:{uptime}"
```

Display shows:

```
ionode-01 192.168.1.42
T:23.5C H:45.2%
P:1013.2hPa
Heap:189KB Up:4h 37m
```

### Changing a Template Later

Send new text without the `!` prefix to update the template:

```bash
ionode write ionode-01 display "T:{bme_temp}C\nH:{bme_humi}%"
```

This replaces the template. The display starts refreshing with the new layout within 5 seconds.

### Raw NATS

```bash
# Set template (no ! prefix)
nats req ionode-01.hal.display.set "T:{bme_temp}C\nH:{bme_humi}%"

# Set raw text (! prefix, no interpolation)
nats req ionode-01.hal.display.set "!Static text here"

# Trigger template re-render
nats req ionode-01.hal.display.set "1"
```

---

## Multi-line Status Dashboard

Here's a practical 4-line template for a 128x32 display:

```bash
ionode device add ionode-01 screen ssd1306 1 --i2c-addr 60 \
  --template "{name}\n{ip}\nT:{bme_temp}C H:{bme_humi}%\nUp:{uptime} {heap}KB"
```

Display:

```
ionode-01
192.168.1.42
T:23.5C H:45.2%
Up:4h 37m 189KB
```

For a 128x64 display (8 lines), you have room for more:

```bash
ionode device add ionode-01 screen ssd1306 0 --i2c-addr 60 \
  --template "{name}\n{ip}\n\nTemp:  {bme_temp} C\nHumi:  {bme_humi} %\nPres:  {bme_pres} hPa\nLight: {light} lux\nUp:{uptime} {heap}KB"
```

Display:

```
ionode-01
192.168.1.42

Temp:  23.5 C
Humi:  45.2 %
Pres:  1013.2 hPa
Light: 342.5 lux
Up:4h 37m 189KB
```

---

## 128x32 Displays

Everything works the same way. Just use pin `1` instead of `0`:

```bash
ionode device add ionode-01 display ssd1306 1 --i2c-addr 60 --template "T:{bme_temp}C"
```

You get 4 lines instead of 8, 21 characters per line. Templates, raw text, and tokens all work identically.

---

## Full Worked Example: BME280 + OLED

A complete setup from scratch: a BME280 sensor feeding live data to an SSD1306 display, all on the same I2C bus.

### Wiring

```
  ESP32-C6                BME280         SSD1306 OLED
  ────────                ──────         ────────────
  3V3  ──────────┬──────  VCC     ┬────  VCC
  GND  ──────────┬──────  GND     ┬────  GND
  IO6 (SDA) ─────┬──────  SDA     ┬────  SDA
  IO7 (SCL) ─────┬──────  SCL     ┬────  SCL
```

All four lines are shared between the BME280 and the SSD1306. Two devices, same 4 wires.

### Scan

```bash
ionode i2c ionode-01 scan
```

Expected: addresses `60` (SSD1306) and `118` (BME280).

### Register Sensors

```bash
ionode device add ionode-01 bme_temp i2c_bme280 --channel 0 --unit C --i2c-addr 118
ionode device add ionode-01 bme_humi i2c_bme280 --channel 1 --unit % --i2c-addr 118
ionode device add ionode-01 bme_pres i2c_bme280 --channel 2 --unit hPa --i2c-addr 118
```

### Register Display with Template

```bash
ionode device add ionode-01 display ssd1306 0 --i2c-addr 60 \
  --template "T:{bme_temp}C H:{bme_humi}%\nP:{bme_pres}hPa\n\n{name} {ip}\nUp:{uptime}"
```

### Result

The display now shows live readings, updated every 5 seconds:

```
T:23.5C H:45.2%
P:1013.2hPa

ionode-01 192.168.1.42
Up:4h 37m
```

All values are also readable via CLI:

```bash
ionode read ionode-01 bme_temp    # → 23.5 C
ionode read ionode-01 bme_humi    # → 45.2 %
ionode read ionode-01 bme_pres    # → 1013.2 hPa
```

And available via NATS, web UI, dashboard, and threshold events.

---

## Web UI

The on-device web UI at `http://{device-ip}/` has display support in the Devices tab:

- SSD1306 devices show a text input with **Send** and **Clear** buttons
- Type text and click Send to update the display
- The template field is available when adding an SSD1306 device

The `/api/devices/display` REST endpoint provides programmatic access.

---

## Template Reference

| Feature | Syntax | Example |
|---------|--------|---------|
| Sensor value | `{sensor_name}` | `{bme_temp}` → `23.5` |
| Node IP | `{ip}` | `192.168.1.42` |
| Free heap (KB) | `{heap}` | `189` |
| Uptime | `{uptime}` | `4h 37m` |
| Node name | `{name}` | `ionode-01` |
| Line break | `\n` | new line on display |
| Raw text (no tokens) | `!` prefix | `!Hello` (literal) |
| Clear display | `0` | blank screen |
| Trigger re-render | `1` | re-applies current template |
| Max template length | 128 characters | including tokens before expansion |

---

## See Also

- [I2C Sensors Guide](I2C-Sensors.md) — Wiring, sensor setup, all supported I2C sensors
- [CLI Reference](CLI.md) — `ionode device add`, `ionode write`, `ionode i2c`
- [NATS API Reference](NATS-API.md) — I2C HAL subjects, device.add payload format
- [Release Notes](RELEASE-NOTES.md) — v0.3.0 I2C feature details
