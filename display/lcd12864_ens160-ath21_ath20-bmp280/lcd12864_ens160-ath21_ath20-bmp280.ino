// Sensor dashboard on a 128x64 ST7920 graphic LCD
//
// ENS160 (eCO2/TVOC/AQI) + AHT2x (temperature/humidity) + BMP280 (pressure/altitude)
// over I2C, rendered on an ST7920 128x64 LCD over hardware SPI.
//
// Board: ESP32C3 Dev Module
// USB CDC On Boot: Enabled
// Upload Mode: UART0/Hardware CDC
// USB Mode: Hardware CDC and JTAG
//
// Wiring (ESP32-C3):
//   I2C sensors: SDA -> GPIO8, SCL -> GPIO9 (ENS160, AHT2x, BMP280 all share this bus)
//   ST7920 LCD:  SCK -> GPIO6, MOSI -> GPIO7, CS -> GPIO5, RESET -> GPIO4 (hardware SPI,
//                PSB tied to GND to select serial mode; see display/lcd12864/lcd12864.ino)
//
// Only ONE AHT2x sensor is used: the AHT21/AHT20 is a fixed 0x38 I2C address with no
// address-select pin, so a second one cannot coexist on the same bus. Its temperature
// and humidity reading is shared between the ENS160 compensation input and the display.
//
// Required libraries:
//   - ENS160 - Adafruit Fork (ScioSense_ENS160)
//   - Adafruit AHTX0
//   - Adafruit BMP280
//   - U8g2

#include <Wire.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include "ScioSense_ENS160.h"

// I2C pins for ESP32-C3 (default SDA=21/SCL=22 are unavailable on this board)
#define I2C_SDA 8
#define I2C_SCL 9

// ST7920 hardware SPI pins
#define LCD_SCK 6
#define LCD_MOSI 7
#define LCD_CS 5
#define LCD_RESET 4

// I2C addresses
#define ENS160_I2C_ADDR 0x53      // ENS160 defaults to 0x53 (sometimes 0x52)
#define BMP280_ADDR_PRIMARY 0x77
#define BMP280_ADDR_SECONDARY 0x76

const unsigned long SENSOR_INTERVAL = 2000; // Poll sensors every 2 seconds
const float SEA_LEVEL_HPA = 1013.25;        // Adjust to local sea-level pressure for accurate altitude

Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;
ScioSense_ENS160 ens160(ENS160_I2C_ADDR);
U8G2_ST7920_128X64_F_HW_SPI u8g2(U8G2_R0, /* CS=*/ LCD_CS, /* reset=*/ LCD_RESET);

unsigned long lastSensorRead = 0;

// Last known-good readings, held between polls so the display doesn't blank
// out on a transient I2C hiccup.
float tempC = 0, humidity = 0;
float pressureHPa = 0, altitudeM = 0;
uint8_t aqi = 0;
uint16_t tvoc = 0, eco2 = 0;
bool haveAirQuality = false;

void drawHeader(const char *title) {
  u8g2.drawBox(0, 0, 128, 13);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(4, 10, title);
  u8g2.setDrawColor(1);
}

void drawDashboard() {
  drawHeader("SENSOR DASHBOARD");
  u8g2.setFont(u8g2_font_6x10_tf);

  // Font is 6px/char; keep each line under ~20 chars so it fits the 128px width.
  char line[24];
  snprintf(line, sizeof(line), "T:%.1fC RH:%.0f%%", tempC, humidity);
  u8g2.drawStr(2, 24, line);

  snprintf(line, sizeof(line), "P:%.0fhPa A:%.0fm", pressureHPa, altitudeM);
  u8g2.drawStr(2, 36, line);

  if (haveAirQuality) {
    snprintf(line, sizeof(line), "AQI:%d TVOC:%dppb", aqi, tvoc);
    u8g2.drawStr(2, 48, line);
    snprintf(line, sizeof(line), "eCO2:%dppm", eco2);
    u8g2.drawStr(2, 60, line);
  } else {
    u8g2.drawStr(2, 48, "Air quality: warming up");
  }
}

void readSensors() {
  sensors_event_t humidity_event, temp_event;
  if (!aht.getEvent(&humidity_event, &temp_event)) {
    Serial.println("WARNING: AHT2x read failed, keeping last reading.");
    return;
  }
  tempC = temp_event.temperature;
  humidity = humidity_event.relative_humidity;

  pressureHPa = bmp.readPressure() / 100.0F;
  altitudeM = bmp.readAltitude(SEA_LEVEL_HPA);

  // Feed environmental data to ENS160 for internal compensation
  ens160.set_envdata(tempC, humidity);

  // measure(false) polls the status register once instead of blocking until
  // new data is flagged, so a stalled sensor can't hang the loop.
  haveAirQuality = ens160.measure(false);
  if (haveAirQuality) {
    aqi = ens160.getAQI();
    tvoc = ens160.getTVOC();
    eco2 = ens160.geteCO2();
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000); // Wait for Serial Monitor, but don't hang forever without one

  Serial.println("\n--- ESP32-C3: ENS160 + AHT2x + BMP280 Dashboard ---");

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!aht.begin(&Wire)) {
    Serial.println("ERROR: Could not find AHT2x sensor!");
    while (1) delay(100);
  }
  Serial.println("OK: AHT2x initialized successfully.");

  if (!bmp.begin(BMP280_ADDR_PRIMARY)) {
    Serial.println("Could not find BMP280 at 0x77, trying 0x76...");
    if (!bmp.begin(BMP280_ADDR_SECONDARY)) {
      Serial.println("ERROR: Could not find BMP280 sensor, check wiring!");
      while (1) delay(100);
    }
  }
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                  Adafruit_BMP280::SAMPLING_X2,
                  Adafruit_BMP280::SAMPLING_X16,
                  Adafruit_BMP280::FILTER_X16,
                  Adafruit_BMP280::STANDBY_MS_500);
  Serial.println("OK: BMP280 initialized successfully.");

  if (!ens160.begin()) {
    Serial.println("ERROR: Could not find ENS160 sensor! Check address (0x53 or 0x52).");
    while (1) delay(100);
  }
  if (!ens160.setMode(ENS160_OPMODE_STD)) {
    Serial.println("WARNING: ENS160 setMode(STD) failed; readings may be stale.");
  }
  Serial.println("OK: ENS160 initialized successfully.");
  Serial.println("Note: Metal-oxide sensors require ~1 hour warm-up for peak accuracy.");

  SPI.begin(LCD_SCK, -1, LCD_MOSI, LCD_CS);
  u8g2.setBusClock(600000); // 600 kHz stable SPI speed
  u8g2.begin();
}

void loop() {
  if (millis() - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = millis();
    readSensors();
  }

  u8g2.clearBuffer();
  drawDashboard();
  u8g2.sendBuffer();
}
