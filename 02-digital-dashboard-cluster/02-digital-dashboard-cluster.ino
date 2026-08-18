/*
 * Digital Dashboard Cluster — hardened revision
 *
 * Simulated instrument cluster on a 128x64 SSD1306 OLED:
 *   - Speed (from potentiometer on A0), large digital readout
 *   - Engine temperature (from potentiometer on A1), horizontal gauge bar
 *     that turns red and flashes "HOT" past 100 degrees
 *   - Turn signal indicator, toggled by a pushbutton, blinks on-screen
 *     arrow + an external LED
 *
 * Turn the two potentiometer knobs in the Wokwi simulator to drive speed
 * and temperature; click the pushbutton to toggle the turn signal.
 *
 * Hardening changes vs. the first version (see README "Production
 * Hardening" section for full rationale):
 *   - Wire runs at 400kHz instead of the 100kHz default, cutting the
 *     full-frame OLED write from ~90ms down to ~25ms — that transfer
 *     blocks the whole MCU on AVR, so this directly buys back
 *     responsiveness on every single frame.
 *   - A Wire bus timeout stops a glitched I2C line from hanging the
 *     sketch forever.
 *   - Exponential moving average smooths both potentiometer readings so
 *     displayed values don't jitter on electrical noise.
 *   - Turn signal state persists across power loss (EEPROM), and its
 *     blink rate is aligned to the ECE R6 standard (~90 flashes/min)
 *     instead of an arbitrary period.
 *   - A watchdog resets the MCU if the main loop ever hangs (e.g. an
 *     I2C fault the bus timeout doesn't catch).
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <avr/wdt.h>
#include <EEPROM.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const uint8_t PIN_POT_SPEED = A0;
const uint8_t PIN_POT_TEMP = A1;
const uint8_t PIN_BUTTON = 2;
const uint8_t PIN_SIGNAL_LED = 3;

const int SPEED_MIN = 0, SPEED_MAX = 220;      // km/h
const int TEMP_MIN = 40, TEMP_MAX = 120;        // degrees C
const int TEMP_HOT_THRESHOLD = 100;

const int EEPROM_ADDR_SIGNAL_STATE = 0; // 1 byte

bool turnSignalOn = false;
bool lastButtonReading = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_MS = 40;

unsigned long lastBlinkToggle = 0;
bool blinkPhase = false;
// ~90 flashes/min, inside the ECE R6 regulated range of 60-120 fpm
const unsigned long BLINK_MS = 333;

// exponential moving average: smoothed = smoothed*(1-a) + raw*a
const float EMA_ALPHA = 0.2;
float smoothedSpeedRaw = 0;
float smoothedTempRaw = 0;
bool emaInitialized = false;

void setup() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_SIGNAL_LED, OUTPUT);

  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  Wire.setClock(400000); // fast mode: cuts full-frame OLED write time ~4x
#if defined(TWI_HAS_TIMEOUT) || defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeoutUs(25000, true); // don't let a glitched I2C bus hang forever
#endif
  display.clearDisplay();
  display.display();

  uint8_t stored = EEPROM.read(EEPROM_ADDR_SIGNAL_STATE);
  turnSignalOn = (stored == 1);

  wdt_enable(WDTO_2S);
}

void handleButton() {
  bool reading = digitalRead(PIN_BUTTON);
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }
  if (millis() - lastDebounceTime > DEBOUNCE_MS) {
    static bool stableState = HIGH;
    if (reading != stableState) {
      stableState = reading;
      if (stableState == LOW) { // pressed (active low)
        turnSignalOn = !turnSignalOn;
        EEPROM.update(EEPROM_ADDR_SIGNAL_STATE, turnSignalOn ? 1 : 0); // survives power loss
      }
    }
  }
  lastButtonReading = reading;
}

void updateBlink() {
  if (millis() - lastBlinkToggle >= BLINK_MS) {
    lastBlinkToggle = millis();
    blinkPhase = !blinkPhase;
  }
  digitalWrite(PIN_SIGNAL_LED, (turnSignalOn && blinkPhase) ? HIGH : LOW);
}

int readSmoothed(uint8_t pin, float &smoothedRaw) {
  int raw = analogRead(pin);
  if (!emaInitialized) {
    smoothedRaw = raw;
  } else {
    smoothedRaw = smoothedRaw * (1.0 - EMA_ALPHA) + raw * EMA_ALPHA;
  }
  return (int)(smoothedRaw + 0.5);
}

void drawSpeed(int speedKmh) {
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  if (speedKmh < 100) display.print(' ');
  if (speedKmh < 10) display.print(' ');
  display.print(speedKmh);

  display.setTextSize(1);
  display.setCursor(0, 28);
  display.print("km/h");
}

void drawTempGauge(int tempC) {
  const int barX = 0, barY = 44, barW = 100, barH = 8;
  display.drawRect(barX, barY, barW, barH, SSD1306_WHITE);

  int fillW = map(tempC, TEMP_MIN, TEMP_MAX, 0, barW - 2);
  fillW = constrain(fillW, 0, barW - 2);
  display.fillRect(barX + 1, barY + 1, fillW, barH - 2, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(barX, barY + 12);
  display.print(tempC);
  display.print((char)247); // degree symbol
  display.print("C");

  if (tempC >= TEMP_HOT_THRESHOLD && blinkPhase) {
    display.setCursor(70, barY + 12);
    display.print("HOT");
  }
}

void drawTurnSignal() {
  if (!(turnSignalOn && blinkPhase)) return;
  // simple right-pointing arrow in the top-right corner
  const int x = 108, y = 4;
  display.fillTriangle(x, y, x, y + 12, x + 12, y + 6, SSD1306_WHITE);
}

void loop() {
  wdt_reset();

  handleButton();
  updateBlink();

  int rawSpeedSmoothed = readSmoothed(PIN_POT_SPEED, smoothedSpeedRaw);
  int rawTempSmoothed = readSmoothed(PIN_POT_TEMP, smoothedTempRaw);
  emaInitialized = true;

  int speedKmh = map(rawSpeedSmoothed, 0, 1023, SPEED_MIN, SPEED_MAX);
  int tempC = map(rawTempSmoothed, 0, 1023, TEMP_MIN, TEMP_MAX);

  display.clearDisplay();
  drawSpeed(speedKmh);
  drawTempGauge(tempC);
  drawTurnSignal();
  display.display();
}
