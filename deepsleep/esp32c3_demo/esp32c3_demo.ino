// Demo of deep sleep mode on an ESP32-C3.
// Not yet flashed/verified on hardware -- see caveat below before relying on it.
//
// Board: ESP32C3 Dev Module
// USB CDC On Boot: Enabled
// Upload Mode: UART0/Hardware CDC
// USB Mode: Hardware CDC and JTAG
//
// The ESP32-C3 wakes from deep sleep using its timer. The wakeup count is
// retained in RTC memory (RTC_DATA_ATTR), which is this chip's equivalent of
// the ESP8266 demo's rtcUserMemoryRead/Write -- both survive deep-sleep
// resets but not a power cycle.
//
// Caveat: the DevKitM-1 exposes only native USB-CDC serial (no UART bridge
// chip like the ESP8266 board), so after each deep-sleep wake the port has
// to re-enumerate with the host before the serial monitor reconnects. A
// short fixed delay after Serial.begin() can be too brief and drop the
// first print lines, so we wait (bounded) for the host instead.

#include <Arduino.h>
#include "esp_sleep.h"

const uint8_t LED_PIN = 8;                 // Built-in LED on ESP32-C3 DevKitM-1
const uint64_t SLEEP_TIME_US = 60ULL * 1000000ULL;

RTC_DATA_ATTR uint32_t wakeupCount = 0;

void setup() {
  Serial.begin(115200);
  unsigned long serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 3000) {
    delay(10);
  }
  delay(100);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println();
  Serial.println("Woke up from deep sleep");
  Serial.print("Wakeup cause: ");
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("timer");
      break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      Serial.println("power-on or reset");
      break;
    default:
      Serial.println("other");
      break;
  }
  Serial.print("Completed deep sleep cycles: ");
  Serial.println(wakeupCount);
}

void loop() {
  // Blink the LED for 5 seconds, twice per second, then sleep for 1 minute.
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(500);
  }

  // Increment before sleeping so the updated value is available after the
  // ESP32-C3 wakes and starts setup() again.
  wakeupCount++;
  Serial.print("Going to sleep after cycle ");
  Serial.println(wakeupCount);
  Serial.flush();

  esp_sleep_enable_timer_wakeup(SLEEP_TIME_US);
  esp_deep_sleep_start();
}
