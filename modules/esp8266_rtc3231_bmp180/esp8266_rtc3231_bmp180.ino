// DS3231 real-time clock (date/time + temperature) + BMP180 (temperature/pressure/altitude) over I2C
//
// Board: Generic ESP8266 Module (ESP8266EX)
//
// Wiring (ESP8266): SDA -> GPIO4/D2, SCL -> GPIO5/D1, both sensors on the same bus.
//
// Required libraries:
//   - RTClib by Adafruit
//   - Adafruit BMP085 (also supports the BMP180)

#include <Wire.h>
#include "RTClib.h"
#include <Adafruit_BMP085.h>

// I2C pins for ESP8266 (Wire.begin(SDA, SCL) mapping)
#define I2C_SDA 4   // GPIO4 = D2
#define I2C_SCL 5   // GPIO5 = D1

// User configuration - modify these for your location
#define SEALEVEL_PRESSURE_PA 101500  // Adjust to local sea level pressure in Pascals
#define READ_INTERVAL_MS 1000


RTC_DS3231 rtc;
Adafruit_BMP085 bmp; // BMP180 uses the same driver as the BMP085, fixed I2C address 0x77

// Translates DateTime::dayOfTheWeek() (0=Sunday..6=Saturday) to a name
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

void setup() {
  Serial.begin(115200);
  Serial.println("");

  // Initialize I2C with ESP8266 pins
  Wire.begin(I2C_SDA, I2C_SCL);

  // Initialize the DS3231 (Default Address: 0x68)
  if (!rtc.begin()) {
    Serial.println(F("Error: Could not find DS3231 module. Check your wiring!"));
    while (1) delay(10);
  }
  Serial.println(F("DS3231 initialized successfully."));

  // Check if the RTC lost power (e.g. battery removed/drained)
  if (rtc.lostPower()) {
    Serial.println(F("RTC lost power! Synchronizing time with compilation time..."));

    // Automatically sets the RTC to the exact date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

    // To set the time manually instead, uncomment the line below (Year, Month, Day, Hour, Min, Sec):
    // rtc.adjust(DateTime(2026, 8, 5, 21, 30, 0));
  }

  // Uncomment to force a re-sync to compile time even if the RTC didn't report lost power
  // (e.g. the battery is fine but the stored time is stale/wrong):
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  // Initialize the BMP180 (Default Address: 0x77)
  if (!bmp.begin()) {
    Serial.println(F("Error: Could not find BMP180 sensor. Check your wiring!"));
    while (1) delay(10);
  }
  Serial.println(F("BMP180 initialized successfully."));
}

void loop() {
  // Fetch the current snapshot of date and time from the module
  DateTime now = rtc.now();

  // Print Date details
  Serial.print(F("--- RTC Reading ---"));
  Serial.println();

  Serial.print(now.year(), DEC);
  Serial.print('/');
  Serial.print(now.month(), DEC);
  Serial.print('/');
  Serial.print(now.day(), DEC);
  Serial.print(F(" ("));
  Serial.print(daysOfTheWeek[now.dayOfTheWeek()]);
  Serial.print(F(") "));

  // Print Time details
  if (now.hour() < 10) Serial.print('0'); // Leading zero padding
  Serial.print(now.hour(), DEC);
  Serial.print(':');
  if (now.minute() < 10) Serial.print('0');
  Serial.print(now.minute(), DEC);
  Serial.print(':');
  if (now.second() < 10) Serial.print('0');
  Serial.print(now.second(), DEC);
  Serial.println();

  // Print temperature sensor reading from the crystal oscillator
  Serial.print(F("RTC Temperature: "));
  Serial.print(rtc.getTemperature());
  Serial.println(F(" C"));

  // Read and print BMP180 data
  Serial.print(F("BMP180 Temp: "));
  Serial.print(bmp.readTemperature());
  Serial.println(F(" C"));

  Serial.print(F("BMP180 Pressure: "));
  Serial.print(bmp.readPressure() / 100.0F); // Convert Pa to hPa, 1hPa = 100Pa = 1mbar
  Serial.println(F(" hPa"));

  Serial.print(F("Approx Altitude: "));
  Serial.print(bmp.readAltitude(SEALEVEL_PRESSURE_PA)); // Adjust to your local sea level pressure (Pa) (Medellin 101500)
  Serial.println(F(" m"));

  Serial.println();
  delay(READ_INTERVAL_MS); // Read every interval
}
