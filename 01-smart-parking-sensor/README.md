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
| HC-SR04 ECHO | D10 |
| Green LED (via 220Ω) | D5 |
| Yellow LED (via 220Ω) | D6 |
| Red LED (via 220Ω) | D7 |
| Buzzer | D8 |

In the Wokwi simulator, drag the HC-SR04's distance slider (or click-drag the sensor itself closer/further from the wall) to change the simulated distance and watch the zones change live.

## Why it's built this way

`pulseIn()` has a timeout (30 ms) so the loop never hangs waiting for an echo that isn't coming — a hardware sensor can and will occasionally miss a pulse, and a real controller can't afford to block indefinitely on that. The buzzer beep pattern is driven off `millis()` rather than `delay()`, so the loop stays responsive even while beeping.
