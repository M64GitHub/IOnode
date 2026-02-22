# GPIO, ADC, PWM, Relays, and Digital I/O

IOnode exposes every GPIO pin over NATS. No device registration needed for raw access - just point at a pin and read or write it. For persistent, named devices (relay, PWM output, digital input), register them and get history, events, web UI controls, and state persistence for free.

This guide covers raw hardware access and registered devices for digital I/O, analog input, PWM output, relays, and RGB LEDs.

---

## What You Need

- An ESP32 board running IOnode (any supported chip)
- The `ionode` CLI installed ([setup guide](SETUP.md))
- Components to connect (LEDs, buttons, relays, motors - whatever you're building)

> **Important:** ESP32 GPIOs are **3.3V**. Do not connect 5V signals directly to any pin. Use a level shifter or voltage divider if interfacing with 5V logic.

---

## Raw GPIO (No Registration Needed)

The fastest way to interact with hardware. Zero setup - every GPIO pin is accessible immediately.

### Read a pin

```bash
ionode gpio ionode-01 4 get
```

```
  GPIO 4  0  (LOW)
```

### Set a pin

```bash
ionode gpio ionode-01 8 set 1
```

```
  GPIO 8  ← 1  (HIGH)
```

That's it. Two commands, no registration.

- **get** reads the pin's current digital value (0 or 1)
- **set** configures the pin as OUTPUT and writes HIGH or LOW

### Raw NATS

```bash
nats req ionode-01.hal.gpio.4.get ""
nats req ionode-01.hal.gpio.8.set "1"
```

---

## Raw ADC

Read any analog-capable GPIO as a 12-bit value (0–4095).

```bash
ionode adc ionode-01 2
```

```
  ADC 2  1847  ████████░░░░░░░░░░░░  45%
```

The CLI shows a bar graph and percentage automatically. Which pins support ADC depends on your chip - most GPIOs with analog capability work. Check your ESP32 variant's datasheet for ADC-capable pins.

### Example: Read a Potentiometer

```
3.3V ─┐
      |
  [Potentiometer]
      |
ADC ──┤── GPIO 2
      |
GND ──┘
```

```bash
ionode adc ionode-01 2
```

Turn the pot and read again - the value changes from 0 (GND) to 4095 (3.3V).

### Raw NATS

```bash
nats req ionode-01.hal.adc.2.read ""
```

---

## Raw PWM

Set any GPIO to an 8-bit PWM output (0–255).

```bash
ionode pwm ionode-01 9 set 128
```

```
  PWM 9  ← 128/255  (50%)
```

```bash
ionode pwm ionode-01 9 get
```

```
  PWM 9  128/255  (50%)
```

### Example: Dim an LED

```
GPIO 9 ──[330Ω]──[LED]── GND
```

```bash
ionode pwm ionode-01 9 set 0      # off
ionode pwm ionode-01 9 set 64     # 25% brightness
ionode pwm ionode-01 9 set 255    # full brightness
```

### Raw NATS

```bash
nats req ionode-01.hal.pwm.9.set "128"
nats req ionode-01.hal.pwm.9.get ""
```

---

## Registering a Digital Input Sensor

Raw GPIO works, but registering a device gives you a named sensor that's queryable, has history, supports threshold events, and shows up in the web UI.

### Why register?

| Raw GPIO | Registered Device |
|----------|-------------------|
| Read by pin number | Read by name |
| No history | 6-sample sparkline history |
| No events | Threshold events with cooldown |
| Not in web UI device list | Full web UI integration |
| Not in discovery | Listed in discovery + device.list |

### Register

```bash
ionode device add ionode-01 door digital_in 7
```

### Wiring: Button / Switch

```
3.3V ─┐
      |
  [Button/Switch]
      |
GPIO ─┤── GPIO 7
      |
  [10K pull-down]
      |
GND ──┘
```

The 10K pull-down resistor keeps the pin LOW when the button is not pressed. When pressed, the pin goes HIGH.

> **Tip:** Many ESP32 pins have internal pull-up/pull-down resistors, but an external pull-down is more reliable for production use.

### Read

```bash
ionode read ionode-01 door
```

```
  door  0
```

### Raw NATS

```bash
# Register
nats req ionode-01.config.device.add '{"n":"door","k":"digital_in","p":7}'

# Read
nats req ionode-01.hal.door ""
```

---

## Registering a Digital Output

A named digital output you can control by name.

### Register

```bash
ionode device add ionode-01 led digital_out 8
```

### Wiring: LED + Resistor

```
GPIO 8 ──[330Ω]──[LED ▶|]── GND
```

The 330Ω resistor limits current to a safe level for the LED (~10mA at 3.3V).

### Control

```bash
ionode write ionode-01 led 1    # on
ionode write ionode-01 led 0    # off
```

### State Persistence

Digital output state survives reboots. When you set `led` to 1, IOnode saves the state to flash (debounced, 5-second delay to protect flash wear). On next boot, the LED turns on automatically.

### Raw NATS

```bash
# Register
nats req ionode-01.config.device.add '{"n":"led","k":"digital_out","p":8}'

# Control
nats req ionode-01.hal.led.set "1"

# Read state
nats req ionode-01.hal.led ""
```

---

## Registering a Relay

Relays are digital outputs with optional inversion for active-low relay modules (which are the most common type sold online).

### Wiring: Relay Module

```
ESP32                 Relay Module
─────                 ────────────
3.3V  ──────────────  VCC
GND   ──────────────  GND
GPIO 16 ────────────  IN (signal)
```

Most relay modules are **active-low** - the relay closes when the signal pin goes LOW. Use `--inverted` to match this behavior.

### Register (Active-Low)

```bash
ionode device add ionode-01 heater relay 16 --inverted
```

### Register (Active-High)

```bash
ionode device add ionode-01 pump relay 16
```

### Control

```bash
ionode write ionode-01 heater 1    # relay closes (GPIO goes LOW because inverted)
ionode write ionode-01 heater 0    # relay opens  (GPIO goes HIGH because inverted)
```

The `--inverted` flag flips the GPIO logic so that `1` always means "on" (relay closed) and `0` means "off" (relay open), regardless of whether the module is active-high or active-low.

### State Persistence

Relay state survives reboots. Saved to `devices.json` with a 5-second debounce to protect flash. On boot, relays restore their last state automatically.

### Raw NATS

```bash
# Register (inverted)
nats req ionode-01.config.device.add '{"n":"heater","k":"relay","p":16,"i":true}'

# Control
nats req ionode-01.hal.heater.set "1"

# Read state
nats req ionode-01.hal.heater ""
```

---

## Registering a PWM Output

Named PWM outputs for motors, fans, dimmers - anything that needs variable power.

### Register

```bash
ionode device add ionode-01 fan pwm 9
```

### Wiring: MOSFET + Motor/Fan

```
              ┌──── Motor/Fan ──── VCC (5-12V)
              |
GPIO 9 ─---─[Gate]  N-MOSFET (e.g. IRLZ44N)
              |
           [Source]
              |
GND ──────────┘
```

The MOSFET acts as a switch controlled by the GPIO. The ESP32's 3.3V signal drives the gate, and the MOSFET handles the higher voltage/current load.

> **Important:** Use a logic-level MOSFET (like IRLZ44N) that fully turns on at 3.3V gate voltage. Standard MOSFETs may need 5V+ on the gate.

### Control

```bash
ionode write ionode-01 fan 0      # off
ionode write ionode-01 fan 128    # 50% duty
ionode write ionode-01 fan 255    # full speed
```

### NOT Persisted Across Reboot

PWM values are intentionally **not saved** to flash. Resuming arbitrary analog outputs on boot could be unsafe (a motor spinning up unexpectedly, a heater element at full power). PWM starts at 0 on every boot.

### Raw NATS

```bash
# Register
nats req ionode-01.config.device.add '{"n":"fan","k":"pwm","p":9}'

# Control
nats req ionode-01.hal.fan.set "128"

# Read
nats req ionode-01.hal.fan ""
```

---

## RGB LED

Many ESP32 boards have a built-in addressable RGB LED. IOnode auto-registers it as `rgb_led` if the board defines `RGB_BUILTIN` (ESP32-C6 and ESP32-S3 boards typically have one).

### Check if Available

```bash
ionode read ionode-01 rgb_led
```

If it returns a value, you have one. If it returns an error, your board doesn't have a built-in RGB LED.

### Set a Color

The value is a packed `0xRRGGBB` integer in **decimal**:

```bash
ionode write ionode-01 rgb_led 16711680    # red   (0xFF0000)
ionode write ionode-01 rgb_led 65280       # green (0x00FF00)
ionode write ionode-01 rgb_led 255         # blue  (0x0000FF)
ionode write ionode-01 rgb_led 16777215    # white (0xFFFFFF)
ionode write ionode-01 rgb_led 0           # off
```

### Color Reference

| Color | Hex | Decimal |
|-------|-----|---------|
| Red | `0xFF0000` | `16711680` |
| Green | `0x00FF00` | `65280` |
| Blue | `0x0000FF` | `255` |
| Yellow | `0xFFFF00` | `16776960` |
| Cyan | `0x00FFFF` | `65535` |
| Magenta | `0xFF00FF` | `16711935` |
| White | `0xFFFFFF` | `16777215` |
| Orange | `0xFF8000` | `16744448` |
| Off | `0x000000` | `0` |

### NOT Persisted

Like PWM, RGB LED values are not saved across reboots. The LED starts off on every boot.

### Raw NATS

```bash
nats req ionode-01.hal.rgb_led.set "16711680"    # red
nats req ionode-01.hal.rgb_led.set "0"           # off
```

---

## Threshold Events on Digital Inputs

Registered sensors support threshold events - NATS notifications when a value crosses a boundary.

### Example: Door Open Alert

```bash
ionode event set ionode-01 door --above 0 --cooldown 5
```

This fires when `door` goes from 0 to 1 (door opens). The 5-second cooldown prevents repeated alerts from a bouncing switch.

### Monitor Events

```bash
ionode watch --events
```

Or subscribe directly:

```bash
nats sub 'ionode-01.events.door'
```

Event payload:

```json
{
  "event": "threshold",
  "device": "ionode-01",
  "sensor": "door",
  "value": 1.0,
  "threshold": 0.0,
  "direction": "above",
  "unit": ""
}
```

### Raw NATS

```bash
nats req ionode-01.config.event.set '{"n":"door","t":0,"d":"above","cd":5}'
```

Events persist across reboots.

---

## Web UI

The on-device web UI at `http://{device-ip}/` supports all device types:

- **Devices tab** - add/remove devices, see live values, toggle actuators
- **Pins tab** - raw GPIO/ADC/PWM access with no registration needed
- **Add Device** form supports all types: digital_in, digital_out, relay, pwm, rgb_led
- Relay and digital output devices show **toggle switches**
- PWM devices show **sliders** (0–255)
- RGB LED shows a **color picker**

---

## Quick Reference

| Feature | Raw Access | Registered Device |
|---------|-----------|-------------------|
| Digital read | `ionode gpio <node> <pin> get` | `ionode read <node> <name>` |
| Digital write | `ionode gpio <node> <pin> set 1` | `ionode write <node> <name> 1` |
| ADC read | `ionode adc <node> <pin>` | `ionode read <node> <name>` (analog_in) |
| PWM write | `ionode pwm <node> <pin> set 128` | `ionode write <node> <name> 128` |
| Relay | - | `ionode write <node> <name> 1` |
| RGB LED | - | `ionode write <node> <name> 16711680` |

### Device Kinds

| Kind | Type | Pin | Persisted | Inverted |
|------|------|-----|-----------|----------|
| `digital_in` | Sensor | GPIO | - | No |
| `analog_in` | Sensor | ADC GPIO | - | No |
| `digital_out` | Actuator | GPIO | Yes | No |
| `relay` | Actuator | GPIO | Yes | Optional |
| `pwm` | Actuator | GPIO | No (safety) | No |
| `rgb_led` | Actuator | RGB_BUILTIN | No (safety) | No |

### Resolution

| Interface | Resolution | Range |
|-----------|-----------|-------|
| GPIO | 1-bit | 0 or 1 |
| ADC | 12-bit | 0–4095 |
| PWM | 8-bit | 0–255 |

---

## See Also

- [Standard Sensors Guide](IOnode-Standard-Sensors.md) - NTC thermistor, LDR, internal temperature
- [I2C Sensors Guide](I2C-Sensors.md) - BME280, BH1750, SHT31, ADS1115
- [I2C Display Guide](I2C-Display.md) - SSD1306 OLED display
- [CLI Reference](CLI.md) - All CLI commands
- [NATS API Reference](NATS-API.md) - Every NATS subject, payload, and response
