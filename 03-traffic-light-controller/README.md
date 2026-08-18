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
