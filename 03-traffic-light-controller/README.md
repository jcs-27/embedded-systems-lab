# Traffic Light + Pedestrian Crossing Controller

A four-way intersection (North-South vs East-West) with an on-demand pedestrian crossing, built as a single non-blocking finite state machine.

## Behaviour

```
NS_GREEN (6s) → NS_YELLOW (1.5s) → ALL_RED (0.5s) → [pedestrian requested?]
                                                          │
                                    yes ──────────────────┤────────────── no
                                    ▼                                      ▼
                          PED_WALK (3s, steady beep)              EW_GREEN (6s)
                          → PED_FLASH (2s, flashing              → EW_YELLOW (1.5s)
                            green + fast beep)                   → ALL_RED (0.5s)
                          → ALL_RED (0.5s)                        → back to NS_GREEN
                          → EW_GREEN
```

Pressing the WALK button at any point latches a pedestrian request; it's served at the next safe all-red gap rather than interrupting a light mid-phase.

## Circuit

| Component | Arduino Pin |
|-----------|-------------|
| NS red / yellow / green | D2 / D3 / D4 |
| EW red / yellow / green | D5 / D6 / D7 |
| Pedestrian red / green | D8 / D9 |
| WALK pushbutton | D10 (INPUT_PULLUP, active low) |
| Buzzer | D11 |

## Why it's built this way

Everything runs off one `switch` on a `State` enum, timed with `millis()` instead of `delay()` — the classic beginner version of this project blocks on `delay()` inside each phase, which means a button press during a 6-second green light is silently dropped. Here `pollWalkButton()` runs every loop iteration regardless of what phase the lights are in, so the request is always caught and served at the next safe gap. This is the same pattern used in the [PID cruise control](../) simulation: keep the control loop non-blocking, let state transitions be explicit and time-driven.

## Production Hardening

The most serious gap in the first version: it always booted straight into `NS_GREEN`, unconditionally. If power blipped while EW had a green with cars mid-intersection, an instant restart into NS_GREEN — with no caution period — is genuinely dangerous. Fixed here, along with the other flaws identified in review:

| Flaw | Fix |
|---|---|
| Booted straight into `NS_GREEN` regardless of what was happening before power loss | Added `STARTUP_FLASH`: every boot — first power-up or recovery from a mid-cycle brownout — starts with 5 seconds of all-way flashing red (the standard "treat as a 4-way stop" caution state) before any green is shown. |
| No defense against a corrupted/miscomputed light combination reaching the hardware | `setLights()` now refuses to energize NS and EW green/yellow simultaneously — see the important caveat below. |
| A hung loop had nothing to recover it | Watchdog timer (`wdt_enable(WDTO_2S)`) resets the MCU if `loop()` ever fails to return within 2 seconds — including the intentional halt inside a detected conflict. |
| No diagnostic trail | Boot count and detected-conflict count are persisted in EEPROM and printed on every boot; every state transition is now logged to Serial. |

**The conflict guard in `setLights()` is a real mitigation, but it is not what it might look like at first glance, and this matters enough to be explicit about it:** real traffic controllers (NEMA TS2) don't rely on the same software that *decides* the light state to also be the only thing preventing a conflict — they use a physically separate hardware module (a Malfunction Management Unit) that independently watches actual voltage on the signal heads. The reason is exactly the limitation of what's added here: this guard runs in the same codebase that computed the (correct or incorrect) arguments passed into it. If the bug were in the state machine's transition logic itself — the same source generating the bad call — this guard can still catch it before it reaches an LED, which is genuinely useful. But it can't catch a hardware fault downstream (a relay welded on, a burnt-out bulb reporting the wrong state), and it can't substitute for the architectural independence a real deployment requires. This was worth building because "catch it in software where you can" is still better than nothing — but it doesn't close the gap I described as the single most important one in review.

**Correction from the earlier flaw review:** I'd characterized a stuck-closed WALK button wiring fault as something that could cause the pedestrian phase to run on every single cycle. Re-reading the debounce logic closely: it only latches `pedestrianRequested` on a HIGH→LOW *edge*, so a button stuck LOW only triggers once, not repeatedly, until it physically releases. That specific throughput-DoS claim was overstated. What's still true: the firmware has no way to distinguish "a person pressed this" from "a wire shorted to ground," which matters for diagnostics even though it doesn't degrade intersection throughput the way I originally described.

**What's still not production-ready, and can't be fixed in this firmware alone:**
- **No independent conflict monitor.** As above — this needs a separate physical module, by design, not more firmware.
- **No hardware fault detection.** A welded relay or burnt-out bulb is invisible to this code; real controllers monitor actual current draw on each signal circuit.
- **No accessible pedestrian signal.** No audible cue or extended-crossing option — a real accessibility compliance gap (ADA / equivalent EU standards), not just a nice-to-have.
- **Fixed timing, no field configuration.** Real deployments need per-intersection timing studies and a way to adjust timing without reflashing firmware.
- **GPIO can't drive real signal loads.** This drives a few mA into LEDs; real signal heads need high-current drivers with flyback protection and galvanic isolation from the logic board.
- **No functional-safety process** — no ISO 26262/equivalent hazard analysis, mandatory before anything like this actually controls a real intersection.
**Compile-verified:** `arduino-cli compile --fqbn arduino:avr:uno` against `arduino:avr@1.8.8` — 4826 bytes flash (14%), 500 bytes RAM (24%). Clean build, no warnings.
