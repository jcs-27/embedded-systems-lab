/*
 * Smart Parking Sensor — hardened revision
 *
 * HC-SR04 ultrasonic sensor measures distance to an obstacle behind the
 * "vehicle". LEDs and a buzzer give the driver graduated feedback.
 *
 * Zones:
 *   > SAFE_CM        green LED, silent
 *   CAUTION_CM..SAFE  yellow LED, slow beep
 *   STOP_CM..CAUTION  red LED, fast beep
 *   < STOP_CM        red LED solid, continuous tone (collision imminent)
 *
 * Hardening changes vs. the first version (see README "Production
 * Hardening" section for full rationale):
 *   - Echo timing moved off blocking pulseIn() onto a pin-change
 *     interrupt, and the end-of-loop delay(60) is gone — the loop never
 *     blocks now, regardless of sensor timing.
 *   - 3-sample median filter rejects single bad ultrasonic readings.
 *   - Hysteresis band at each zone boundary stops LED/buzzer chatter.
 *   - Ambient temperature compensation constant replaces the fixed /58
 *     divisor (which silently assumed 20C air).
 *   - A watchdog resets the MCU if the main loop ever hangs.
 *   - Sensor-timeout events are counted and persisted in EEPROM, a
 *     minimal stand-in for automotive-style fault logging (DTCs).
 */

#include <avr/wdt.h>
#include <EEPROM.h>

const uint8_t PIN_TRIG = 9;
const uint8_t PIN_ECHO = 3; // must be an interrupt-capable pin (D2/D3 on Uno)
const uint8_t PIN_LED_GREEN = 5;
const uint8_t PIN_LED_YELLOW = 6;
const uint8_t PIN_LED_RED = 7;
const uint8_t PIN_BUZZER = 8;

const float AMBIENT_TEMP_C = 20.0; // recalibrate if deployed somewhere colder/hotter
const float SPEED_OF_SOUND_CM_PER_US = (331.3 + 0.606 * AMBIENT_TEMP_C) / 10000.0;

const float SAFE_CM = 50.0;
const float CAUTION_CM = 20.0;
const float STOP_CM = 5.0;
const float HYSTERESIS_CM = 3.0; // dead-band to stop chatter at zone boundaries

const unsigned long SLOW_BEEP_MS = 500;
const unsigned long FAST_BEEP_MS = 120;
const unsigned long PING_INTERVAL_MS = 60;   // how often we trigger a new ping
const unsigned long ECHO_TIMEOUT_US = 30000; // ~5m max range
const unsigned long SERIAL_PRINT_INTERVAL_MS = 250;

const int EEPROM_ADDR_FAULT_COUNT = 0; // uint16_t

enum Zone { ZONE_SAFE, ZONE_CAUTION, ZONE_DANGER, ZONE_STOP };
Zone currentZone = ZONE_SAFE;

// --- interrupt-driven echo capture --------------------------------------
volatile unsigned long echoStartUs = 0;
volatile unsigned long echoDurationUs = 0;
volatile bool echoPending = false;   // true while we're waiting on a rising/falling pair
volatile bool echoReady = false;     // true once a full pulse has been captured

void echoISR() {
  if (digitalRead(PIN_ECHO) == HIGH) {
    echoStartUs = micros();
    echoPending = true;
  } else if (echoPending) {
    echoDurationUs = micros() - echoStartUs;
    echoPending = false;
    echoReady = true;
  }
}

unsigned long lastPingAt = 0;
unsigned long pingSentAt = 0;
bool waitingForEcho = false;

// --- 3-sample median filter ---------------------------------------------
float sampleBuf[3] = { -1, -1, -1 };
uint8_t sampleIdx = 0;

float medianOf3(float a, float b, float c) {
  if (a > b) { float t = a; a = b; b = t; }
  if (b > c) { float t = b; b = c; c = t; }
  if (a > b) { float t = a; a = b; b = t; }
  return b;
}

// --- fault logging ---------------------------------------------------------
uint16_t faultCount = 0;

void recordSensorFault() {
  faultCount++;
  EEPROM.put(EEPROM_ADDR_FAULT_COUNT, faultCount);
}

// --- outputs ---------------------------------------------------------------
unsigned long lastBeepToggle = 0;
bool beepOn = false;

void setLeds(bool green, bool yellow, bool red) {
  digitalWrite(PIN_LED_GREEN, green);
  digitalWrite(PIN_LED_YELLOW, yellow);
  digitalWrite(PIN_LED_RED, red);
}

void driveBuzzer(unsigned long intervalMs, bool solid) {
  if (solid) {
    tone(PIN_BUZZER, 1500);
    return;
  }
  if (intervalMs == 0) {
    noTone(PIN_BUZZER);
    return;
  }
  unsigned long now = millis();
  if (now - lastBeepToggle >= intervalMs) {
    lastBeepToggle = now;
    beepOn = !beepOn;
    if (beepOn) tone(PIN_BUZZER, 1500);
    else noTone(PIN_BUZZER);
  }
}

// hysteresis-aware zone classification: needs to know the current zone to
// decide which threshold (enter vs. exit) applies
Zone classifyZone(float distanceCm, Zone previousZone) {
  if (distanceCm < 0) return ZONE_SAFE; // no echo: fail safe, not alarming

  switch (previousZone) {
    case ZONE_SAFE:
      if (distanceCm <= CAUTION_CM) return ZONE_DANGER;
      if (distanceCm <= SAFE_CM) return ZONE_CAUTION;
      return ZONE_SAFE;
    case ZONE_CAUTION:
      if (distanceCm <= STOP_CM) return ZONE_STOP;
      if (distanceCm <= CAUTION_CM) return ZONE_DANGER;
      if (distanceCm > SAFE_CM + HYSTERESIS_CM) return ZONE_SAFE;
      return ZONE_CAUTION;
    case ZONE_DANGER:
      if (distanceCm <= STOP_CM) return ZONE_STOP;
      if (distanceCm > CAUTION_CM + HYSTERESIS_CM) return ZONE_CAUTION;
      return ZONE_DANGER;
    case ZONE_STOP:
      if (distanceCm > STOP_CM + HYSTERESIS_CM) return ZONE_DANGER;
      return ZONE_STOP;
  }
  return ZONE_SAFE;
}

void applyZone(Zone z) {
  switch (z) {
    case ZONE_SAFE:    setLeds(true, false, false); driveBuzzer(0, false); break;
    case ZONE_CAUTION: setLeds(false, true, false);  driveBuzzer(SLOW_BEEP_MS, false); break;
    case ZONE_DANGER:  setLeds(false, false, true);  driveBuzzer(FAST_BEEP_MS, false); break;
    case ZONE_STOP:    setLeds(false, false, true);  driveBuzzer(0, true); break;
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  attachInterrupt(digitalPinToInterrupt(PIN_ECHO), echoISR, CHANGE);

  EEPROM.get(EEPROM_ADDR_FAULT_COUNT, faultCount);
  if (faultCount == 0xFFFF) faultCount = 0; // unwritten EEPROM reads as 0xFF bytes
  Serial.print("Boot OK. Stored sensor-fault count: ");
  Serial.println(faultCount);

  wdt_enable(WDTO_2S); // reset the MCU if loop() ever hangs for >2s
}

void loop() {
  wdt_reset();

  unsigned long now = millis();

  // fire a new ping on a fixed, non-blocking schedule
  if (!waitingForEcho && now - lastPingAt >= PING_INTERVAL_MS) {
    lastPingAt = now;
    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10); // sensor-mandated trigger pulse width, not a control-loop delay
    digitalWrite(PIN_TRIG, LOW);
    pingSentAt = now;
    waitingForEcho = true;
  }

  // service a completed echo capture
  if (echoReady) {
    noInterrupts();
    unsigned long durationUs = echoDurationUs;
    echoReady = false;
    interrupts();

    float rawCm = durationUs * SPEED_OF_SOUND_CM_PER_US / 2.0;
    sampleBuf[sampleIdx % 3] = rawCm;
    sampleIdx++;
    waitingForEcho = false;

    float filteredCm = medianOf3(sampleBuf[0], sampleBuf[1], sampleBuf[2]);
    currentZone = classifyZone(filteredCm, currentZone);
    applyZone(currentZone);

    static unsigned long lastPrint = 0;
    if (now - lastPrint >= SERIAL_PRINT_INTERVAL_MS) {
      lastPrint = now;
      Serial.print("Distance: "); Serial.print(filteredCm); Serial.println(" cm");
    }
  }

  // a ping that never got an echo back: timeout, log it, fail safe
  if (waitingForEcho && (now - pingSentAt) * 1000UL > ECHO_TIMEOUT_US) {
    waitingForEcho = false;
    recordSensorFault();
    currentZone = ZONE_SAFE; // fail safe: never alarm on a missing reading
    applyZone(currentZone);
  }
}
