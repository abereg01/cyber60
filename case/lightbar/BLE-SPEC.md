# Light bar BLE spec, v1

How the KNOB (or a phone) talks to the light bar's ESP32-C3. The bar is a BLE
**peripheral**, the knob is the **central**. Firmware: `fw/`. All numbers are
**little-endian**. Out-of-range values are clamped, never rejected.

## Discovery

- Advertised name: `Lightbar`
- Advertised 128-bit service UUID: `6c7b0001-8a3e-4f5e-9d2b-4c1ee1b0a001`
- Up to 3 connections at once (the knob, a phone, one spare). Advertising goes
  on while a slot is free.
- No pairing in v1: anyone in Bluetooth range can change the light. That is a
  light, not a lock. If it matters later: require bonding on the Step and State
  writes and let the knob bond once (the knob would then keep the key).

## Characteristics

All UUIDs are `6c7b00NN-8a3e-4f5e-9d2b-4c1ee1b0a001`.

### State, `…0002`: read, write, notify (8 bytes)

| Byte | Field | Range | Meaning |
|---|---|---|---|
| 0 | `on` | 0 or 1 | Light on/off. Brightness is kept while off |
| 1 | `mode` | 0 white, 1 colour | **White** uses the warm and cool white LEDs by temperature; **colour** uses R, G, B by hue and saturation. They never mix, so power stays low |
| 2 | `brightness` | 0–255 | Master brightness (before gamma) |
| 3 | `sat` | 0–255 | Colour saturation (colour mode) |
| 4–5 | `hue` | 0–359 (u16) | Colour hue in degrees: 0 red, 120 green, 240 blue |
| 6–7 | `cct` | 2700–6500 (u16) | White colour temperature in kelvin (white mode) |

A write must be exactly 8 bytes (others are ignored). The bar notifies the new
State to every subscriber after any change, at most 20 times a second, so the
knob's screen follows a phone and vice versa.

Defaults on first boot: on, white, brightness 80, 3000 K, hue 30, sat 255.
After that the last State is restored on power-up (saved 2 s after the last
change, so a spinning ring does not wear the flash).

### Step, `…0003`: write without response (3 bytes)

**This is what the knob should use for the ring.** A relative change, so two
writers can't overwrite each other and the knob never reads before writing.

| Byte | Field | Meaning |
|---|---|---|
| 0 | `target` | 0 brightness, 1 cct (kelvin), 2 hue (degrees), 3 saturation, 4 power toggle |
| 1–2 | `delta` | Signed 16-bit, in the target's own units. Ignored for the power toggle |

Brightness, cct and saturation stop at their limits; **hue wraps** around the
circle. A brightness step while the light is off turns it on. Suggested knob
mapping: one detent = ±4 brightness, ±50 K, ±6° hue, ±8 saturation; a tap on
the knob screen switches `mode`, which is a State write (read State, flip
byte 1, write all 8 bytes back).

### Config, `…0004`: read, write (4 bytes), saved across power loss

| Byte | Field | Range | Default | Meaning |
|---|---|---|---|---|
| 0 | `limit` | 10–100 | 100 | Master brightness ceiling, percent. Use it to cap power |
| 1–2 | `fade_ms` | 0–2000 (u16) | 250 | Time to ease to a new value. The ring stays smooth, 0 snaps |
| 3 | `gamma_x10` | 10–30 | 22 | Gamma × 10. 22 = 2.2, which makes low brightness usable |

### Info, `…0005`: read (text)

ASCII, for example `lightbar 0.1.0 ch=5 pwm=20kHz/10bit`. Version, number of
PWM channels and PWM speed.

## Hardware behind it

Five low-side MOSFET channels, PWM at 20 kHz (no flicker on a phone camera),
10-bit. GPIO0 red, GPIO1 green, GPIO3 blue, GPIO4 warm white, GPIO5 cool white.
At most 2 "channel-equivalents" of output are ever on at once (the firmware
scales down if a mid-range white would ask for more), which protects a 5 V
rail. With a 12 V strip the same firmware applies; only the power parts change.

## For the knob firmware

Scan for the service UUID, connect, subscribe to State, write Step for ring
movement. On connect, read State once to draw the screen. Reconnect with
backoff (the bar can be switched off at the wall).
