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

## Production Hardening

The first version had a bottleneck that isn't obvious until you do the math: a full 128×64 monochrome frame is 1024 bytes, and at the Arduino default I2C speed (100kHz) that's roughly **90ms to push one frame** — and `Wire`'s transfer is blocking on AVR, so the *entire MCU* froze for that ~90ms on every single `display.display()` call, capping the real frame rate around 10fps and swallowing any button press that landed in that window. Fixed here, along with the other flaws identified in review:

| Flaw | Fix |
|---|---|
| ~90ms blocking I2C write per frame at the 100kHz default | `Wire.setClock(400000)` — fast mode, cuts the write to roughly a quarter of the time. |
| A glitched I2C bus could hang the sketch forever | `Wire.setWireTimeoutUs()` added behind a feature-detect guard. Confirmed by actual compile: on `arduino:avr@1.8.8` (the current core) this guard evaluates false and the call is skipped — that AVR core's `Wire.h` doesn't expose the timeout feature yet. The watchdog is the real backstop for an I2C hang on this core; the guard just means the sketch builds cleanly either way instead of failing outright. |
| Raw potentiometer readings jittered on electrical noise | Exponential moving average (`EMA_ALPHA = 0.2`) smooths both speed and temp before they're mapped to display units. |
| Turn signal state lost on every power cycle | Persisted to EEPROM on toggle (`EEPROM.update`, so it only writes on an actual change — EEPROM has a finite write-cycle life). |
| Turn signal blink rate was an arbitrary 400ms | Changed to 333ms, landing at ~90 flashes/minute — inside the ECE R6 regulated range (60–120 fpm) rather than a number picked because it "looked right." |
| A hung loop had nothing to recover it | Watchdog timer (`wdt_enable(WDTO_2S)`) resets the MCU if `loop()` ever fails to return within 2 seconds. |

**What's still not production-ready, and can't be fixed in this firmware alone:**
- **Not reading a real vehicle bus.** This reads two potentiometers as stand-ins for CAN signals. A production dashboard needs to handle the actual hard problem — what to show when the CAN message for speed goes stale or stops arriving entirely — which has no meaningful equivalent to build against without a real CAN source.
- **No legal calibration/traceability.** Real speedometers must never under-read actual speed beyond a defined tolerance (UNECE regulations) with a verified, auditable calibration chain. This is a `map()` call with no cross-check against a second signal source.
- **Wrong display technology for a real cluster.** OLEDs burn in on static elements over years (the speed digits sit in the same pixels constantly) and read poorly in direct sunlight — production clusters use sunlight-readable TFTs for exactly this reason.
- **No functional-safety process behind it** — no ISO 26262 hazard analysis or ASIL rating, which real vehicle safety-relevant displays require.
- **I2C bus timeout isn't actually active on the current core.** As noted above — `Wire.setWireTimeoutUs()` compiles out on `arduino:avr@1.8.8`. A real deploy on a newer core (or a different MCU with hardware I2C timeout support) would need to re-check this; until then, the watchdog is the only recovery path for a hung bus.

**Compile-verified:** `arduino-cli compile --fqbn arduino:avr:uno` against `arduino:avr@1.8.8` with Adafruit GFX Library 1.12.6 + Adafruit SSD1306 2.5.17 + Adafruit BusIO 1.17.4 — 15680 bytes flash (48%), 377 bytes RAM (18%). Clean build, no warnings.
