// demo of deepsleep mode
// Board: Generic ESP8266 Module (ESP8266EX)

// Wiring (ESP8266): D0(GPIO16) -> RST

const uint32_t RTC_MEMORY_OFFSET = 0;
const uint32_t RTC_MEMORY_MAGIC = 0xDEEF51EE;

uint32_t wakeupcount = 0;

void setup() {
  Serial.begin(115200);

  uint32_t rtcData[2] = {0, 0};
  if (ESP.rtcUserMemoryRead(RTC_MEMORY_OFFSET, rtcData, sizeof(rtcData)) &&
      rtcData[0] == RTC_MEMORY_MAGIC) {
    wakeupcount = rtcData[1];
  }

  Serial.println("Woke up");
  Serial.print("Completed deep sleep cycles: ");
  Serial.println(wakeupcount);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
}

void loop() {
  // Blink the LED for 5 seconds, twice per second, then sleep for 1 minute.
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(500);
  }

  // Count this cycle before sleeping so the updated value is available after
  // the ESP8266 wakes up and starts setup() again.
  wakeupcount++;
  uint32_t rtcData[2] = {RTC_MEMORY_MAGIC, wakeupcount};
  if (!ESP.rtcUserMemoryWrite(RTC_MEMORY_OFFSET, rtcData, sizeof(rtcData))) {
    Serial.println("Could not save deep sleep cycle count");
  }

  ESP.deepSleep(60e6);
}
