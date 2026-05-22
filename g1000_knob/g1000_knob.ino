// ESP32-S3 Settings:
// Device: ESP32S3 Dev Module
// port: /dev/ttyACM0 (built in USB controller)
// USB CDC Onboot: Enable (Bind Serial.println to onboard USB controller)
// Upload Mode: UART0/Hardware CDC
// USB Mode: USB-OTG (TinyUSB)     <-- required for HID joystick

// Linux test:
// sudo apt install joystick
// jstest /dev/input/js0


#include <Arduino.h>

// Library: RotaryEncoder Matthias Hertel
#include <RotaryEncoder.h>

// Library manager: Joystick_ESP32S2
#include <Joystick_ESP32S2.h>

// No need for pullup resistors
#define PIN_OUTER_A 14
#define PIN_OUTER_B 13
#define PIN_INNER_A 12
#define PIN_INNER_B 11

// Joystick: 2 axes (X, Y), 4 buttons (outer CW/CCW, inner CW/CCW), no hat.
// Args: reportId, type, buttonCount, hatCount,
//       includeX, includeY, includeZ, includeRx, includeRy, includeRz,
//       includeRudder, includeThrottle, includeAccelerator, includeBrake, includeSteering
Joystick_ Joystick(
  0x03, JOYSTICK_TYPE_JOYSTICK,
  4, 0,
  true,  true,  false,
  false, false, false,
  false, false, false, false, false
);

// Axis range. Use unsigned 0..1023 (center=512) — the HID descriptor
// treats axis fields as unsigned, so negative values would wrap.
const int AXIS_MIN    = 0;
const int AXIS_MAX    = 1023;
const int AXIS_CENTER = 512;

// How many encoder detents map to full-scale axis travel.
const int FULL_SCALE_DETENTS = 50;

// Pulse width (ms) for the CW/CCW "button" presses, if you use that mode.
const unsigned long BUTTON_PULSE_MS = 30;

RotaryEncoder encoder_out(PIN_OUTER_A, PIN_OUTER_B, RotaryEncoder::LatchMode::TWO03);
RotaryEncoder encoder_in (PIN_INNER_A, PIN_INNER_B, RotaryEncoder::LatchMode::TWO03);

int pos_out = 0;
int pos_in  = 0;

// For pulsed-button mode:
unsigned long btn_release_at[4] = {0, 0, 0, 0};

static int clamp_axis(int v) {
  if (v < AXIS_MIN) return AXIS_MIN;
  if (v > AXIS_MAX) return AXIS_MAX;
  return v;
}

static int detents_to_axis(int detents) {
  long half_span = (AXIS_MAX - AXIS_MIN) / 2;
  long scaled = AXIS_CENTER + (long)detents * half_span / FULL_SCALE_DETENTS;
  return clamp_axis((int)scaled);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("FlightSim G1000 Knob");

  Joystick.setXAxisRange(AXIS_MIN, AXIS_MAX);
  Joystick.setYAxisRange(AXIS_MIN, AXIS_MAX);
  Joystick.begin(false);   // false = manual sendState() — fewer USB reports

  Joystick.setXAxis(AXIS_CENTER);
  Joystick.setYAxis(AXIS_CENTER);

  // HID endpoint isn't ready immediately after begin(); resend
  // the centered state until the host acknowledges.
  for (int i = 0; i < 100; i++) {
    Joystick.sendState();
    delay(20);
  }

  // Seed encoder positions to 0 based on the actual resting pin state,
  // so we don't register a phantom transition on the first tick().
  encoder_out.tick(); encoder_out.setPosition(0);
  encoder_in.tick();  encoder_in.setPosition(0);
}

// ---- DIAGNOSTIC: comment out once axis sign is verified ----
void loop_diag() {
  static const int samples[] = {0, 256, 512, 768, 1023};
  static int idx = 0;
  static unsigned long t = 0;
  if (millis() - t >= 2000) {
    t = millis();
    int v = samples[idx];
    Joystick.setXAxis(v);
    Joystick.setYAxis(v);
    Joystick.sendState();
    Serial.printf("DIAG sending axis = %d  (expect jstest ~ %ld)\n",
                  v, ((long)v * 65535 / 1023) - 32768);
    idx = (idx + 1) % 5;
  }
}

void loop() {
  // Uncomment the next line to run the diagnostic instead of the encoders:
  // loop_diag(); return;

  encoder_out.tick();
  encoder_in.tick();

  int new_out = encoder_out.getPosition();
  int new_in  = encoder_in.getPosition();

  bool changed = false;

  // Buttons: 0=outer CW, 1=outer CCW, 2=inner CW, 3=inner CCW
  if (new_out != pos_out) {
    int delta = new_out - pos_out;
    pos_out = new_out;
    int btn = (delta > 0) ? 0 : 1;
    Joystick.setButton(btn, 1);
    btn_release_at[btn] = millis() + BUTTON_PULSE_MS;
    Serial.printf("outer pos=%d  delta=%+d  btn=%d\n", pos_out, delta, btn);
    changed = true;
  }

  if (new_in != pos_in) {
    int delta = new_in - pos_in;
    pos_in = new_in;
    int btn = (delta > 0) ? 2 : 3;
    Joystick.setButton(btn, 1);
    btn_release_at[btn] = millis() + BUTTON_PULSE_MS;
    Serial.printf("inner pos=%d  delta=%+d  btn=%d\n", pos_in, delta, btn);
    changed = true;
  }

  unsigned long now = millis();
  for (int i = 0; i < 4; i++) {
    if (btn_release_at[i] && (long)(now - btn_release_at[i]) >= 0) {
      Joystick.setButton(i, 0);
      btn_release_at[i] = 0;
      changed = true;
    }
  }

  // Heartbeat: re-send current state every 250 ms so the host always
  // has a fresh, correct snapshot even if it missed an earlier report.
  static unsigned long last_send = 0;
  if (changed || (now - last_send) >= 250) {
    Joystick.sendState();
    last_send = now;
  }
}
