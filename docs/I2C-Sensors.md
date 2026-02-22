# I2C Sensors

IOnode supports I2C sensors out of the box. I2C is a two-wire bus that lets you connect multiple sensors to the same two pins. Sensors like the BME280 (temperature, humidity, pressure), BH1750 (light), SHT31 (temperature, humidity), and ADS1115 (16-bit ADC) are cheap, accurate, and widely available as breakout boards. IOnode includes built-in drivers for all of them - no libraries to install, no code to write.

This guide walks you through wiring, setup, and reading your first I2C sensor. You'll be reading live values in under 5 minutes.

---

## What You Need

- An ESP32 board running IOnode (any supported chip)
- An I2C sensor breakout board (BH1750, BME280, SHT31, or ADS1115)
- 4 jumper wires (VCC, GND, SDA, SCL)
- The `ionode` CLI installed ([setup guide](SETUP.md))

That's it. Most I2C breakout boards include pull-up resistors on the SDA and SCL lines, so you don't need external pull-ups.

> **Important:** I2C sensors run at **3.3V**. Do not connect VCC to 5V unless your breakout board has a voltage regulator (most do - check the markings).

---

## Wiring

Every I2C sensor connects the same way: 4 wires.

```
  ESP32                 Sensor Breakout
  ─────                 ───────────────
  3.3V  ──────────────  VCC (or VIN)
  GND   ──────────────  GND
  SDA   ──────────────  SDA
  SCL   ──────────────  SCL
```

The SDA and SCL pins depend on your ESP32 chip:

| Chip | SDA Pin | SCL Pin |
|------|---------|---------|
| ESP32-C6 | GPIO 6 | GPIO 7 |
| ESP32-S3 | GPIO 8 | GPIO 9 |
| ESP32-C3 | GPIO 4 | GPIO 6 |
| ESP32 (classic) | GPIO 21 | GPIO 22 |

These pins are fixed per chip variant. You don't need to configure them - IOnode detects your chip and uses the right pins automatically.

### Wiring Example: ESP32-C6 + BH1750

```
  ESP32-C6                  BH1750 Breakout
  ────────                  ───────────────
  3V3  (pin 3V3) ────────  VCC
  GND  (pin GND) ────────  GND
  IO6  (pin 6)   ────────  SDA
  IO7  (pin 7)   ────────  SCL
                            ADDR → leave unconnected (default 0x23)
```

---

## Quickstart: BH1750 Light Sensor

The BH1750 is the simplest I2C sensor - one value (lux), one address, no channels. This is the fastest way to get an I2C sensor working.

### 1. Wire it

Connect 4 wires as shown above (3.3V, GND, SDA, SCL).

### 2. Scan the bus

Verify IOnode can see the sensor:

```bash
ionode i2c ionode-01 scan
```

You should see address `35` (hex 0x23) in the results. If you don't, check your wiring.

### 3. Register the sensor

```bash
ionode device add ionode-01 light i2c_bh1750 --unit lux --i2c-addr 35
```

This tells IOnode: "There's a BH1750 at address 35 called `light`, measured in lux."

### 4. Read it

```bash
ionode read ionode-01 light
```

Output:

```
  light  342.5 lux
```

That's it. Three commands. Your light sensor is now:

- Readable via CLI, NATS, and the web UI
- Polled every 5 minutes for history (sparkline)
- EMA-smoothed for stable readings
- Ready for threshold events (`ionode event set ionode-01 light --below 50`)

---

## BME280: Temperature, Humidity, and Pressure

The BME280 is the most popular I2C environmental sensor. It measures three things at once: temperature, humidity, and barometric pressure.

### I2C Address

| ADDR Pin | Address (hex) | Address (decimal) |
|----------|--------------|-------------------|
| Not connected / GND | 0x76 | 118 |
| Connected to VCC | 0x77 | 119 |

Most breakout boards default to **0x76 (decimal 118)**.

### Channels

The BME280 returns 3 values. In IOnode, you register one device per value using the `--channel` flag:

| Channel | Value | Typical Unit |
|---------|-------|-------------|
| 0 | Temperature | C |
| 1 | Humidity | % |
| 2 | Pressure | hPa |

### Wiring

```
  ESP32-C6                  BME280 Breakout
  ────────                  ───────────────
  3V3  ──────────────────── VCC (or VIN)
  GND  ──────────────────── GND
  IO6  ──────────────────── SDA (or SDI)
  IO7  ──────────────────── SCL (or SCK)
                             CSB → leave unconnected
                             SDO → GND for 0x76, VCC for 0x77
```

### Register All Three Channels

```bash
ionode device add ionode-01 bme_temp i2c_bme280 --channel 0 --unit C --i2c-addr 118
ionode device add ionode-01 bme_humi i2c_bme280 --channel 1 --unit % --i2c-addr 118
ionode device add ionode-01 bme_pres i2c_bme280 --channel 2 --unit hPa --i2c-addr 118
```

You can register just the channels you need. If you only care about temperature, register only channel 0.

### Read

```bash
ionode read ionode-01 bme_temp
ionode read ionode-01 bme_humi
ionode read ionode-01 bme_pres
```

```
  bme_temp  23.5 C
  bme_humi  45.2 %
  bme_pres  1013.2 hPa
```

> **Efficiency note:** IOnode reads all 3 values from the BME280 in a single I2C transaction and caches the result for 1 second. Reading `bme_temp`, `bme_humi`, and `bme_pres` in quick succession does NOT cause 3 separate sensor reads.

### Add a Temperature Alert

```bash
ionode event set ionode-01 bme_temp --above 30 --cooldown 60
```

When the temperature exceeds 30°C, IOnode publishes a NATS event to `ionode-01.events.bme_temp`. The cooldown prevents repeated alerts for 60 seconds.

Monitor events live:

```bash
ionode watch --events
```

### Raw NATS Equivalent

```bash
# Register
nats req ionode-01.config.device.add '{"n":"bme_temp","k":"i2c_bme280","p":0,"u":"C","ia":118}'

# Read
nats req ionode-01.hal.bme_temp ""
```

---

## SHT31: Temperature and Humidity

The SHT31 is a high-accuracy temperature and humidity sensor. Similar to the BME280 but without pressure, and slightly more accurate for humidity.

### I2C Address

| ADDR Pin | Address (hex) | Address (decimal) |
|----------|--------------|-------------------|
| Not connected / GND | 0x44 | 68 |
| Connected to VCC | 0x45 | 69 |

### Channels

| Channel | Value | Typical Unit |
|---------|-------|-------------|
| 0 | Temperature | C |
| 1 | Humidity | % |

### Register

```bash
ionode device add ionode-01 sht_temp i2c_sht31 --channel 0 --unit C --i2c-addr 68
ionode device add ionode-01 sht_humi i2c_sht31 --channel 1 --unit % --i2c-addr 68
```

### Read

```bash
ionode read ionode-01 sht_temp
ionode read ionode-01 sht_humi
```

---

## ADS1115: 16-bit ADC

The ADS1115 is a high-precision analog-to-digital converter with 4 input channels. Use it when the ESP32's built-in 12-bit ADC isn't accurate enough, or when you need more analog inputs.

### I2C Address

The ADS1115 address is set by connecting the ADDR pin:

| ADDR Pin | Address (hex) | Address (decimal) |
|----------|--------------|-------------------|
| GND | 0x48 | 72 |
| VCC | 0x49 | 73 |
| SDA | 0x4A | 74 |
| SCL | 0x4B | 75 |

Default (ADDR to GND): **0x48 (decimal 72)**.

### Channels

| Channel | Input Pin | Description |
|---------|-----------|-------------|
| 0 | AIN0 | Analog input 0 |
| 1 | AIN1 | Analog input 1 |
| 2 | AIN2 | Analog input 2 |
| 3 | AIN3 | Analog input 3 |

Each channel reads single-ended voltage relative to ground. The default range is ±4.096V with 0.125mV resolution (16-bit).

### Wiring Example: Soil Moisture Sensor

```
  ESP32-C6                ADS1115              Soil Sensor
  ────────                ───────              ───────────
  3V3  ─────────────────  VDD                  VCC ← 3V3
  GND  ─────────────────  GND                  GND ← GND
  IO6  ─────────────────  SDA                  AOUT ──── AIN0
  IO7  ─────────────────  SCL
                          ADDR ── GND
```

### Register

```bash
ionode device add ionode-01 soil i2c_ads1115 --channel 0 --unit mV --i2c-addr 72
```

### Read

```bash
ionode read ionode-01 soil
```

```
  soil  1247.5 mV
```

The value is in millivolts (0.125mV per LSB). For 4 channels:

```bash
ionode device add ionode-01 ain0 i2c_ads1115 --channel 0 --unit mV --i2c-addr 72
ionode device add ionode-01 ain1 i2c_ads1115 --channel 1 --unit mV --i2c-addr 72
ionode device add ionode-01 ain2 i2c_ads1115 --channel 2 --unit mV --i2c-addr 72
ionode device add ionode-01 ain3 i2c_ads1115 --channel 3 --unit mV --i2c-addr 72
```

---

## i2c_generic: Any I2C Sensor

For I2C sensors that don't have a dedicated driver, the `i2c_generic` kind lets you read any register and apply a scale factor. This works with any sensor that exposes data via a simple register read.

### How It Works

- **`--i2c-addr`** - the sensor's I2C address (decimal)
- **pin** - the register address to read (0-255)
- **`--reg-len`** - how many bytes to read: 1 or 2 (default: 1)
- **`--scale`** - multiply the raw value by this number (default: 1.0)

IOnode reads the register, combines the bytes as a big-endian unsigned integer, and multiplies by the scale factor.

### Example: LM75 Temperature Sensor

The LM75 stores temperature in register 0x00 as a 16-bit value with 0.0625°C resolution (only the upper 11 bits are significant, but reading 2 bytes and scaling works for a rough reading).

| Property | Value |
|----------|-------|
| Address | 0x48 (decimal 72) |
| Register | 0x00 |
| Bytes | 2 |
| Scale | 0.00390625 (1/256, since MSB is integer part) |

```bash
ionode device add ionode-01 lm75_temp i2c_generic 0 --unit C --i2c-addr 72 --reg-len 2 --scale 0.00390625
```

```bash
ionode read ionode-01 lm75_temp
```

### Raw NATS

```bash
nats req ionode-01.config.device.add '{"n":"lm75_temp","k":"i2c_generic","p":0,"u":"C","ia":72,"rl":2,"sc":0.00390625}'
```

---

## Multiple Sensors on One Bus

I2C supports multiple devices on the same two wires. Each device has a unique address. You can connect several sensors simultaneously - just wire them all to the same SDA and SCL pins.

```
  ESP32-C6
  ────────
  3V3  ──┬───── BME280 VCC
         ├───── BH1750 VCC
         └───── SSD1306 VCC
  GND  ──┬───── BME280 GND
         ├───── BH1750 GND
         └───── SSD1306 GND
  IO6  ──┬───── BME280 SDA
         ├───── BH1750 SDA
         └───── SSD1306 SDA
  IO7  ──┬───── BME280 SCL
         ├───── BH1750 SCL
         └───── SSD1306 SCL
```

### Address Reference

| Sensor | Default Address | Alternate Address |
|--------|----------------|-------------------|
| BME280 | 0x76 (118) | 0x77 (119) |
| BH1750 | 0x23 (35) | 0x5C (92) |
| SHT31 | 0x44 (68) | 0x45 (69) |
| ADS1115 | 0x48 (72) | 0x49 (73), 0x4A (74), 0x4B (75) |
| SSD1306 | 0x3C (60) | 0x3D (61) |

All default addresses are unique, so you can connect one of each without any configuration.

### Two BME280s

Need two BME280s? Set the ADDR pin differently on each module:

```bash
# First BME280 - SDO pin to GND → address 0x76
ionode device add ionode-01 indoor_temp i2c_bme280 --channel 0 --unit C --i2c-addr 118

# Second BME280 - SDO pin to VCC → address 0x77
ionode device add ionode-01 outdoor_temp i2c_bme280 --channel 0 --unit C --i2c-addr 119
```

Use `ionode i2c ionode-01 scan` to verify both are detected.

---

## Raw I2C Bus Access

IOnode provides raw I2C commands for debugging and working with unsupported devices. These work even without any registered I2C sensors.

### Scan

Find all devices on the bus:

```bash
ionode i2c ionode-01 scan
```

### Detect

Check if a specific address responds:

```bash
ionode i2c ionode-01 detect 118
```

### Read Register

Read bytes from a register:

```bash
ionode i2c ionode-01 read 118 --reg 0xD0 --len 1
```

This reads 1 byte from register 0xD0 of device at address 118 (BME280 chip ID - should return `[96]` which is 0x60).

### Write Register

Write bytes to a register:

```bash
ionode i2c ionode-01 write 118 --reg 0xF2 --data 1
```

### Bus Recovery

If the I2C bus gets stuck (a sensor holds SDA low), run:

```bash
ionode i2c ionode-01 recover
```

This toggles SCL 9 times to release any stuck device.

### Raw NATS

```bash
nats req ionode-01.hal.i2c.scan ""
nats req ionode-01.hal.i2c.118.detect ""
nats req ionode-01.hal.i2c.118.read '{"reg":208,"len":1}'
nats req ionode-01.hal.i2c.118.write '{"reg":242,"data":[1]}'
nats req ionode-01.hal.i2c.recover ""
```

Note: addresses in NATS subjects are always decimal.

---

## Web UI

The on-device web UI at `http://{device-ip}/` supports I2C sensors in the Devices tab:

- **Add Device** form includes all I2C device types (i2c_bme280, i2c_bh1750, i2c_sht31, i2c_ads1115, i2c_generic)
- **I2C Address** field appears when an I2C kind is selected
- **Channel** hints show which channel maps to which value (e.g., "0=temp, 1=humi, 2=pres" for BME280)
- **Scan** button scans the I2C bus and shows detected addresses

I2C sensors show up in the device list and display live readings just like any other sensor.

---

## Supported Sensors Quick Reference

| Kind | Sensor | Channels | Default Address | What It Measures |
|------|--------|----------|----------------|------------------|
| `i2c_bh1750` | BH1750 | 1 | 0x23 (35) | Ambient light (lux) |
| `i2c_bme280` | BME280 | 3 | 0x76 (118) | Temperature, humidity, pressure |
| `i2c_sht31` | SHT31 | 2 | 0x44 (68) | Temperature, humidity |
| `i2c_ads1115` | ADS1115 | 4 | 0x48 (72) | 16-bit ADC voltage (mV) |
| `i2c_generic` | Any | 1 | configurable | Raw register read with scale |

---

## See Also

- [I2C Display Guide](I2C-Display.md) - SSD1306 OLED display setup and templates
- [CLI Reference](CLI.md) - All CLI commands including `ionode i2c` and `ionode device add`
- [NATS API Reference](NATS-API.md) - I2C HAL subjects and device.add payload format
- [Release Notes](RELEASE-NOTES.md) - v0.3.0 I2C feature details
