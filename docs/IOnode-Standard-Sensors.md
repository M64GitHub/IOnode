# Standard Sensors: NTC, LDR, and Internal Temperature

IOnode includes built-in sensor types for the most common measurements: temperature (NTC thermistor), light level (LDR), and chip temperature. No external libraries, no I2C, no driver code — just connect and read.

This guide covers wiring, registration, and reading for each sensor type. For I2C sensors (BME280, BH1750, SHT31, ADS1115), see the [I2C Sensors Guide](I2C-Sensors.md).

---

## What You Need

- An ESP32 board running IOnode (any supported chip)
- The `ionode` CLI installed ([setup guide](SETUP.md))
- For NTC/LDR: a 10K resistor, the sensor, and 3 jumper wires

---

## Internal Temperature (`internal_temp`)

The easiest sensor — zero wiring, already registered on every IOnode.

### Read It

```bash
ionode read ionode-01 chip_temp
```

```
  chip_temp  38.1 C
```

That's it. `chip_temp` is pre-registered on every IOnode at boot. It reads the ESP32's on-die temperature sensor.

### What It Measures

This is the chip's internal die temperature, not ambient temperature. It's typically 5–15°C above room temperature depending on workload and WiFi activity. Useful for:

- Board health monitoring
- Detecting overheating in enclosed installations
- Threshold alerts when the chip runs hot

### Chip Support

| Chip | Supported |
|------|-----------|
| ESP32-C6 | Yes |
| ESP32-S3 | Yes |
| ESP32-C3 | Yes |
| ESP32 (classic) | No (returns `unsupported`) |

The classic ESP32 lacks the on-die temperature sensor API used by IOnode.

### Threshold Alert Example

Get notified when the chip exceeds 60°C:

```bash
ionode event set ionode-01 chip_temp --above 60 --cooldown 30
```

Monitor events:

```bash
ionode watch --events
```

### Raw NATS

```bash
nats req ionode-01.hal.chip_temp ""
nats req ionode-01.hal.system.temperature ""
```

Both return the same value. `chip_temp` goes through the device registry (with history); `system.temperature` is the raw HAL endpoint.

---

## NTC 10K Thermistor (`ntc_10k`)

The most popular external temperature sensor. Cheap (~$0.50), accurate, and needs just one resistor and two wires beyond power.

### What It Is

An NTC (Negative Temperature Coefficient) thermistor is a resistor whose resistance decreases as temperature increases. A 10K NTC has 10,000 ohms at 25°C. IOnode reads the voltage across a voltage divider, calculates the resistance, and converts to temperature using the Steinhart-Hart equation.

### Quickstart

**1. Wire it:**

```
3.3V ─┐
      |
  [10K fixed]
      |
ADC ──┤── GPIO 2
      |
  [NTC 10K]
      |
GND ──┘
```

**2. Register:**

```bash
ionode device add ionode-01 temp ntc_10k 2 --unit C
```

**3. Read:**

```bash
ionode read ionode-01 temp
```

```
  temp  23.5 C
```

Three steps. Your thermistor is now a named sensor with history, events, and web UI support.

### Wiring

The NTC and a fixed 10K resistor form a voltage divider. IOnode supports both orientations:

```
NTC on GND side              NTC on 3.3V side
(default)                    (inverted=true)

3.3V ─┐                     3.3V ─┐
      |                           |
  [10K fixed]                 [NTC 10K]
      |                           |
ADC ──┤                     ADC ──┤
      |                           |
  [NTC 10K]                  [10K fixed]
      |                           |
GND ──┘                     GND ──┘
```

Both work equally well. The default orientation (NTC on GND side) is shown on the left. If your NTC is on the 3.3V side, use `--inverted`:

```bash
ionode device add ionode-01 temp ntc_10k 2 --unit C --inverted
```

### How It Works

IOnode reads the ADC voltage and converts to temperature:

```
1. Read:   mV = average of 16 × analogReadMilliVolts(pin)
2. Ratio:  ratio = mV / 3300.0
3. R_ntc:  R = 10000 × ratio / (1 - ratio)           [default]
           R = 10000 × (1 - ratio) / ratio           [inverted]
4. Temp:   T(K) = 1 / (1/298.15 + (1/3950) × ln(R / 10000))
5. Result: T(°C) = T(K) - 273.15
```

Constants:
- Beta coefficient: **3950**
- Reference resistance: **10KΩ at 25°C** (298.15K)
- Fixed resistor: **10KΩ**
- ADC reference: **3300mV** (3.3V)

### ADC Warmup

The ESP32's SAR ADC reads ~60mV high after being idle for more than 1 second. IOnode handles this automatically:

1. **Warmup burst** — 16 rapid reads to wake the ADC
2. **300ms settle** — wait for the ADC to stabilize
3. **Real read** — 16-sample average for noise reduction

This happens on every NTC poll cycle. You don't need to do anything — readings are accurate out of the box.

### Polling and History

- **Polled every 5 seconds** — the NTC value updates frequently for responsive readings
- **History recorded every 5 minutes** — 6-sample ring buffer for sparkline display
- **EMA-smoothed** — readings are stable, not jumpy

### Accuracy

±0.5°C typical in the 0–50°C range. Outside this range, accuracy decreases due to the thermistor's non-linear response, but the Steinhart-Hart equation provides reasonable results from roughly -20°C to 100°C.

### Threshold Event Example

Alert when temperature exceeds 28°C:

```bash
ionode event set ionode-01 temp --above 28 --cooldown 30
```

### Raw NATS

```bash
# Register
nats req ionode-01.config.device.add '{"n":"temp","k":"ntc_10k","p":2,"u":"C"}'

# Register (inverted)
nats req ionode-01.config.device.add '{"n":"temp","k":"ntc_10k","p":2,"u":"C","i":true}'

# Read
nats req ionode-01.hal.temp ""
```

---

## LDR (Light Dependent Resistor)

An LDR's resistance decreases with more light. IOnode reads it as a percentage (0–100%).

### What It Is

A Light Dependent Resistor (also called a photoresistor) changes resistance based on light intensity. In bright light, resistance drops to a few hundred ohms. In darkness, it rises to several hundred kilohms. Paired with a fixed resistor in a voltage divider, IOnode converts the ADC reading to a 0–100% light level.

### Quickstart

**1. Wire it:**

```
3.3V ─┐
      |
  [10K fixed]
      |
ADC ──┤── GPIO 3
      |
    [LDR]
      |
GND ──┘
```

**2. Register:**

```bash
ionode device add ionode-01 light ldr 3 --unit %
```

**3. Read:**

```bash
ionode read ionode-01 light
```

```
  light  72.3 %
```

### Wiring

Like the NTC, the LDR forms a voltage divider with a 10K fixed resistor. Both orientations are supported:

```
LDR on GND side              LDR on 3.3V side
(default)                    (inverted=true)

3.3V ─┐                     3.3V ─┐
      |                           |
  [10K fixed]                   [LDR]
      |                           |
ADC ──┤                     ADC ──┤
      |                           |
    [LDR]                    [10K fixed]
      |                           |
GND ──┘                     GND ──┘
```

**Default (LDR on GND side):** more light = higher ADC voltage = higher percentage.

**Inverted (LDR on 3.3V side):** more light = lower ADC voltage. Use `--inverted` to flip the output so that more light still reads as a higher percentage:

```bash
ionode device add ionode-01 light ldr 3 --unit % --inverted
```

### How It Works

```
1. Read:   mV = average of 16 × analogReadMilliVolts(pin)
2. Percent: pct = mV × 100 / 3300
3. Result:  result = inverted ? (100 - pct) : pct
```

The output is a simple percentage — not lux. For calibrated lux readings, use an [I2C BH1750 sensor](I2C-Sensors.md) instead.

### Threshold Event Example

Alert when light drops below 20% (lights turned off):

```bash
ionode event set ionode-01 light --below 20 --cooldown 10
```

### Raw NATS

```bash
# Register
nats req ionode-01.config.device.add '{"n":"light","k":"ldr","p":3,"u":"%"}'

# Read
nats req ionode-01.hal.light ""
```

---

## EMA Smoothing and History

All registered sensors automatically get:

- **EMA smoothing** — Exponential Moving Average for stable readings without sudden jumps
- **6-sample history** — ring buffer recorded every 5 minutes, displayed as a sparkline in the web UI
- **NTC special case** — polled every 5 seconds for responsive temperature readings; other sensors are read on demand and recorded every 5 minutes

The history and smoothing happen in the background with no configuration needed.

---

## Combining Sensors and Actuators

Here's a full worked example: an NTC temperature sensor and a relay-controlled fan.

### Wire Both

```
3.3V ─┐                     ESP32           Relay Module
      |                     ─────           ────────────
  [10K fixed]               3.3V ──────── VCC
      |                     GND  ──────── GND
ADC ──┤── GPIO 2            GPIO 16 ───── IN
      |
  [NTC 10K]
      |
GND ──┘
```

### Register Both

```bash
ionode device add ionode-01 temp ntc_10k 2 --unit C
ionode device add ionode-01 fan relay 16 --inverted
```

### Set a Temperature Alert

```bash
ionode event set ionode-01 temp --above 28 --cooldown 60
```

### Automate with a Script

IOnode fires NATS events — your automation logic lives outside the device. A simple bash script:

```bash
#!/bin/bash
# Turn on fan when temperature event fires
nats sub 'ionode-01.events.temp' | while read -r line; do
    nats req ionode-01.hal.fan.set "1" >/dev/null
    echo "Fan ON — temperature threshold exceeded"
done
```

Or use [OpenClaw](https://github.com/openclaw/openclaw) for natural language automation:

```
"When ionode-01 temperature exceeds 28°C, turn on the fan"
```

---

## Web UI

The on-device web UI at `http://{device-ip}/` supports all standard sensor types in the Devices tab:

- **Add Device** form includes `ntc_10k`, `ldr`, `digital_in`, `analog_in`, and all actuator types
- Live sensor readings update in real time
- NTC and LDR show current values with units
- `chip_temp` appears automatically (pre-registered)

---

## Sensor Quick Reference

| Kind | What It Measures | Pin | Unit | Samples | Inverted |
|------|-----------------|-----|------|---------|----------|
| `internal_temp` | Chip die temperature | — (virtual) | C | — | No |
| `ntc_10k` | External temperature (thermistor) | ADC GPIO | C | 16 avg | Optional |
| `ldr` | Light level (photoresistor) | ADC GPIO | % | 16 avg | Optional |
| `analog_in` | Raw ADC value | ADC GPIO | — | 1 | No |
| `digital_in` | Digital pin state | GPIO | — | 1 | No |

### Constants

| Parameter | Value |
|-----------|-------|
| ADC resolution | 12-bit (0–4095) |
| ADC reference voltage | 3300mV (3.3V) |
| NTC beta coefficient | 3950 |
| NTC reference | 10KΩ at 25°C (298.15K) |
| NTC warmup | 16 burst reads + 300ms settle |
| NTC poll interval | Every 5 seconds |
| History interval | Every 5 minutes |
| History depth | 6 samples |

---

## See Also

- [GPIO & Actuators Guide](GPIO.md) — Raw GPIO, ADC, PWM, relays, digital I/O
- [I2C Sensors Guide](I2C-Sensors.md) — BME280, BH1750, SHT31, ADS1115
- [I2C Display Guide](I2C-Display.md) — SSD1306 OLED display
- [CLI Reference](CLI.md) — All CLI commands including `ionode device add` and `ionode event set`
- [NATS API Reference](NATS-API.md) — Every NATS subject, payload, and response
