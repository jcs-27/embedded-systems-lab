/*
 * Keypad Door Lock
 *
 * A 4x4 matrix keypad guards a servo-actuated latch. Enter the 4-digit
 * PIN and press '#' to submit, '*' to clear what you've typed so far.
 * Get it right: the latch swings open, the green LED lights, and it
 * re-locks itself automatically after a few seconds. Get it wrong three
 * times: the lock goes into a timed lockout with a flashing red LED and
 * an alarm tone, and the lockout is logged to EEPROM.
 *
 * Default PIN: 1234
 */

#include <Keypad.h>
#include <Servo.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include <string.h>

const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {2, 3, 4, 5};
byte colPins[COLS] = {6, 7, 8, 9};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

const uint8_t PIN_SERVO = 10;
const uint8_t PIN_LED_GREEN = 11;
const uint8_t PIN_LED_RED = 12;
const uint8_t PIN_BUZZER = A0;

const char CORRECT_PIN[5] = "1234"; // change this to set your own code
const uint8_t MAX_ATTEMPTS = 3;
const unsigned long UNLOCK_HOLD_MS = 5000;   // how long the latch stays open
const unsigned long LOCKOUT_MS = 15000;      // penalty box duration after 3 wrong tries
const unsigned long LOCKOUT_FLASH_MS = 200;

const int SERVO_LOCKED_DEG = 0;
const int SERVO_UNLOCKED_DEG = 90;

const int EEPROM_ADDR_LOCKOUT_COUNT = 0; // uint16_t

enum LockState { LOCKED, UNLOCKED, LOCKOUT };
LockState state = LOCKED;
unsigned long stateEnteredAt = 0;

Servo latch;
char enteredPin[5]; // 4 digits + null terminator
uint8_t pinLen = 0;
uint8_t wrongAttempts = 0;
uint16_t lockoutCount = 0;

void resetEntry() {
  pinLen = 0;
  enteredPin[0] = '\0';
}

void beep(unsigned int freq, unsigned int durationMs) {
  tone(PIN_BUZZER, freq, durationMs);
}

void enterState(LockState s) {
  state = s;
  stateEnteredAt = millis();

  switch (s) {
    case LOCKED:
      latch.write(SERVO_LOCKED_DEG);
      digitalWrite(PIN_LED_GREEN, LOW);
      digitalWrite(PIN_LED_RED, LOW);
      resetEntry();
      break;
    case UNLOCKED:
      latch.write(SERVO_UNLOCKED_DEG);
      digitalWrite(PIN_LED_GREEN, HIGH);
      digitalWrite(PIN_LED_RED, LOW);
      beep(1500, 150);
      resetEntry();
      break;
    case LOCKOUT:
      latch.write(SERVO_LOCKED_DEG);
      digitalWrite(PIN_LED_GREEN, LOW);
      beep(300, 500);
      lockoutCount++;
      EEPROM.put(EEPROM_ADDR_LOCKOUT_COUNT, lockoutCount);
      resetEntry();
      break;
  }
}

void handleKey(char key) {
  if (state == LOCKOUT) return; // ignore all input during lockout

  if (key >= '0' && key <= '9') {
    if (pinLen < 4) {
      enteredPin[pinLen++] = key;
      enteredPin[pinLen] = '\0';
      beep(800, 40);
    }
    return;
  }

  if (key == '*') { // clear
    resetEntry();
    beep(400, 60);
    return;
  }

  if (key == '#') { // submit
    if (pinLen == 4 && strcmp(enteredPin, CORRECT_PIN) == 0) {
      wrongAttempts = 0;
      enterState(UNLOCKED);
    } else {
      wrongAttempts++;
      resetEntry();
      if (wrongAttempts >= MAX_ATTEMPTS) {
        enterState(LOCKOUT);
      } else {
        beep(300, 250); // short error buzz, stays LOCKED
      }
    }
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  latch.attach(PIN_SERVO);

  EEPROM.get(EEPROM_ADDR_LOCKOUT_COUNT, lockoutCount);
  if (lockoutCount == 0xFFFF) lockoutCount = 0;
  Serial.print("Boot OK. Lifetime lockout count: ");
  Serial.println(lockoutCount);

  enterState(LOCKED);
  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();
  unsigned long elapsed = millis() - stateEnteredAt;

  char key = keypad.getKey();
  if (key) handleKey(key);

  switch (state) {
    case LOCKED:
      // nothing time-based; waiting on keypad input
      break;

    case UNLOCKED:
      if (elapsed >= UNLOCK_HOLD_MS) enterState(LOCKED);
      break;

    case LOCKOUT: {
      bool flashOn = (elapsed / LOCKOUT_FLASH_MS) % 2 == 0;
      digitalWrite(PIN_LED_RED, flashOn);
      if (elapsed >= LOCKOUT_MS) enterState(LOCKED);
      break;
    }
  }
}
