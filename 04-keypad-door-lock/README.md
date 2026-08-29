# Keypad Door Lock

A 4x4 matrix keypad guards a servo-actuated latch — punch in the PIN, press `#` to submit, and the latch swings open. Get it wrong three times and the lock enters a timed lockout with a flashing red LED and an alarm tone.

## Behaviour

- Enter up to 4 digits, `#` to submit, `*` to clear and start over
- **Correct PIN (default `1234`):** latch servo swings to 90°, green LED on, short success tone, auto re-locks after 5 seconds
- **Wrong PIN:** short error buzz, entry clears, try again
- **3 wrong attempts in a row:** 15-second lockout — red LED flashes, alarm tone, keypad ignored entirely until it expires; the lockout event is counted and saved to EEPROM (survives power loss)

## Circuit

| Component | Arduino Pin |
|-----------|-------------|
| Keypad rows R1–R4 | D2–D5 |
| Keypad columns C1–C4 | D6–D9 |
| Latch servo (PWM) | D10 |
| Green LED (via 220Ω) | D11 |
| Red LED (via 220Ω) | D12 |
| Buzzer | A0 |

## Why it's built this way

Same shape as the [traffic light controller](../03-traffic-light-controller): one `enum` state (`LOCKED` / `UNLOCKED` / `LOCKOUT`) with a single `enterState()` doing entry actions, timed purely off `millis()` — the lockout countdown and the auto re-lock timer both run without ever blocking the keypad scan. `wrongAttempts` resets to zero only on a *correct* entry, so it genuinely takes 3 consecutive failures to trigger a lockout, not 3 failures scattered across a long session with successes in between.

To change the code, edit `CORRECT_PIN` in the sketch — it's a compile-time constant, not something a real product would want (see below).

## Requires

Add via Wokwi's Library Manager (see `libraries.txt`):
- Keypad (by Mark Stanley / Alexander Brevig)
- Servo — the Arduino IDE bundles this by default so it's easy to assume it's always there, but a minimal `arduino-cli` core install does *not* include it; compiling this confirmed that the hard way (`fatal error: Servo.h: No such file or directory` until it was installed as a library explicitly).

EEPROM ships with the AVR core — no extra install needed.

## Known limitations (same honesty policy as the rest of this lab)

- **PIN is hardcoded in firmware**, not user-settable without reflashing — a real product needs a PIN-change flow, ideally requiring the current PIN first.
- **PIN sits in flash in plain text.** Trivial to read off the chip with a programmer. A real lock would need at least a hash comparison, ideally a secure element.
- **No physical tamper detection** on the enclosure or the latch itself — a real lock also needs to know if it was forced open regardless of what the keypad says.
- **Lockout duration is fixed** at 15s regardless of how many lockouts have happened — real access-control systems typically escalate (longer lockout each repeated offense) to resist brute-forcing by someone willing to wait.

**Compile-verified:** `arduino-cli compile --fqbn arduino:avr:uno` against `arduino:avr@1.8.8` with Keypad 3.1.1 + Servo 1.3.0 — 5976 bytes flash (18%), 446 bytes RAM (21%). Clean build, no warnings.
