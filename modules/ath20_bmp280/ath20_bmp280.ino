// AHT20 (temperature/humidity) + BMP280 (temperature/pressure/altitude) over I2C
//
// Board: ESP32C3 Dev Module
// USB CDC On Boot: Enabled
// Upload Mode: UART0/Hardware CDC
// USB Mode: Hardware CDC and JTAG
//
// Wiring (ESP32-C3): SDA -> GPIO8, SCL -> GPIO9, both sensors on the same bus.
//
// Required libraries:
//   - Adafruit AHTX0
//   - Adafruit BMP280

#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>

// I2C pins for ESP32-C3 (default SDA=21/SCL=22 are unavailable on this board)
#define I2C_SDA 8
#define I2C_SCL 9

// BMP280 I2C address depends on the SDO pin strap
#define BMP280_ADDR_PRIMARY 0x77
#define BMP280_ADDR_SECONDARY 0x76

Adafruit_AHTX0 aht;  // no internal heating
Adafruit_BMP280 bmp; // Uses I2C by default, has internal heating

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000); // Wait for Serial Monitor, but don't hang forever without one

  Serial.println(F("ESP32-C3 Dual Sensor Test (AHT20 + BMP280)"));

  // Initialize I2C with ESP32-C3 pins
  Wire.begin(I2C_SDA, I2C_SCL);

  // Initialize AHT20 (Default Address: 0x38)
  if (!aht.begin()) {
    Serial.println(F("Could not find a valid AHT20 sensor, check wiring!"));
    while (1) delay(10);
  }
  Serial.println(F("AHT20 initialized successfully."));

  // Initialize BMP280; try the primary address first, then the secondary
  if (!bmp.begin(BMP280_ADDR_PRIMARY)) {
    Serial.println(F("Could not find a valid BMP280 sensor at 0x77, trying 0x76..."));
    if (!bmp.begin(BMP280_ADDR_SECONDARY)) {
      Serial.println(F("Could not find a valid BMP280 sensor at 0x76, check wiring!"));
      while (1) delay(10);
    }
  }
  Serial.println(F("BMP280 initialized successfully."));

  /* Default settings for the BMP280 indoor navigation profile */
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */
}

void loop() {
  // Read AHT20 Data
  sensors_event_t humidity, temp_aht;
  if (!aht.getEvent(&humidity, &temp_aht)) {
    Serial.println(F("WARNING: AHT20 read failed, skipping this cycle."));
    delay(2000);
    return;
  }

  // Read BMP280 Data
  float temp_bmp = bmp.readTemperature();
  float pressure = bmp.readPressure() / 100.0F; // Convert Pa to hPa
  float altitude = bmp.readAltitude(1013.25);   // Adjust 1013.25 to your local sea level pressure

  // Print Results
  Serial.println(F("--- Sensor Readings ---"));
  
  Serial.print(F("AHT20 Temp: "));
  Serial.print(temp_aht.temperature);
  Serial.println(F(" °C"));
  
  Serial.print(F("AHT20 Humidity: "));
  Serial.print(humidity.relative_humidity);
  Serial.println(F(" %"));

  Serial.print(F("BMP280 Temp: "));
  Serial.print(temp_bmp);
  Serial.println(F(" °C"));

  Serial.print(F("BMP280 Pressure: "));
  Serial.print(pressure);
  Serial.println(F(" hPa"));

  Serial.print(F("Approx Altitude: "));
  Serial.print(altitude);
  Serial.println(F(" m"));

  Serial.println();
  delay(2000); // Read every 2 seconds
}
