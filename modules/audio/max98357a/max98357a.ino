// MAX98357A I2S amplifier demo: alternates a 330 Hz tone on the left channel
// and a 440 Hz tone on the right channel, using two amplifier boards (one per
// channel) driven from the same I2S bus.
//
// The target is picked at compile time from the board selected in the IDE
// (CONFIG_IDF_TARGET_*); only the I2S pins differ.
//
// ESP32-C3
//   Board: ESP32C3 Dev Module
//   USB CDC On Boot: Enabled
//   Upload Mode: UART0/Hardware CDC
//   USB Mode: Hardware CDC and JTAG
//   With USB CDC On Boot disabled, Serial goes to UART0 and nothing shows on
//   /dev/ttyACM0. With arduino-cli use: -b esp32:esp32:esp32c3:CDCOnBoot=cdc
//   Wiring: BCLK -> GPIO3, LRC -> GPIO1, DIN -> GPIO10
//
// ESP32 (ESP32-D0WD-V3, e.g. an ESP-WROOM-32 DevKit)
//   Board: ESP32 Dev Module
//   Serial on /dev/ttyUSB0. With arduino-cli use: -b esp32:esp32:esp32
//   Wiring: BCLK -> GPIO26, LRC -> GPIO25, DIN -> GPIO22
//
// Wiring common to both (-> both MAX98357A boards):
//   VIN -> 5V, GND -> GND
//   SD (left board)  -> 100k to 5V  (SD > 1.4 V selects left only)
//   SD (right board) -> 680k to 5V  (SD ~1.0 V selects right only)
// The SD voltage divides against the chip's internal ~100k pull-down and the
// board's 1M pull-up to VIN. Right-only needs 0.77-1.4 V; aim for the middle,
// since a value near 1.4 V lets the right board pick up the left channel.
//
// Required libraries:
//   - ESP_I2S (bundled with the ESP32 Arduino core 3.x)

#include "ESP_I2S.h"
#include <math.h>

#if CONFIG_IDF_TARGET_ESP32C3
const int I2S_BCLK = 3;
const int I2S_LRC = 1;
const int I2S_DIN = 10;
#elif CONFIG_IDF_TARGET_ESP32
const int I2S_BCLK = 26;
const int I2S_LRC = 25;
const int I2S_DIN = 22;
#else
#error "This sketch supports the ESP32-C3 and the ESP32; select one of them as the board."
#endif

I2SClass i2s;

// Audio constants
const int SAMPLE_RATE = 44100;
const int TONE_DURATION_MS = 1000; // 1 second per tone
const int TOTAL_SAMPLES = (SAMPLE_RATE * TONE_DURATION_MS) / 1000;

const int AMPLITUDE = 16384;                  // 50% full scale; higher may clip small speakers on the MAX98357A
const int FADE_SAMPLES = SAMPLE_RATE / 200;   // 5 ms fade in/out

const int SINE_TABLE_BITS = 10;
const int SINE_TABLE_SIZE = 1 << SINE_TABLE_BITS;
int16_t sineTable[SINE_TABLE_SIZE];

struct StereoFrame {
  int16_t left;
  int16_t right;
};

const int FRAME_BUFFER_LEN = 256;
StereoFrame frameBuffer[FRAME_BUFFER_LEN];

void setup() {
  // Start the physical USB/Serial monitor at normal speed
  Serial.begin(115200);
  delay(1000);
  Serial.println("Initializing clean I2S Stereo Setup...");

  // Precompute one sine period once; sin() is software-emulated on the C3
  for (int i = 0; i < SINE_TABLE_SIZE; i++) {
    sineTable[i] = (int16_t)(AMPLITUDE * sinf(2.0f * M_PI * i / SINE_TABLE_SIZE));
  }

  // setPins(bclk, ws, dout, din, mclk): the amplifier's DIN is the ESP32's
  // data out; no data in (-1) and no MCLK (-1)
  i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN, -1, -1);

  // Launch standard 44.1kHz stereo audio playback structure
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("System Error: Failed to configure clean stereo I2S!");
    while (1);
  }

  Serial.println("I2S Stereo Ready! Audio stream is starting...");
}


void loop() {
  // --- PART 1: 330 Hz on Left Channel Only ---
  Serial.println("Playing 330Hz tone on LEFT channel...");
  playChannelTone(330, true);
  delay(200); // Short silence between tones

  // --- PART 2: 440 Hz on Right Channel Only ---
  Serial.println("Playing 440Hz tone on RIGHT channel...");
  playChannelTone(440, false);
  delay(200); // Short silence between tones
}

// Stream a sine tone using the lookup table and a 32-bit phase accumulator.
// The ESP32-C3 has no FPU, so calling sin() per sample is too slow to keep the
// I2S DMA fed (underruns sound like buzzing/crackling). Integer math only here.
void playChannelTone(float frequency, bool playOnLeft) {
  uint32_t phase = 0;
  uint32_t phaseStep = (uint32_t)(frequency * 4294967296.0 / SAMPLE_RATE);

  int n = 0;
  for (int i = 0; i < TOTAL_SAMPLES; i++) {
    int32_t sample = sineTable[phase >> (32 - SINE_TABLE_BITS)];
    phase += phaseStep;

    // Short linear fade in/out to avoid clicks at tone start/end
    if (i < FADE_SAMPLES) {
      sample = sample * i / FADE_SAMPLES;
    } else if (i >= TOTAL_SAMPLES - FADE_SAMPLES) {
      sample = sample * (TOTAL_SAMPLES - 1 - i) / FADE_SAMPLES;
    }

    frameBuffer[n].left = playOnLeft ? sample : 0;
    frameBuffer[n].right = playOnLeft ? 0 : sample;

    // Push a whole block to the I2S DMA instead of one 4-byte frame at a time
    if (++n == FRAME_BUFFER_LEN) {
      i2s.write((uint8_t*)frameBuffer, sizeof(frameBuffer));
      n = 0;
    }
  }
  if (n > 0) {
    i2s.write((uint8_t*)frameBuffer, n * sizeof(StereoFrame));
  }
}
