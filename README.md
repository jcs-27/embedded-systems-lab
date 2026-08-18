# Embedded Systems Lab

Three small embedded C/C++ projects, each simulated with [Wokwi](https://wokwi.com) — no physical hardware required to try them out.

Built as hands-on practice for automotive / embedded software engineering (real-time control, sensor I/O, non-blocking state machines) — the same skills behind [PID Cruise Control Simulation](../) and the CAN Bus simulator in this portfolio.

| # | Project | What it demonstrates |
|---|---------|----------------------|
| 1 | [Smart Parking Sensor](./01-smart-parking-sensor) | Ultrasonic distance sensing, threshold-based zone logic, LED/buzzer feedback |
| 2 | [Digital Dashboard Cluster](./02-digital-dashboard-cluster) | OLED graphics, analog sensor input, real-time gauge rendering |
| 3 | [Traffic Light + Pedestrian Crossing](./03-traffic-light-controller) | Finite state machine, non-blocking timing (`millis()`, no `delay()`), interrupt-style button handling |

## How to run any of these

Each folder is self-contained: `diagram.json` (circuit) + `sketch.ino` (firmware) + `libraries.txt` (dependencies, if any).

**Easiest — Wokwi web simulator:**
1. Go to [wokwi.com/projects/new/arduino-uno](https://wokwi.com/projects/new/arduino-uno)
2. Paste the contents of `sketch.ino` into the code editor
3. Click the `diagram.json` tab and paste in that file's contents (or use the visual editor to wire it up using the README's pin table — same result)
4. If `libraries.txt` exists, add those libraries via the Library Manager (bookcase icon)
5. Press the green ▶ Simulate button

**VS Code:** install the [Wokwi for VS Code](https://marketplace.visualstudio.com/items?itemName=Wokwi.wokwi-vscode) extension, open one of the project folders, and press F1 → "Wokwi: Start Simulator".

> Wiring was authored by hand against Wokwi's part library from memory of its schema — if a wire doesn't auto-connect when you open a project, the visual editor will highlight the mismatched pin; just redraw that one connection. Everything else (the firmware logic, state machines, sensor math) is untouched by that and works as written.

## Roadmap

- [ ] Add a 4th project: lane detection with OpenCV (separate repo, not Wokwi)
- [ ] Wire up a shared `platformio.ini` for local build/test outside Wokwi
- [ ] CI: lint + compile-check all three sketches on push (GitHub Actions + `arduino-cli`)
