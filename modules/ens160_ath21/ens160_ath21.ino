// ENS160 (eCO2/TVOC/AQI) + AHT21 (temperature/humidity) over I2C
//
// Board: ESP32C3 Dev Module
// USB CDC On Boot: Enabled
// Upload Mode: UART0/Hardware CDC
// USB Mode: Hardware CDC and JTAG
//
// Wiring (ESP32-C3): SDA -> GPIO8, SCL -> GPIO9, both sensors on the same bus.
// AHT21 is a fixed-function humidity sensor; ENS160 is a metal-oxide gas
// sensor that uses the AHT21's temperature/humidity reading to compensate
// its own measurements, so both must be initialized before looping.
//
// Required libraries:
//   - ENS160 - Adafruit Fork (ScioSense_ENS160)
//   - Adafruit AHTX0

#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include "ScioSense_ENS160.h"

// I2C pins for ESP32-C3 (default SDA=21/SCL=22 are unavailable on this board)
#define SDA_PIN 8
#define SCL_PIN 9

// I2C Addresses
// AHT21 is strictly 0x38
// ENS160 defaults to 0x53 (sometimes 0x52 depending on module ADDR pad)
#define ENS160_I2C_ADDR 0x53

Adafruit_AHTX0 aht;
ScioSense_ENS160 ens160(ENS160_I2C_ADDR);

unsigned long lastMeasurementTime = 0;
const unsigned long MEASURE_INTERVAL = 3000; // Read every 3 seconds

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000); // Wait for Serial Monitor

  Serial.println("\n--- ESP32-C3: ENS160 + AHT21 Sensor Reader ---");

  // Initialize custom I2C pins on ESP32-C3
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize AHT21
  if (!aht.begin(&Wire)) {
    Serial.println("ERROR: Could not find AHT21 sensor!");
    while (1) delay(100);
  }
  Serial.println("OK: AHT21 initialized successfully.");

  // Initialize ENS160
  if (!ens160.begin()) {
    Serial.println("ERROR: Could not find ENS160 sensor! Check address (0x53 or 0x52).");
    while (1) delay(100);
  }

  // Set ENS160 to standard operating mode
  if (!ens160.setMode(ENS160_OPMODE_STD)) {
    Serial.println("WARNING: ENS160 setMode(STD) failed; readings may be stale.");
  }
  Serial.println("OK: ENS160 initialized successfully.");
  Serial.println("Note: Metal-oxide sensors require ~1 hour warm-up for peak accuracy.");
}

void loop() {
  // Non-blocking periodic reader
  if (millis() - lastMeasurementTime >= MEASURE_INTERVAL) {
    lastMeasurementTime = millis();

    // 1. Fetch Temperature & Humidity from AHT21
    sensors_event_t humidity_event, temp_event;
    if (!aht.getEvent(&humidity_event, &temp_event)) {
      Serial.println("WARNING: AHT21 read failed, skipping this cycle.");
      return;
    }

    float tempC = temp_event.temperature;
    float rh = humidity_event.relative_humidity;

    // 2. Feed environmental data to ENS160 for internal compensation calibration
    ens160.set_envdata(tempC, rh);

    // 3. Query ENS160 state; measure(false) polls the status register once
    // instead of blocking until new data is flagged, so a stalled sensor
    // can't hang the loop.
    if (!ens160.measure(false)) {
      Serial.println("Temp/Humidity ready, air quality data not yet refreshed.");
      Serial.printf("Temp: %.1f C  |  Humidity: %.1f %%\n", tempC, rh);
      return;
    }

    uint8_t aqi = ens160.getAQI();          // Air Quality Index (1-5)
    uint16_t tvoc = ens160.getTVOC();       // TVOC in ppb
    uint16_t eco2 = ens160.geteCO2();       // eCO2 in ppm

    // 4. Output to Serial Monitor
    Serial.println("-------------------------------------------");
    Serial.printf("Temp: %.1f C  |  Humidity: %.1f %%\n", tempC, rh);
    Serial.printf("AQI: %d (1-5)  |  TVOC: %d ppb  |  eCO2: %d ppm\n", aqi, tvoc, eco2);
  }
}
