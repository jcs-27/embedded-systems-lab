# Mini Weather Station

A DHT22 measures temperature and humidity; a 128×64 OLED shows the live reading plus the all-time min/max (persisted in EEPROM — survives power loss). Three LEDs give an at-a-glance comfort read.

## Behaviour

- **Display:** current temp (large), humidity, running min/max for both, and a lifetime sensor-fault counter
- **Comfort LEDs:** 🟢 green (18–26°C, 30–60% humidity) · 🟡 yellow (moderate) · 🔴 red (outside both ranges)
- **Reset button:** clears the stored min/max and starts tracking fresh from the current reading, with a brief on-screen confirmation

## Circuit

| Component | Arduino Pin |
|-----------|-------------|
| DHT22 data | D7 |
| Reset pushbutton | D8 (INPUT_PULLUP, active low) |
| Green LED (via 220Ω) | D9 |
| Yellow LED (via 220Ω) | D10 |
| Red LED (via 220Ω) | D11 |
| SSD1306 OLED SDA / SCL | A4 / A5 |

In Wokwi, drag the DHT22's temperature/humidity sliders to change the simulated reading live.

## Why it's built this way

Same non-blocking discipline as the rest of this lab: the DHT22 needs at least ~2 seconds between reads (it'll return stale or garbage data if polled faster), enforced with a `millis()` timer rather than `delay()`. A failed read — DHT22s do occasionally miss a beat, in simulation and in real hardware — increments a persisted fault counter and keeps showing the last good reading, rather than flashing a bogus temperature at whoever's looking at the display. The OLED redraw is also rate-limited to 200ms; there's no reason to pay the ~25ms I2C write cost 40 times a second for a value that only changes every 2.5 seconds.

## Requires

Add via Wokwi's Library Manager (see `libraries.txt`):
- DHT sensor library (Adafruit)
- Adafruit Unified Sensor (dependency of the above)
- Adafruit GFX Library
- Adafruit SSD1306

## Bugs found and fixed during testing

Compiling this the first time caught a real Arduino toolchain gotcha, not just a typo: the `enum Comfort` was originally declared partway through the file, right above the function that returns it. Arduino's build step auto-generates forward prototypes for every function in a sketch and inserts them immediately after the `#include` lines — *before* anything else in the file, including a custom type declared further down. That meant the auto-generated prototype for `Comfort classify(...)` referenced a type that, from the compiler's point of view at that point in the file, didn't exist yet: `error: 'Comfort' does not name a type`. Fixed by moving the `enum` above every function definition, which is the standard fix for this specific class of Arduino compile error — worth knowing about since it'll resurface on any sketch that returns a custom type from a function declared after that type first appears.

**Compile-verified:** `arduino-cli compile --fqbn arduino:avr:uno` against `arduino:avr@1.8.8` with DHT sensor library 1.4.7 + Adafruit Unified Sensor 1.1.15 + Adafruit GFX Library 1.12.6 + Adafruit SSD1306 2.5.17 — 19250 bytes flash (59%), 701 bytes RAM (34%). Clean build, no warnings.
