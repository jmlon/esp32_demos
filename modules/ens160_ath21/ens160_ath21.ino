// ESP32C3 Dev Module

// Libraries:
// - ENS160 - Adafruit Fork
// - Adafruit AHTX0

#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include "ScioSense_ENS160.h"

// Define I2C pins for ESP32-C3
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
    Serial.println("❌ Error: Could not find AHT21 sensor!");
    while (1) delay(100);
  }
  Serial.println("✅ AHT21 initialized successfully.");

  // Initialize ENS160
  if (!ens160.begin()) {
    Serial.println("❌ Error: Could not find ENS160 sensor! Check address (0x53 or 0x52).");
    while (1) delay(100);
  }
  
  // Set ENS160 to standard operating mode
  ens160.setMode(ENS160_OPMODE_STD);
  Serial.println("✅ ENS160 initialized successfully.");
  Serial.println("Note: Metal-oxide sensors require ~1 hour warm-up for peak accuracy.");
}

void loop() {
  // Non-blocking periodic reader
  if (millis() - lastMeasurementTime >= MEASURE_INTERVAL) {
    lastMeasurementTime = millis();

    // 1. Fetch Temperature & Humidity from AHT21
    sensors_event_t humidity_event, temp_event;
    aht.getEvent(&humidity_event, &temp_event);

    float tempC = temp_event.temperature;
    float rh = humidity_event.relative_humidity;

    // 2. Feed environmental data to ENS160 for internal compensation calibration
    ens160.set_envdata(tempC, rh);

    // 3. Query ENS160 state and read Air Quality metrics
    ens160.measure(true);
    
    uint8_t aqi = ens160.getAQI();          // Air Quality Index (1-5)
    uint16_t tvoc = ens160.getTVOC();       // TVOC in ppb
    uint16_t eco2 = ens160.geteCO2();       // eCO2 in ppm

    // 4. Output to Serial Monitor
    Serial.println("-------------------------------------------");
    Serial.printf("🌡️  Temp: %.1f °C  |  💧 Humidity: %.1f %%\n", tempC, rh);
    Serial.printf("🍃 AQI: %d (1-5)    |  🧪 TVOC: %d ppb  |  ☁️  eCO2: %d ppm\n", aqi, tvoc, eco2);
  }
}

