// DS3231 real-time clock (date/time + temperature) + BMP180 (temperature/pressure/altitude) + ENSS160 (VOC/CO2) + AHT21 (temperature/humidity) over I2C
// rendered on an ST7920 128x64 graphic LCD over hardware SPI.
//
// Board: Generic ESP8266 Module (ESP8266EX)
//
// Wiring (ESP8266):
//   I2C sensors: SDA -> GPIO4/D2, SCL -> GPIO5/D1, all sensors on the same bus.
//   ST7920 LCD (hardware SPI, write-only so no MISO):
//     - RS/CS      -> D8 (GPIO15)
//     - R/W (MOSI) -> D7 (GPIO13)
//     - E   (SCK)  -> D5 (GPIO14)
//     - PSB        -> GND (selects the LCD's serial mode)
//     - RST        -> Vin (5V), so U8g2 does not drive the reset line
//
// Required libraries:
//   - RTClib by Adafruit
//   - Adafruit BMP085 (also supports the BMP180)
//   - ENS160 - Adafruit Fork (ScioSense_ENS160)
//   - Adafruit AHTX0
//   - U8g2

#include <Wire.h>
#include <SPI.h>
#include <U8g2lib.h>
#include "RTClib.h"
#include <Adafruit_BMP085.h>
#include <Adafruit_AHTX0.h>
#include "ScioSense_ENS160.h"

// I2C pins for ESP8266 (Wire.begin(SDA, SCL) mapping)
#define I2C_SDA 4   // GPIO4 = D2
#define I2C_SCL 5   // GPIO5 = D1
// ST7920 chip-select; SCK/MOSI are fixed by the ESP8266 hardware SPI peripheral
#define LCD_CS 15   // GPIO15 = D8
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
#define SCREEN_INTERVAL_MS 4000      // How long each display page stays on screen
#define SCREEN_COUNT 3


RTC_DS3231 rtc;
Adafruit_BMP085 bmp;
Adafruit_AHTX0 aht;
ScioSense_ENS160 ens160(ENS160_I2C_ADDR);
// The LCD reset line is tied to Vin, so U8g2 does not control it.
U8G2_ST7920_128X64_F_HW_SPI u8g2(U8G2_R0, /* CS=*/ LCD_CS, /* reset=*/ U8X8_PIN_NONE);

// Translates DateTime::dayOfTheWeek() (0=Sunday..6=Saturday) to a name
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
// Three-letter form, for the width-constrained LCD
char daysShort[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// Last known-good readings, held between polls so the display keeps showing
// values while the sensors are between samples (or after a transient failure).
DateTime now;
float rtcTemp = 0;
float bmp180temp = 0, bmp180pressure = 0, bmp180altitude = 0;
float tempC = 0, rh = 0;
uint8_t aqi = 0;
uint16_t tvoc = 0, eco2 = 0;
bool haveAirQuality = false;

unsigned long lastSensorRead = 0;
unsigned long lastClockTick = 0;
unsigned long lastScreenSwitch = 0;
uint8_t currentScreen = 0;

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

  // Initialize the ST7920 LCD. ESP8266 hardware SPI: SCK=D5 (GPIO14), MOSI=D7 (GPIO13).
  // The display is write-only, so MISO is not connected.
  SPI.begin();
  u8g2.setBusClock(600000);  // 600 kHz stable SPI speed
  u8g2.begin();
  Serial.println("OK: ST7920 128x64 LCD initialized successfully.");

  // Show something immediately instead of an empty screen for the first interval
  readSensors();
  printSensors();
}

// Polls every sensor once and refreshes the cached values.
void readSensors() {
  // Fetch the current snapshot of date and time from the module
  now = rtc.now();
  rtcTemp = rtc.getTemperature();

  bmp180temp = bmp.readTemperature();
  bmp180pressure = bmp.readPressure() / 100.0F; // Convert Pa to hPa, 1hPa = 100Pa = 1mbar
  bmp180altitude = bmp.readAltitude(SEALEVEL_PRESSURE_PA);

  sensors_event_t humidityEvent;
  sensors_event_t temperatureEvent;
  if (aht.getEvent(&humidityEvent, &temperatureEvent)) {
    tempC = temperatureEvent.temperature;
    rh = humidityEvent.relative_humidity;
  } else {
    Serial.println("WARNING: AHT21 read failed, keeping last reading.");
  }

  // Feed environmental data to the ENS160 for internal compensation.
  // measure(false) polls the status register once instead of blocking until new
  // data is flagged, so a stalled sensor can't hang the loop.
  ens160.set_envdata(tempC, rh);
  haveAirQuality = ens160.measure(false);
  if (haveAirQuality) {
    aqi = ens160.getAQI();          // Air Quality Index (1-5)
    tvoc = ens160.getTVOC();        // TVOC in ppb (low=0ppb, high=1000ppb)
    eco2 = ens160.geteCO2();        // eCO2 in ppm (low=400ppm, high=6000ppm)
  }
}

// Dumps the cached readings to the serial monitor.
void printSensors() {
  Serial.printf("RTC    : %04d/%02d/%02d (%s) %02d:%02d:%02d\n",
                now.year(), now.month(), now.day(),
                daysOfTheWeek[now.dayOfTheWeek()],
                now.hour(), now.minute(), now.second());
  Serial.printf("RTC    : Temp: %.1f C\n", rtcTemp);
  Serial.printf("BMP180 : Temp: %.1f C  | Pressure: %.2f hPa | Approx Altitude: %.2f m\n", bmp180temp, bmp180pressure, bmp180altitude);
  Serial.printf("AHT21  : Temp: %.1f C  |  Humidity: %.1f %%\n", tempC, rh);
  if (haveAirQuality) {
    Serial.printf("ENS160 : AQI: %d (1-5)  |  TVOC: %d ppb  |  eCO2: %d ppm\n", aqi, tvoc, eco2);
  }
  Serial.println();
}

// Inverted title bar across the top 13 pixels.
void drawHeader(const char *title) {
  u8g2.drawBox(0, 0, 128, 13);
  u8g2.setDrawColor(0); // White text on black background
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(4, 10, title);
  u8g2.setDrawColor(1); // Reset back to normal black drawing
}

// Page 1: date and time from the DS3231, plus its die temperature.
void drawScreenClock() {
  drawHeader("DATE & TIME");
  char line[24];

  // 20px digit-only font: HH:MM:SS is 8 chars at ~10px each, centred by eye
  u8g2.setFont(u8g2_font_logisoso20_tn);
  snprintf(line, sizeof(line), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  u8g2.drawStr(8, 38, line);

  u8g2.setFont(u8g2_font_6x10_tf);
  snprintf(line, sizeof(line), "%s %04d/%02d/%02d", daysShort[now.dayOfTheWeek()],
           now.year(), now.month(), now.day());
  u8g2.drawStr(2, 51, line);

  snprintf(line, sizeof(line), "RTC temp: %.1f C", rtcTemp);
  u8g2.drawStr(2, 62, line);
}

// Page 2: AHT21 temperature/humidity and BMP180 pressure/altitude.
void drawScreenEnvironment() {
  drawHeader("ENVIRONMENT");
  u8g2.setFont(u8g2_font_6x10_tf);

  // Font is 6px/char; keep each line under ~20 chars so it fits the 128px width.
  char line[24];
  snprintf(line, sizeof(line), "Temp: %.1f C", tempC);
  u8g2.drawStr(2, 24, line);

  snprintf(line, sizeof(line), "Hum : %.1f %%", rh);
  u8g2.drawStr(2, 36, line);

  snprintf(line, sizeof(line), "Pres: %.1f hPa", bmp180pressure);
  u8g2.drawStr(2, 48, line);

  snprintf(line, sizeof(line), "Alt : %.0f m", bmp180altitude);
  u8g2.drawStr(2, 60, line);
}

// Page 3: ENS160 air quality, with a 1-5 AQI bar graph.
void drawScreenAirQuality() {
  drawHeader("AIR QUALITY");
  u8g2.setFont(u8g2_font_6x10_tf);

  if (!haveAirQuality) {
    u8g2.drawStr(2, 30, "ENS160 warming up");
    u8g2.drawStr(2, 42, "(no data yet)");
    return;
  }

  char line[24];
  snprintf(line, sizeof(line), "AQI : %d of 5", aqi);
  u8g2.drawStr(2, 24, line);

  // Five slots, filled up to the current AQI level
  for (uint8_t i = 0; i < 5; i++) {
    if (i < aqi) {
      u8g2.drawBox(78 + i * 10, 16, 8, 8);
    } else {
      u8g2.drawFrame(78 + i * 10, 16, 8, 8);
    }
  }

  snprintf(line, sizeof(line), "TVOC: %d ppb", tvoc);
  u8g2.drawStr(2, 38, line);

  snprintf(line, sizeof(line), "eCO2: %d ppm", eco2);
  u8g2.drawStr(2, 50, line);

  // eCO2 bar scaled over the sensor's 400..6000 ppm reporting range
  uint16_t clamped = eco2 < 400 ? 400 : (eco2 > 6000 ? 6000 : eco2);
  u8g2.drawFrame(2, 54, 124, 8);
  u8g2.drawBox(4, 56, ((clamped - 400) * 120) / 5600, 4);
}

void loop() {
  unsigned long nowMs = millis();

  // The clock page shows seconds, so re-read the RTC more often than the full
  // sensor sweep - otherwise the displayed time would step in 5 second jumps.
  if (nowMs - lastClockTick >= 250) {
    lastClockTick = nowMs;
    now = rtc.now();
  }

  if (nowMs - lastSensorRead >= READ_INTERVAL_MS) {
    lastSensorRead = nowMs;
    readSensors();
    printSensors();
  }

  // Rotate through the pages
  if (nowMs - lastScreenSwitch >= SCREEN_INTERVAL_MS) {
    lastScreenSwitch = nowMs;
    currentScreen = (currentScreen + 1) % SCREEN_COUNT;
  }

  u8g2.clearBuffer();
  switch (currentScreen) {
    case 0: drawScreenClock(); break;
    case 1: drawScreenEnvironment(); break;
    case 2: drawScreenAirQuality(); break;
  }
  u8g2.sendBuffer();

  delay(50); // ~20 FPS frame pacing; keeps the SPI bus from being hammered
}
