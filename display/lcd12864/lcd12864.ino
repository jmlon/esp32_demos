// ST7920 128x64 graphic LCD demo: three rotating screens (dashboard mockup,
// shapes/fonts, animated waveform) over hardware SPI.
//
// Board: ESP32C3 Dev Module
// USB CDC On Boot: Enabled
// Upload Mode: UART0/Hardware CDC
// USB Mode: Hardware CDC and JTAG
//
// Wiring (ESP32-C3, hardware SPI): SCK -> GPIO6, MOSI -> GPIO7, CS -> GPIO5,
// RESET -> GPIO4. PSB must be tied to GND to select the LCD's serial mode
// (parallel mode is not supported by this constructor). No MISO connection
// is needed; the display is write-only.
//
// Required libraries:
//   - U8g2

#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>

// U8g2 Hardware SPI Constructor for ST7920 128x64
U8G2_ST7920_128X64_F_HW_SPI u8g2(U8G2_R0, /* CS=*/ 5, /* reset=*/ 4);

uint8_t progress = 0;
uint8_t currentScreen = 0;
unsigned long lastScreenSwitch = 0;

void drawHeader(const char* title) {
  // Inverted Header Box: Black background with white text
  u8g2.drawBox(0, 0, 128, 13);
  u8g2.setDrawColor(0); // White text on black background
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(4, 10, title);
  u8g2.setDrawColor(1); // Reset back to normal black drawing
}

void drawScreenDashboard() {
  drawHeader("SYSTEM STATUS");

  // Draw Open-Iconic Glyphs
  u8g2.setFont(u8g2_font_open_iconic_embedded_1x_t);
  u8g2.drawGlyph(4, 28, 74);  // Microchip / CPU Icon
  u8g2.drawGlyph(4, 43, 80);  // WiFi Icon

  // Info Text
  u8g2.setFont(u8g2_font_profont11_tr);
  u8g2.drawStr(18, 28, "ESP32-C3: 160MHz");
  u8g2.drawStr(18, 43, "WiFi: Connected");

  // Animated Progress Bar Frame
  u8g2.drawRFrame(4, 49, 120, 12, 2);
  u8g2.drawBox(6, 51, (progress * 116) / 100, 8); // Scaled inner fill
}

void drawScreenShapes() {
  drawHeader("GEOMETRY & FONTS");

  // Concentric Circles
  u8g2.drawCircle(20, 38, 15, U8G2_DRAW_ALL);
  u8g2.drawDisc(20, 38, 7, U8G2_DRAW_ALL);

  // Rounded Box with Inverted Text Highlight
  u8g2.drawRFrame(45, 18, 78, 20, 3);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(50, 30, "ST7920 128x64");

  // Filled Box with Inverted White Text
  u8g2.drawBox(45, 42, 78, 18);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_helvB08_tr);
  u8g2.drawStr(52, 55, "INVERTED");
  u8g2.setDrawColor(1);
}

void drawScreenWaveform() {
  drawHeader("REALTIME GRAPH");

  // Grid Lines
  for (int x = 0; x < 128; x += 16) {
    u8g2.drawVLine(x, 14, 50);
  }

  // Draw Simulated Sine Wave
  for (int x = 0; x < 127; x++) {
    int y1 = 38 + (int)(18.0 * sin((x + progress * 4) * 0.1));
    int y2 = 38 + (int)(18.0 * sin((x + 1 + progress * 4) * 0.1));
    u8g2.drawLine(x, y1, x + 1, y2);
  }
}

void setup() {
  SPI.begin(6, -1, 7, 5);    // SCK=6, MISO=-1, MOSI=7, CS=5
  u8g2.setBusClock(600000);  // 600 kHz stable SPI speed
  u8g2.begin();
}

void loop() {
  // Update animation counters
  progress = (progress + 2) % 101;

  // Switch screen every 3.5 seconds
  if (millis() - lastScreenSwitch > 3500) {
    lastScreenSwitch = millis();
    currentScreen = (currentScreen + 1) % 3;
  }

  u8g2.clearBuffer();

  switch (currentScreen) {
    case 0: drawScreenDashboard(); break;
    case 1: drawScreenShapes(); break;
    case 2: drawScreenWaveform(); break;
  }

  u8g2.sendBuffer();
  delay(30); // ~30 FPS frame pacing
}