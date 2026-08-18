# Digital Dashboard Cluster

A simulated instrument cluster on a 128×64 OLED: live speed readout, an engine-temperature gauge bar that flashes a "HOT" warning, and a blinking turn-signal indicator (on-screen arrow + physical LED).

## Behaviour

- **Speed**: potentiometer on A0 → mapped to 0–220 km/h, shown as a large digital number
- **Engine temp**: potentiometer on A1 → mapped to 40–120°C, shown as a fill-bar gauge; flashes "HOT" once it crosses 100°C
- **Turn signal**: pushbutton on D2 toggles it on/off; while on, an arrow blinks in the corner of the screen and the external LED blinks in sync (400 ms period)

## Circuit

| Component | Arduino Pin |
|-----------|-------------|
| SSD1306 OLED SDA | A4 |
| SSD1306 OLED SCL | A5 |
| Speed potentiometer wiper | A0 |
| Temp potentiometer wiper | A1 |
| Turn signal pushbutton | D2 (INPUT_PULLUP, active low) |
| Turn signal LED (via 220Ω) | D3 |

In Wokwi, drag the potentiometer knobs to change speed/temp live, and click the pushbutton to toggle the turn signal.

## Why it's built this way

The button uses a debounce window (40 ms) against a stable-state latch instead of trusting a single `digitalRead()` — mechanical (and simulated) buttons bounce, and without debouncing a single press can register as several toggles. All screen and LED blinking is timed off `millis()`, not `delay()`, so the potentiometer readings stay responsive even mid-blink.

## Requires

Add via Wokwi's Library Manager (see `libraries.txt`):
- Adafruit GFX Library
- Adafruit SSD1306
