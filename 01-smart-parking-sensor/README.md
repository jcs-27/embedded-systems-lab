# Smart Parking Sensor

Reverse-parking assist, simulated. An HC-SR04 ultrasonic sensor measures distance to an obstacle; LEDs and a buzzer escalate feedback as the "vehicle" gets closer, the same graduated-alert pattern used in real parking assist systems.

## Behaviour

| Distance | LED | Buzzer |
|----------|-----|--------|
| > 50 cm | 🟢 Green | Silent |
| 20–50 cm | 🟡 Yellow | Slow beep (500 ms) |
| 5–20 cm | 🔴 Red | Fast beep (120 ms) |
| < 5 cm | 🔴 Red (solid) | Continuous tone — stop |

Distance readings are also logged to the Serial Monitor at 9600 baud.

## Circuit

| Component | Arduino Pin |
|-----------|-------------|
| HC-SR04 TRIG | D9 |
| HC-SR04 ECHO | D3 (must be an interrupt pin — D2 or D3 on Uno) |
| Green LED (via 220Ω) | D5 |
| Yellow LED (via 220Ω) | D6 |
| Red LED (via 220Ω) | D7 |
| Buzzer | D8 |

In the Wokwi simulator, drag the HC-SR04's distance slider (or click-drag the sensor itself closer/further from the wall) to change the simulated distance and watch the zones change live.

## Why it's built this way

The buzzer beep pattern is driven off `millis()` rather than `delay()`, so the loop stays responsive even while beeping. Distance is measured via a pin-change interrupt on ECHO rather than `pulseIn()` — see below.

## Production Hardening

This started as a working demo with a real flaw: `pulseIn()` blocks the whole MCU for up to 30ms per ping, and the loop had a further `delay(60)` at the end, meaning the entire controller was frozen for a chunk of every cycle. That's harmless in a single-purpose demo and a real problem the moment this shares a chip with anything else. Fixed here:

| Flaw | Fix |
|---|---|
| Blocking `pulseIn()` + `delay(60)` froze the loop every cycle | Echo timing moved to a pin-change interrupt (`attachInterrupt` on D3, capturing rising/falling edges via `micros()`); pings are scheduled non-blockingly off `millis()`. The loop never blocks now. |
| Single bad ultrasonic reading (double-echo, angled surface) directly drove the display | 3-sample median filter rejects one-off outliers before a reading reaches the zone classifier. |
| LED/buzzer chatter at zone boundaries | Hysteresis band (`HYSTERESIS_CM`) — the zone only changes back once the reading has cleared the boundary by a margin, not the instant it crosses it. |
| Fixed `/58` divisor silently assumed 20°C air | Replaced with a temperature-derived constant (`AMBIENT_TEMP_C`) using the actual speed-of-sound formula, so it's a documented, adjustable calibration point instead of a magic number. |
| A hung loop had nothing to recover it | Watchdog timer (`wdt_enable(WDTO_2S)`) resets the MCU if `loop()` ever fails to come back around within 2 seconds. |
| No record of sensor failures | Timeout events (ping sent, no echo back) increment a counter persisted in EEPROM — a minimal stand-in for automotive-style fault codes, survives power loss, printed on every boot. |
| `Serial.print` every loop iteration | Rate-limited to once per 250ms — the loop can now run far faster than that, and flooding serial output isn't free. |

**What's still not production-ready, and can't be fixed in this firmware alone:**
- **Single sensor, no redundancy.** One HC-SR04 is one point of failure. Real ADAS parking systems use multiple overlapping sensors so one dead/misaligned unit can't produce a false "all clear" — that needs more hardware, not more code.
- **Not automotive-rated hardware.** No AEC-Q100 qualification, no conformal coating, no reverse-polarity/load-dump protection on the supply rail, no IP-rated enclosure for a sensor that would live in a bumper.
- **No independent safety layer.** Everything — sensing, filtering, and the decision to alarm — runs on one MCU. A single firmware bug (even in this hardened version) has no independent hardware backstop, unlike a real safety-critical system.
- **No functional-safety process behind it.** None of this went through ISO 26262 hazard analysis or an ASIL rating, which is mandatory before anything like this actually influences a real vehicle safety function.
- Not compile-tested against real Arduino hardware or `arduino-cli` — no toolchain was available in this environment. If something doesn't compile as-is, it's most likely the watchdog or EEPROM include on an unusual core; both are standard AVR libraries.
