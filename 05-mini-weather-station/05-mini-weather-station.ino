/*
 * Mini Weather Station
 *
 * DHT22 measures temperature + humidity; a 128x64 OLED shows the current
 * reading plus the all-time min/max (persisted in EEPROM, survives power
 * loss). Three LEDs give an at-a-glance comfort read: green (comfortable),
 * yellow (moderate), red (extreme). Hold the reset button to clear the
 * stored min/max and start tracking fresh.
 *
 * DHT22 needs at least ~2s between reads — that's enforced with a
 * non-blocking millis() timer, not delay(), same as every other project
 * in this lab. A failed read (DHT22s do occasionally miss a beat) is
 * logged as a fault to EEPROM rather than shown as a bogus temperature.
 */

#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <avr/wdt.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

const uint8_t PIN_DHT = 7;
const uint8_t PIN_BUTTON = 8;
const uint8_t PIN_LED_GREEN = 9;
const uint8_t PIN_LED_YELLOW = 10;
const uint8_t PIN_LED_RED = 11;

const unsigned long READ_INTERVAL_MS = 2500; // DHT22 minimum is ~2s; margin included
const unsigned long DEBOUNCE_MS = 40;
const unsigned long RESET_MSG_MS = 1500;
const unsigned long DISPLAY_INTERVAL_MS = 200; // no need to redraw faster than this is legible

const int EEPROM_ADDR_MIN_TEMP = 0;   // float
const int EEPROM_ADDR_MAX_TEMP = 4;   // float
const int EEPROM_ADDR_MIN_HUM = 8;    // float
const int EEPROM_ADDR_MAX_HUM = 12;   // float
const int EEPROM_ADDR_FAULT_COUNT = 16; // uint16_t
const int EEPROM_ADDR_INITIALIZED = 18; // uint8_t sentinel: 0xA5 once real data exists

// Declared up here, ahead of every function, on purpose: the Arduino build
// step auto-generates forward prototypes for every function in the sketch
// and inserts them right after the #include lines — before anything else
// in the file. A function returning `Comfort` would get prototyped before
// this enum existed if it were declared any later, which fails to compile
// with "'Comfort' does not name a type". Compiling this the first time
// caught exactly that.
enum Comfort { COMFY, MODERATE, EXTREME };

DHT dht(PIN_DHT, DHT22);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

float lastTempC = NAN, lastHumidity = NAN;
float minTemp, maxTemp, minHum, maxHum;
uint16_t faultCount = 0;
bool haveGoodReading = false;

unsigned long lastReadAt = 0;
unsigned long lastDrawAt = 0;
unsigned long resetMsgUntil = 0;

bool lastButtonReading = HIGH;
unsigned long lastDebounceTime = 0;
bool stableButtonState = HIGH;

void loadEeprom() {
  uint8_t initialized;
  EEPROM.get(EEPROM_ADDR_INITIALIZED, initialized);
  if (initialized == 0xA5) {
    EEPROM.get(EEPROM_ADDR_MIN_TEMP, minTemp);
    EEPROM.get(EEPROM_ADDR_MAX_TEMP, maxTemp);
    EEPROM.get(EEPROM_ADDR_MIN_HUM, minHum);
    EEPROM.get(EEPROM_ADDR_MAX_HUM, maxHum);
    EEPROM.get(EEPROM_ADDR_FAULT_COUNT, faultCount);
  } else {
    minTemp = minHum = 1000;
    maxTemp = maxHum = -1000;
    faultCount = 0;
  }
}

void saveMinMax() {
  EEPROM.put(EEPROM_ADDR_MIN_TEMP, minTemp);
  EEPROM.put(EEPROM_ADDR_MAX_TEMP, maxTemp);
  EEPROM.put(EEPROM_ADDR_MIN_HUM, minHum);
  EEPROM.put(EEPROM_ADDR_MAX_HUM, maxHum);
  uint8_t marker = 0xA5;
  EEPROM.put(EEPROM_ADDR_INITIALIZED, marker);
}

void resetMinMax() {
  minTemp = lastTempC; maxTemp = lastTempC;
  minHum = lastHumidity; maxHum = lastHumidity;
  saveMinMax();
  resetMsgUntil = millis() + RESET_MSG_MS;
}

void handleButton() {
  bool reading = digitalRead(PIN_BUTTON);
  if (reading != lastButtonReading) lastDebounceTime = millis();
  if (millis() - lastDebounceTime > DEBOUNCE_MS && reading != stableButtonState) {
    stableButtonState = reading;
    if (stableButtonState == LOW && haveGoodReading) {
      resetMinMax();
    }
  }
  lastButtonReading = reading;
}

Comfort classify(float t, float h) {
  bool comfyTemp = t >= 18 && t <= 26;
  bool comfyHum = h >= 30 && h <= 60;
  bool moderateTemp = t >= 15 && t <= 30;
  bool moderateHum = h >= 20 && h <= 70;
  if (comfyTemp && comfyHum) return COMFY;
  if (moderateTemp && moderateHum) return MODERATE;
  return EXTREME;
}

void setComfortLeds(Comfort c) {
  digitalWrite(PIN_LED_GREEN, c == COMFY);
  digitalWrite(PIN_LED_YELLOW, c == MODERATE);
  digitalWrite(PIN_LED_RED, c == EXTREME);
}

void takeReading() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    faultCount++;
    EEPROM.put(EEPROM_ADDR_FAULT_COUNT, faultCount);
    return; // keep displaying the last good reading rather than a bogus one
  }

  lastTempC = t;
  lastHumidity = h;
  haveGoodReading = true;

  bool changed = false;
  if (t < minTemp) { minTemp = t; changed = true; }
  if (t > maxTemp) { maxTemp = t; changed = true; }
  if (h < minHum) { minHum = h; changed = true; }
  if (h > maxHum) { maxHum = h; changed = true; }
  if (changed) saveMinMax();

  setComfortLeds(classify(t, h));
}

void drawDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (millis() < resetMsgUntil) {
    display.setTextSize(1);
    display.setCursor(0, 24);
    display.print("Min/max reset.");
    display.display();
    return;
  }

  if (!haveGoodReading) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Waiting for sensor...");
    display.display();
    return;
  }

  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(lastTempC, 1);
  display.print("C");

  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print("Humidity: ");
  display.print(lastHumidity, 0);
  display.print("%");

  display.setCursor(0, 34);
  display.print("Min "); display.print(minTemp, 1);
  display.print(" Max "); display.print(maxTemp, 1);

  display.setCursor(0, 44);
  display.print("Hum "); display.print(minHum, 0);
  display.print("-"); display.print(maxHum, 0);
  display.print("%");

  display.setCursor(0, 56);
  display.print("Faults: "); display.print(faultCount);

  display.display();
}

void setup() {
  Serial.begin(9600);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);

  dht.begin();
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  Wire.setClock(400000);
  display.clearDisplay();
  display.display();

  loadEeprom();
  Serial.print("Boot OK. Fault count: "); Serial.println(faultCount);

  wdt_enable(WDTO_4S); // DHT22 reads can occasionally run long; give it margin
}

void loop() {
  wdt_reset();
  handleButton();

  unsigned long now = millis();
  if (now - lastReadAt >= READ_INTERVAL_MS) {
    lastReadAt = now;
    takeReading();
  }

  // the OLED write is a ~25ms blocking I2C transfer even at 400kHz; no
  // point paying that cost faster than a human can read the result
  if (now - lastDrawAt >= DISPLAY_INTERVAL_MS) {
    lastDrawAt = now;
    drawDisplay();
  }
}
