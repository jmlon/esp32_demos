// DS3231 real-time clock (date/time + temperature) + BMP180 (temperature/pressure/altitude) + ENSS160 (VOC/CO2) + AHT21 (temperature/humidity) over I2C
//
// Board: Generic ESP8266 Module (ESP8266EX)
//
// Wiring (ESP8266): SDA -> GPIO4/D2, SCL -> GPIO5/D1, both sensors on the same bus.
//
// Required libraries:
//   - RTClib by Adafruit
//   - Adafruit BMP085 (also supports the BMP180)
//   - ENS160 - Adafruit Fork (ScioSense_ENS160)
//   - Adafruit AHTX0

#include <Wire.h>
#include "RTClib.h"
#include <Adafruit_BMP085.h>
#include <Adafruit_AHTX0.h>
#include "ScioSense_ENS160.h"

// I2C pins for ESP8266 (Wire.begin(SDA, SCL) mapping)
#define I2C_SDA 4   // GPIO4 = D2
#define I2C_SCL 5   // GPIO5 = D1
// I2C device addresses
#define ENS160_I2C_ADDR 0x53
// AHT21 is strictly 0x38
#define AHT21_I2C_ADDR 0x38
// DS3231 is strictly 0x68
#define DS3231_I2C_ADDR 0x68
// BMP180 is strictly 0x77
#define BMP180_I2C_ADDR 0x77

// User configuration - modify these for your location
#define SEALEVEL_PRESSURE_PA 101500  // Adjust to local sea level pressure in Pascals (Medellin 101500)
#define READ_INTERVAL_MS 5000


RTC_DS3231 rtc;
Adafruit_BMP085 bmp;
Adafruit_AHTX0 aht;
ScioSense_ENS160 ens160(ENS160_I2C_ADDR);

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

  // Initialize AHT21
  if (!aht.begin(&Wire)) {
    Serial.println("ERROR: Could not find AHT21 sensor!");
    while (1) delay(100);
  }
  Serial.println("OK: AHT21 initialized successfully.");

  // Initialize ENS160
  if (!ens160.begin()) {
    Serial.println("ERROR: Could not find ENS160 sensor! Check address (0x53 or 0x52).");
    while (1) delay(100); // TODO: Disable sensor instead of blocking program
  }
  // Set ENS160 to standard operating mode
  if (!ens160.setMode(ENS160_OPMODE_STD)) {
    Serial.println("WARNING: ENS160 setMode(STD) failed; readings may be stale.");
  }
  Serial.println("OK: ENS160 initialized successfully.");
  Serial.println("Note: Metal-oxide sensors require ~1 hour warm-up for peak accuracy.");


}

void loop() {
  // Fetch the current snapshot of date and time from the module
  DateTime now = rtc.now();

  // Print Date/Time and RTC temperature (crystal oscillator sensor)
  Serial.printf("RTC    : %04d/%02d/%02d (%s) %02d:%02d:%02d\n",
                now.year(), now.month(), now.day(),
                daysOfTheWeek[now.dayOfTheWeek()],
                now.hour(), now.minute(), now.second());
  Serial.printf("RTC    : Temp: %.1f C\n", rtc.getTemperature());

  // Read and print BMP180 data
  float bmp180temp = bmp.readTemperature();
  float bmp180pressure = bmp.readPressure() / 100.0F; // Convert Pa to hPa, 1hPa = 100Pa = 1mbar
  float bmp180altitude = bmp.readAltitude(SEALEVEL_PRESSURE_PA);
  Serial.printf("BMP180 : Temp: %.1f C  | Pressure: %.2f hPa | Approx Altitude: %.2f m\n", bmp180temp, bmp180pressure, bmp180altitude);

  // Read and print AHT21 data
  sensors_event_t humidityEvent;
  sensors_event_t temperatureEvent;
  aht.getEvent(&humidityEvent, &temperatureEvent);
  float tempC = temperatureEvent.temperature;
  float rh = humidityEvent.relative_humidity;
  Serial.printf("AHT21  : Temp: %.1f C  |  Humidity: %.1f %%\n", tempC, rh);

  // Read and print ENS160 data
  ens160.set_envdata(tempC, rh);
  if (ens160.measure(false)) {
    uint8_t aqi = ens160.getAQI();          // Air Quality Index (1-5)
    uint16_t tvoc = ens160.getTVOC();       // TVOC in ppb (low=0ppb, high=1000ppb)
    uint16_t eco2 = ens160.geteCO2();       // eCO2 in ppm (low=400ppm, high=6000ppm)
    Serial.printf("ENS160 : AQI: %d (1-5)  |  TVOC: %d ppb  |  eCO2: %d ppm\n", aqi, tvoc, eco2);
  }

  Serial.println();
  delay(READ_INTERVAL_MS); // Read every interval
}
