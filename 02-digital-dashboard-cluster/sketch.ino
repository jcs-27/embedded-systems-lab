/*
 * Digital Dashboard Cluster
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
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

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

bool turnSignalOn = false;
bool lastButtonReading = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_MS = 40;

unsigned long lastBlinkToggle = 0;
bool blinkPhase = false;
const unsigned long BLINK_MS = 400;

void setup() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_SIGNAL_LED, OUTPUT);

  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.clearDisplay();
  display.display();
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
  handleButton();
  updateBlink();

  int rawSpeed = analogRead(PIN_POT_SPEED);
  int rawTemp = analogRead(PIN_POT_TEMP);
  int speedKmh = map(rawSpeed, 0, 1023, SPEED_MIN, SPEED_MAX);
  int tempC = map(rawTemp, 0, 1023, TEMP_MIN, TEMP_MAX);

  display.clearDisplay();
  drawSpeed(speedKmh);
  drawTempGauge(tempC);
  drawTurnSignal();
  display.display();

  delay(20);
}
