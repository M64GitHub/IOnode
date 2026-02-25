# DHT Temperature & Humidity Sensors

IOnode supports DHT11 and DHT22 temperature/humidity sensors out of the box. These are cheap, widely available single-wire sensors that connect to any GPIO pin. IOnode includes a built-in bit-bang driver - no libraries to install, no code to write.

The **AM2303** is a DHT22 in a wired package (3 wires instead of 4 pins). It uses the exact same protocol and is registered the same way as a DHT22.

This guide covers wiring, setup, and reading your first DHT sensor.

---

## What You Need

- An ESP32 board running IOnode (any supported chip)
- A DHT sensor: DHT11, DHT22, or AM2303
- Jumper wires
- A 10kΩ pull-up resistor (recommended, see wiring section)
- The `ionode` CLI installed ([setup guide](SETUP.md))

---

## Supported Sensors

| Sensor | Device Kinds | Temp Range | Humidity Range | Resolution | Accuracy |
|--------|-------------|------------|---------------|------------|----------|
| DHT11 | `dht11_temp`, `dht11_humi` | 0-50°C | 20-80% RH | 1°C / 1% | ±2°C / ±5% |
| DHT22 | `dht22_temp`, `dht22_humi` | -40-80°C | 0-100% RH | 0.1°C / 0.1% | ±0.5°C / ±2% |
| AM2303 | `dht22_temp`, `dht22_humi` | -40-80°C | 0-100% RH | 0.1°C / 0.1% | ±0.5°C / ±2% |

The AM2303 is electrically identical to the DHT22. Use `dht22_temp` and `dht22_humi` device kinds for both.

---

## Wiring

DHT sensors use a single-wire protocol on any GPIO pin. Unlike I2C, there are no fixed pin assignments - you choose the GPIO pin when you register the device.

### Pull-Up Resistor

A **10kΩ resistor between VCC (3.3V) and the DATA line** is recommended for reliable readings. IOnode enables the ESP32's internal pull-up (~45kΩ) as a fallback, but an external 10kΩ gives much better signal integrity, especially with longer wires.

> **Important:** DHT sensors run at **3.3V** on ESP32 boards. Do not connect VCC to 5V unless your breakout board has a voltage regulator.

### DHT22 / DHT11 (4-pin package)

Most DHT22 and DHT11 sensors come as a 4-pin package. Pin 3 (NC) is not connected.

```
  ESP32                 DHT22 / DHT11
  ─────                 ─────────────
                        Pin 1 (VCC) ─── 3.3V
  GPIO ────┬─────────── Pin 2 (DATA)
           │
          10kΩ
           │
          3.3V          Pin 3 (NC)  ─── not connected
                        Pin 4 (GND) ─── GND
```

Facing the sensor with the grid side towards you, pins are numbered left to right: 1 (VCC), 2 (DATA), 3 (NC), 4 (GND).

### AM2303 (3-wire package)

The AM2303 comes with 3 wires already attached. No NC pin.

```
  ESP32                 AM2303
  ─────                 ──────
  3.3V ────┬─────────── Red wire    (VCC)
           │
          10kΩ
           │
  GPIO ────┴─────────── Yellow wire (DATA)
  GND  ──────────────── Black wire  (GND)
```

### DHT Module Breakout Boards

Many DHT modules come on a small PCB with a built-in 10kΩ pull-up resistor. These have 3 pins labeled VCC, DATA (or OUT/S), and GND. If your module has a built-in pull-up, you don't need an external resistor - just connect the 3 wires directly.

```
  ESP32                 DHT Module (3-pin breakout)
  ─────                 ───────────────────────────
  3.3V ──────────────── VCC (or +)
  GPIO ──────────────── DATA (or OUT / S)
  GND  ──────────────── GND (or -)
```

---

## Quickstart: DHT22 / AM2303

This walks through setting up a DHT22 or AM2303 to read temperature and humidity.

### 1. Wire it

Connect the sensor to any available GPIO pin (example uses GPIO 4). Add a 10kΩ pull-up resistor between 3.3V and the DATA line (or skip if your module has one built in).

### 2. Register temperature and humidity

```bash
ionode device add ionode-01 room_temp dht22_temp 4 --unit C
ionode device add ionode-01 room_humi dht22_humi 4 --unit %
```

Both devices use the **same GPIO pin** (4). The device kind (`dht22_temp` vs `dht22_humi`) determines which value is returned.

> **AM2303 users:** Use `dht22_temp` and `dht22_humi` - the AM2303 is a DHT22 in a different package. There is no separate `am2303` device kind.

### 3. Read

```bash
ionode read ionode-01 room_temp
ionode read ionode-01 room_humi
```

```
  room_temp  23.5 C
  room_humi  45.2 %
```

That's it. Your DHT sensor is now:

- Readable via CLI, NATS, and the web UI
- Polled every 5 minutes for history (sparkline)
- EMA-smoothed for stable readings
- Ready for threshold events

> **Efficiency note:** IOnode reads both temperature and humidity from the sensor in a single physical read and caches the result for 2 seconds. Reading `room_temp` and `room_humi` in quick succession does NOT cause two separate sensor reads.

---

## DHT11

The DHT11 is a cheaper, lower-accuracy alternative to the DHT22. Setup is identical - just use `dht11_temp` and `dht11_humi` device kinds instead.

```bash
ionode device add ionode-01 room_temp dht11_temp 4 --unit C
ionode device add ionode-01 room_humi dht11_humi 4 --unit %
```

The DHT11 returns integer values (no decimal places) and has a narrower range (0-50°C, 20-80% RH). For most applications, the DHT22 or AM2303 is a better choice.

---

## Threshold Events

Set up alerts when values cross a threshold:

```bash
ionode event set ionode-01 room_temp --above 30 --cooldown 60
```

When temperature exceeds 30°C, IOnode publishes a NATS event to `ionode-01.events.room_temp`. The cooldown prevents repeated alerts for 60 seconds.

Monitor events live:

```bash
ionode watch --events
```

---

## Web UI

The on-device web UI at `http://{device-ip}/` supports DHT sensors in the Devices tab:

- **Add Device** form includes all DHT device types (`dht11_temp`, `dht11_humi`, `dht22_temp`, `dht22_humi`)
- **AM2303 users:** select `dht22_temp` or `dht22_humi` from the dropdown - there is no separate AM2303 entry
- **Pin** field is required - enter the GPIO pin number
- Default units are assigned automatically ("C" for temperature, "%" for humidity)

---

## Raw NATS

For DHT22 and AM2303 (both use `dht22_temp` / `dht22_humi`):

```bash
# Register
nats req ionode-01.config.device.add '{"n":"room_temp","k":"dht22_temp","p":4,"u":"C"}'
nats req ionode-01.config.device.add '{"n":"room_humi","k":"dht22_humi","p":4,"u":"%"}'

# Read
nats req ionode-01.hal.room_temp ""
nats req ionode-01.hal.room_humi ""
```

---

## See Also

- [Standard Sensors Guide](IOnode-Standard-Sensors.md) - NTC thermistor, LDR, internal temperature
- [I2C Sensors Guide](I2C-Sensors.md) - BME280, BH1750, SHT31, ADS1115 setup
- [I2C Display Guide](I2C-Display.md) - SSD1306 OLED display setup and templates
- [CLI Reference](CLI.md) - All CLI commands including `ionode device add`
- [NATS API Reference](NATS-API.md) - HAL subjects and device.add payload format
