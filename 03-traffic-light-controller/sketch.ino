/*
 * Traffic Light + Pedestrian Crossing Controller — hardened revision
 *
 * A four-way intersection (North-South vs East-West) with an on-demand
 * pedestrian crossing phase. Entirely non-blocking: the whole thing is
 * one finite state machine driven off millis(), so a pedestrian button
 * press is never missed while a light is "busy" waiting on delay().
 *
 * Press the WALK button at any time; the request is latched and served
 * at the next safe all-red gap between the NS and EW green phases.
 *
 * Hardening changes vs. the first version (see README "Production
 * Hardening" section for full rationale, including what this CANNOT
 * fix without dedicated hardware):
 *   - Boots into a flashing-all-red "power interruption" state before
 *     ever showing a green, instead of assuming NS_GREEN is safe to
 *     start in regardless of what was happening before power was lost.
 *   - setLights() now refuses to energize NS and EW green/yellow at
 *     the same time, as a last-resort software check — see README for
 *     why this is NOT a substitute for a real independent hardware
 *     conflict monitor.
 *   - A watchdog resets the MCU if the main loop ever hangs.
 *   - Boot count and detected-conflict count are persisted in EEPROM
 *     as a minimal field diagnostic trail.
 *   - Every state transition is logged to Serial.
 */

#include <avr/wdt.h>
#include <EEPROM.h>

enum State {
  STARTUP_FLASH,
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

const char* stateName(State s) {
  switch (s) {
    case STARTUP_FLASH: return "STARTUP_FLASH";
    case NS_GREEN: return "NS_GREEN";
    case NS_YELLOW: return "NS_YELLOW";
    case ALL_RED_BEFORE_EW: return "ALL_RED_BEFORE_EW";
    case PED_WALK: return "PED_WALK";
    case PED_FLASH: return "PED_FLASH";
    case ALL_RED_AFTER_PED: return "ALL_RED_AFTER_PED";
    case EW_GREEN: return "EW_GREEN";
    case EW_YELLOW: return "EW_YELLOW";
    case ALL_RED_BEFORE_NS: return "ALL_RED_BEFORE_NS";
  }
  return "UNKNOWN";
}

// NS light pins
const uint8_t PIN_NS_RED = 2, PIN_NS_YELLOW = 3, PIN_NS_GREEN = 4;
// EW light pins
const uint8_t PIN_EW_RED = 5, PIN_EW_YELLOW = 6, PIN_EW_GREEN = 7;
// Pedestrian light pins
const uint8_t PIN_PED_RED = 8, PIN_PED_GREEN = 9;
const uint8_t PIN_WALK_BUTTON = 10;
const uint8_t PIN_BUZZER = 11;

const unsigned long T_STARTUP_FLASH = 5000; // all-way caution period after any (re)boot
const unsigned long T_GREEN = 6000;
const unsigned long T_YELLOW = 1500;
const unsigned long T_ALL_RED = 500;
const unsigned long T_PED_WALK = 3000;
const unsigned long T_PED_FLASH = 2000;
const unsigned long PED_FLASH_PERIOD = 250;
const unsigned long PED_BEEP_PERIOD = 300;
const unsigned long STARTUP_FLASH_PERIOD = 500;

const int EEPROM_ADDR_BOOT_COUNT = 0;     // uint16_t
const int EEPROM_ADDR_CONFLICT_COUNT = 2; // uint16_t

State state = STARTUP_FLASH;
unsigned long stateEnteredAt = 0;
bool pedestrianRequested = false;

bool lastButtonReading = HIGH;
unsigned long lastDebounceTime = 0;
bool stableButtonState = HIGH;
const unsigned long DEBOUNCE_MS = 40;

uint16_t conflictCount = 0;

// Last-resort software guard: refuses to energize a genuinely conflicting
// combination of lights. This catches corrupted/miscomputed state before
// it reaches the hardware — it does NOT catch a welded relay, a burnt-out
// bulb, or (critically) a systematic bug in the logic that decided these
// arguments in the first place, since that bug lives in this same file.
// A real deployed intersection needs a physically separate conflict
// monitor for that; see README.
void setLights(bool nsR, bool nsY, bool nsG, bool ewR, bool ewY, bool ewG, bool pedR, bool pedG) {
  bool nsActive = nsY || nsG;
  bool ewActive = ewY || ewG;
  if (nsActive && ewActive) {
    conflictCount++;
    EEPROM.put(EEPROM_ADDR_CONFLICT_COUNT, conflictCount);
    Serial.println("FATAL: conflicting light combination blocked, forcing all-red and halting for watchdog reset");
    digitalWrite(PIN_NS_RED, HIGH); digitalWrite(PIN_NS_YELLOW, LOW); digitalWrite(PIN_NS_GREEN, LOW);
    digitalWrite(PIN_EW_RED, HIGH); digitalWrite(PIN_EW_YELLOW, LOW); digitalWrite(PIN_EW_GREEN, LOW);
    digitalWrite(PIN_PED_RED, HIGH); digitalWrite(PIN_PED_GREEN, LOW);
    while (true) { /* intentionally halt; wdt_reset() is never called again, so the watchdog reboots us */ }
  }

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
  Serial.print("-> "); Serial.println(stateName(s));

  switch (s) {
    case STARTUP_FLASH:        setLights(1,0,0, 1,0,0, 1,0); noTone(PIN_BUZZER); break; // red solid; flash pattern handled in loop()
    case NS_GREEN:              setLights(0,0,1, 1,0,0, 1,0); noTone(PIN_BUZZER); break;
    case NS_YELLOW:              setLights(0,1,0, 1,0,0, 1,0); noTone(PIN_BUZZER); break;
    case ALL_RED_BEFORE_EW:    setLights(1,0,0, 1,0,0, 1,0); noTone(PIN_BUZZER); break;
    case PED_WALK:                setLights(1,0,0, 1,0,0, 0,1); pedestrianRequested = false; break;
    case PED_FLASH:                setLights(1,0,0, 1,0,0, 0,0); break; // green toggled in loop()
    case ALL_RED_AFTER_PED:    setLights(1,0,0, 1,0,0, 1,0); noTone(PIN_BUZZER); break;
    case EW_GREEN:                setLights(1,0,0, 0,0,1, 1,0); noTone(PIN_BUZZER); break;
    case EW_YELLOW:                setLights(1,0,0, 0,1,0, 1,0); noTone(PIN_BUZZER); break;
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
  Serial.begin(9600);

  const uint8_t outputs[] = {
    PIN_NS_RED, PIN_NS_YELLOW, PIN_NS_GREEN,
    PIN_EW_RED, PIN_EW_YELLOW, PIN_EW_GREEN,
    PIN_PED_RED, PIN_PED_GREEN, PIN_BUZZER
  };
  for (uint8_t i = 0; i < sizeof(outputs); i++) pinMode(outputs[i], OUTPUT);
  pinMode(PIN_WALK_BUTTON, INPUT_PULLUP);

  uint16_t bootCount = 0;
  EEPROM.get(EEPROM_ADDR_BOOT_COUNT, bootCount);
  if (bootCount == 0xFFFF) bootCount = 0; // unwritten EEPROM
  bootCount++;
  EEPROM.put(EEPROM_ADDR_BOOT_COUNT, bootCount);

  EEPROM.get(EEPROM_ADDR_CONFLICT_COUNT, conflictCount);
  if (conflictCount == 0xFFFF) conflictCount = 0;

  Serial.print("Boot #"); Serial.print(bootCount);
  Serial.print(" | logged conflicts: "); Serial.println(conflictCount);

  // Every boot — whether first power-up or recovery from a brownout mid
  // EW-green with cars in the intersection — starts here, never straight
  // into a green. See README for why this matters.
  enterState(STARTUP_FLASH);

  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();

  pollWalkButton();
  unsigned long elapsed = millis() - stateEnteredAt;

  switch (state) {
    case STARTUP_FLASH: {
      bool flashOn = (elapsed / STARTUP_FLASH_PERIOD) % 2 == 0;
      digitalWrite(PIN_NS_RED, flashOn);
      digitalWrite(PIN_EW_RED, flashOn);
      if (elapsed >= T_STARTUP_FLASH) enterState(NS_GREEN);
      break;
    }

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
