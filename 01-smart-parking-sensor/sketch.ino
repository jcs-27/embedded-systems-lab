/*
 * Smart Parking Sensor
 *
 * HC-SR04 ultrasonic sensor measures distance to an obstacle behind the
 * "vehicle". LEDs and a buzzer give the driver graduated feedback, the
 * same way a reverse-parking assist system does: calmer as you have more
 * room, urgent as you close in.
 *
 * Zones:
 *   > SAFE_CM        green LED, silent
 *   CAUTION_CM..SAFE  yellow LED, slow beep
 *   STOP_CM..CAUTION  red LED, fast beep
 *   < STOP_CM        red LED solid, continuous tone (collision imminent)
 */

const uint8_t PIN_TRIG = 9;
const uint8_t PIN_ECHO = 10;
const uint8_t PIN_LED_GREEN = 5;
const uint8_t PIN_LED_YELLOW = 6;
const uint8_t PIN_LED_RED = 7;
const uint8_t PIN_BUZZER = 8;

const float SAFE_CM = 50.0;
const float CAUTION_CM = 20.0;
const float STOP_CM = 5.0;

const unsigned long SLOW_BEEP_MS = 500;
const unsigned long FAST_BEEP_MS = 120;

unsigned long lastBeepToggle = 0;
bool beepOn = false;

float readDistanceCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  unsigned long durationUs = pulseIn(PIN_ECHO, HIGH, 30000UL); // 30ms timeout ~= 5m range
  if (durationUs == 0) {
    return -1; // no echo received (out of range)
  }
  return durationUs / 58.0; // speed of sound conversion, round trip
}

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

void setup() {
  Serial.begin(9600);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
}

void loop() {
  float distance = readDistanceCm();

  if (distance < 0) {
    // out of sensor range: treat as safe
    setLeds(true, false, false);
    driveBuzzer(0, false);
    Serial.println("Distance: out of range");
  } else if (distance > SAFE_CM) {
    setLeds(true, false, false);
    driveBuzzer(0, false);
    Serial.print("Distance: "); Serial.print(distance); Serial.println(" cm | SAFE");
  } else if (distance > CAUTION_CM) {
    setLeds(false, true, false);
    driveBuzzer(SLOW_BEEP_MS, false);
    Serial.print("Distance: "); Serial.print(distance); Serial.println(" cm | CAUTION");
  } else if (distance > STOP_CM) {
    setLeds(false, false, true);
    driveBuzzer(FAST_BEEP_MS, false);
    Serial.print("Distance: "); Serial.print(distance); Serial.println(" cm | DANGER");
  } else {
    setLeds(false, false, true);
    driveBuzzer(0, true);
    Serial.print("Distance: "); Serial.print(distance); Serial.println(" cm | STOP");
  }

  delay(60); // HC-SR04 needs a short settle time between pings
}
