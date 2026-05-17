// Rotary encoder with ST7789 display
// Video explanation: https://www.youtube.com/watch?v=v4BbSzJ-hz4
// https://www.bitsanddroids.com/post/back-to-basics-dual-concentric-rotary-encoders
// https://github.com/mathertel/RotaryEncoder
//
// Board: ESP32 Dev Module, Flash=4MB
// Required libraries:
//   - RotaryEncoder by Matthias Hertel
//   - Adafruit ST7735 & ST7789
//   - Adafruit GFX

#include <Arduino.h>
#include <RotaryEncoder.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// Rotary encoder pins (no need for pullup resistors)
#define PIN_OUTER_A 14   // GPIO14
#define PIN_OUTER_B 27   // GPIO27
#define PIN_INNER_A 26   // GPIO26
#define PIN_INNER_B 25   // GPIO25

// ST7789 display pins
#define LCD_MOSI 23 // D23
#define LCD_SCLK 18 // D18
#define LCD_CS   15 // D15
#define LCD_DC    2 // D2
#define LCD_RST   4 // D4
#define LCD_BLK  32 // D32

RotaryEncoder encoder_out(PIN_OUTER_A, PIN_OUTER_B, RotaryEncoder::LatchMode::TWO03);
RotaryEncoder encoder_in(PIN_INNER_A, PIN_INNER_B, RotaryEncoder::LatchMode::TWO03);

Adafruit_ST7789 tft = Adafruit_ST7789(LCD_CS, LCD_DC, LCD_RST);

static int pos_out = 0;
static int pos_in = 0;

void drawValues() {
  tft.fillScreen(ST77XX_BLACK);

  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.println("Rotary Encoder");

  tft.setCursor(0, 40);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextSize(2);
  tft.print("Outer: ");
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_WHITE);
  tft.println(pos_out);

  tft.setCursor(0, 90);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.print("Inner: ");
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_WHITE);
  tft.println(pos_in);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Rotary encoder with ST7789 display");

  pinMode(LCD_BLK, OUTPUT);
  digitalWrite(LCD_BLK, HIGH);

  tft.init(135, 240);
  tft.setRotation(3);
  drawValues();
}

void loop() {
  bool changed = false;

  encoder_out.tick();
  int newPos_out = encoder_out.getPosition();
  if (pos_out != newPos_out) {
    Serial.print("outer pos:");
    Serial.print(newPos_out);
    Serial.print(" dir:");
    Serial.println((int)(encoder_out.getDirection()));
    pos_out = newPos_out;
    changed = true;
  }

  encoder_in.tick();
  int newPos_in = encoder_in.getPosition();
  if (pos_in != newPos_in) {
    Serial.print("inner pos:");
    Serial.print(newPos_in);
    Serial.print(" dir:");
    Serial.println((int)(encoder_in.getDirection()));
    pos_in = newPos_in;
    changed = true;
  }

  if (changed) {
    drawValues();
  }
}
