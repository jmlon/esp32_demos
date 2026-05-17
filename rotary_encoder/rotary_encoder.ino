// Rotary encoder
// Video explanation: https://www.youtube.com/watch?v=v4BbSzJ-hz4
// https://www.bitsanddroids.com/post/back-to-basics-dual-concentric-rotary-encoders
// https://github.com/mathertel/RotaryEncoder

#include <Arduino.h>

// Library: RotaryEncoder Matthias Hertel
#include <RotaryEncoder.h>

// No need for pullup resistors
#define PIN_OUTER_A A16   // GPIO14
#define PIN_OUTER_B A17   // GPIO27

#define PIN_INNER_A A19   // GPIO26
#define PIN_INNER_B A18   // GPIO25


// Setup a RotaryEncoder with 2 steps per latch for the 2 signal input pins:
RotaryEncoder encoder_out(PIN_OUTER_A, PIN_OUTER_B, RotaryEncoder::LatchMode::TWO03);
RotaryEncoder encoder_in(PIN_INNER_A, PIN_INNER_B, RotaryEncoder::LatchMode::TWO03);


void setup() {
  Serial.begin(115200);
  while (! Serial);
  Serial.println("SimplePollRotator example for the RotaryEncoder library.");

}

void loop() {

  static int pos_out = 0;
  static int pos_in = 0;

  encoder_out.tick();
  int newPos_out = encoder_out.getPosition();
  if (pos_out != newPos_out) {
    Serial.print("pos:");
    Serial.print(newPos_out);
    Serial.print(" dir:");
    Serial.println((int)(encoder_out.getDirection()));
    pos_out = newPos_out;
  } // if

  encoder_in.tick();
  int newPos_in = encoder_in.getPosition();
  if (pos_in != newPos_in) {
    Serial.print("pos:");
    Serial.print(newPos_in);
    Serial.print(" dir:");
    Serial.println((int)(encoder_in.getDirection()));
    pos_in = newPos_in;
  } // if


}
