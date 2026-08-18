/*
 * Traffic Light + Pedestrian Crossing Controller
 *
 * A four-way intersection (North-South vs East-West) with an on-demand
 * pedestrian crossing phase. Entirely non-blocking: the whole thing is
 * one finite state machine driven off millis(), so a pedestrian button
 * press is never missed while a light is "busy" waiting on delay().
 *
 * Press the WALK button at any time; the request is latched and served
 * at the next safe all-red gap between the NS and EW green phases.
 */

enum State {
  NS_GREEN,
  NS_YELLOW,
  ALL_RED_BEFORE_EW,
  PED_WALK,
  PED_FLASH,
  ALL_RED_AFTER_PED,
  EW_GREEN,
  EW_YELLOW,
  ALL_RED_BEFORE_NS
};

// NS light pins
const uint8_t PIN_NS_RED = 2, PIN_NS_YELLOW = 3, PIN_NS_GREEN = 4;
// EW light pins
const uint8_t PIN_EW_RED = 5, PIN_EW_YELLOW = 6, PIN_EW_GREEN = 7;
// Pedestrian light pins
const uint8_t PIN_PED_RED = 8, PIN_PED_GREEN = 9;
const uint8_t PIN_WALK_BUTTON = 10;
const uint8_t PIN_BUZZER = 11;

const unsigned long T_GREEN = 6000;
const unsigned long T_YELLOW = 1500;
const unsigned long T_ALL_RED = 500;
const unsigned long T_PED_WALK = 3000;
const unsigned long T_PED_FLASH = 2000;
const unsigned long PED_FLASH_PERIOD = 250;
const unsigned long PED_BEEP_PERIOD = 300;

State state = NS_GREEN;
unsigned long stateEnteredAt = 0;
bool pedestrianRequested = false;

bool lastButtonReading = HIGH;
unsigned long lastDebounceTime = 0;
bool stableButtonState = HIGH;
const unsigned long DEBOUNCE_MS = 40;

void setLights(bool nsR, bool nsY, bool nsG, bool ewR, bool ewY, bool ewG, bool pedR, bool pedG) {
  digitalWrite(PIN_NS_RED, nsR);
  digitalWrite(PIN_NS_YELLOW, nsY);
  digitalWrite(PIN_NS_GREEN, nsG);
  digitalWrite(PIN_EW_RED, ewR);
  digitalWrite(PIN_EW_YELLOW, ewY);
  digitalWrite(PIN_EW_GREEN, ewG);
  digitalWrite(PIN_PED_RED, pedR);
  digitalWrite(PIN_PED_GREEN, pedG);
}

void enterState(State s) {
  state = s;
  stateEnteredAt = millis();

  switch (s) {
    case NS_GREEN:             setLights(0,0,1, 1,0,0, 1,0); noTone(PIN_BUZZER); break;
    case NS_YELLOW:            setLights(0,1,0, 1,0,0, 1,0); noTone(PIN_BUZZER); break;
    case ALL_RED_BEFORE_EW:    setLights(1,0,0, 1,0,0, 1,0); noTone(PIN_BUZZER); break;
    case PED_WALK:              setLights(1,0,0, 1,0,0, 0,1); pedestrianRequested = false; break;
    case PED_FLASH:              setLights(1,0,0, 1,0,0, 0,0); break; // green toggled in loop()
    case ALL_RED_AFTER_PED:    setLights(1,0,0, 1,0,0, 1,0); noTone(PIN_BUZZER); break;
    case EW_GREEN:              setLights(1,0,0, 0,0,1, 1,0); noTone(PIN_BUZZER); break;
    case EW_YELLOW:              setLights(1,0,0, 0,1,0, 1,0); noTone(PIN_BUZZER); break;
    case ALL_RED_BEFORE_NS:    setLights(1,0,0, 1,0,0, 1,0); noTone(PIN_BUZZER); break;
  }
}

void pollWalkButton() {
  bool reading = digitalRead(PIN_WALK_BUTTON);
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }
  if (millis() - lastDebounceTime > DEBOUNCE_MS && reading != stableButtonState) {
    stableButtonState = reading;
    if (stableButtonState == LOW) { // pressed
      pedestrianRequested = true;
    }
  }
  lastButtonReading = reading;
}

void setup() {
  const uint8_t outputs[] = {
    PIN_NS_RED, PIN_NS_YELLOW, PIN_NS_GREEN,
    PIN_EW_RED, PIN_EW_YELLOW, PIN_EW_GREEN,
    PIN_PED_RED, PIN_PED_GREEN, PIN_BUZZER
  };
  for (uint8_t i = 0; i < sizeof(outputs); i++) pinMode(outputs[i], OUTPUT);
  pinMode(PIN_WALK_BUTTON, INPUT_PULLUP);

  enterState(NS_GREEN);
}

void loop() {
  pollWalkButton();
  unsigned long elapsed = millis() - stateEnteredAt;

  switch (state) {
    case NS_GREEN:
      if (elapsed >= T_GREEN) enterState(NS_YELLOW);
      break;

    case NS_YELLOW:
      if (elapsed >= T_YELLOW) enterState(ALL_RED_BEFORE_EW);
      break;

    case ALL_RED_BEFORE_EW:
      if (elapsed >= T_ALL_RED) {
        enterState(pedestrianRequested ? PED_WALK : EW_GREEN);
      }
      break;

    case PED_WALK:
      if (elapsed % PED_BEEP_PERIOD < PED_BEEP_PERIOD / 2) tone(PIN_BUZZER, 1000);
      else noTone(PIN_BUZZER);
      if (elapsed >= T_PED_WALK) enterState(PED_FLASH);
      break;

    case PED_FLASH: {
      bool flashOn = (elapsed / PED_FLASH_PERIOD) % 2 == 0;
      digitalWrite(PIN_PED_GREEN, flashOn);
      if (flashOn) tone(PIN_BUZZER, 1800); else noTone(PIN_BUZZER);
      if (elapsed >= T_PED_FLASH) enterState(ALL_RED_AFTER_PED);
      break;
    }

    case ALL_RED_AFTER_PED:
      if (elapsed >= T_ALL_RED) enterState(EW_GREEN);
      break;

    case EW_GREEN:
      if (elapsed >= T_GREEN) enterState(EW_YELLOW);
      break;

    case EW_YELLOW:
      if (elapsed >= T_YELLOW) enterState(ALL_RED_BEFORE_NS);
      break;

    case ALL_RED_BEFORE_NS:
      if (elapsed >= T_ALL_RED) enterState(NS_GREEN);
      break;
  }
}
