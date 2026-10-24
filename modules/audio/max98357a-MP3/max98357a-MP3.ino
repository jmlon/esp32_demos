// MP3 player for the ESP32-C3 or the ESP32 and the MAX98357A.  It plays every
// .mp3 file in the board's flash filesystem (LittleFS), in name order, then
// starts over; press BOOT to skip to the next track.  The track names, their
// format and the decoder's CPU load are printed on the serial port.
//
// Decoding uses the Helix MP3 decoder that ships with the ESP32 Arduino core
// (the one behind ESP_I2S's playMP3()).  Helix works in fixed point, so it
// needs no FPU and runs on the C3 as well.  playMP3() only plays a buffer
// already in RAM; this sketch streams from a file instead, a few KB at a
// time.  Any sample rate MPEG allows (8 to 48 kHz), mono or stereo, and
// constant or variable bit rate files play; mono is sent to both slots.
//
// The target is picked at compile time from the board selected in the IDE
// (CONFIG_IDF_TARGET_*); only the pins differ.
//
// Both boards need the partition scheme "No OTA (1MB APP/3MB SPIFFS)", which
// leaves 2.9 MB for MP3 files: about 3 minutes at 128 kbps, 6 at 64 kbps.
//
// ESP32-C3
//   Board: ESP32C3 Dev Module
//   USB CDC On Boot: Enabled
//   Upload Mode: UART0/Hardware CDC
//   USB Mode: Hardware CDC and JTAG
//   Partition Scheme: No OTA (1MB APP/3MB SPIFFS)
//   With arduino-cli use:
//     -b esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=noota_3g
//   Wiring: BCLK -> GPIO3, LRC -> GPIO1, DIN -> GPIO10
//   BOOT button: GPIO9
//
// ESP32 (ESP32-D0WD-V3, e.g. an ESP-WROOM-32 DevKit)
//   Board: ESP32 Dev Module
//   Partition Scheme: No OTA (1MB APP/3MB SPIFFS)
//   With arduino-cli use: -b esp32:esp32:esp32:PartitionScheme=noota_3g
//   Wiring: BCLK -> GPIO26, LRC -> GPIO25, DIN -> GPIO22
//   BOOT button: GPIO0
//
// Wiring common to both: MAX98357A VIN -> 5V, GND -> GND.
//
// The MP3 files go in the data/ folder next to this sketch.  Write them to
// the board, after flashing the sketch with the partition scheme above, with
//   ./upload-data.sh [port]
// which builds a LittleFS image of data/ and writes it to the filesystem
// partition (the Arduino IDE 2 has no built-in uploader for it).
//
// Required libraries:
//   - ESP_I2S, LittleFS (bundled with the ESP32 Arduino core 3.x)

#include <Arduino.h>
#include <LittleFS.h>
#include "ESP_I2S.h"
#include "esp_cpu.h"
#include "mp3dec.h"

namespace {

// ---------------------------------------------------------------------------
// Target

#if CONFIG_IDF_TARGET_ESP32C3
constexpr int8_t I2S_BCLK = 3;
constexpr int8_t I2S_LRC = 1;
constexpr int8_t I2S_DIN = 10;
constexpr uint8_t KEY_PIN = 9;  // BOOT button, LOW while pressed
#elif CONFIG_IDF_TARGET_ESP32
constexpr int8_t I2S_BCLK = 26;
constexpr int8_t I2S_LRC = 25;
constexpr int8_t I2S_DIN = 22;
constexpr uint8_t KEY_PIN = 0;  // BOOT button, LOW while pressed
#else
#error "This sketch supports the ESP32-C3 and the ESP32; select one of them as the board."
#endif

// ---------------------------------------------------------------------------
// Output

// MP3s are usually mastered close to full scale, which with the MAX98357A at
// 9 dB gain and 5 V overdrives small speakers (see max98357a-MIDI).  VOLUME
// scales every sample, in Q15 so it costs no floating point on the C3.
constexpr float VOLUME = 0.4f;
constexpr int32_t VOLUME_Q15 = static_cast<int32_t>(VOLUME * 32768);

I2SClass i2s;
uint32_t i2sRate = 0;  // the sample rate I2S runs at

// One MP3 frame decodes to at most 2 granules x 576 samples per channel; the
// output is always stereo.
int16_t pcm[MAX_NCHAN * MAX_NGRAN * MAX_NSAMP];

void setRate(uint32_t rate) {
  if (rate != i2sRate) {
    i2s.configureTX(rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    i2sRate = rate;
  }
}

// Scales the decoded samples in place, and spreads a mono frame (samples
// 0..n-1) over both slots, working backwards so nothing is overwritten
// before it is read.  Returns the number of int16_t samples to write.
size_t toStereo(int16_t *buf, int samples, int channels) {
  if (channels == 2) {
    for (int i = 0; i < samples; ++i) {
      buf[i] = static_cast<int16_t>((buf[i] * VOLUME_Q15) >> 15);
    }
    return samples;
  }
  for (int i = samples - 1; i >= 0; --i) {
    const int16_t s = static_cast<int16_t>((buf[i] * VOLUME_Q15) >> 15);
    buf[2 * i] = s;
    buf[2 * i + 1] = s;
  }
  return 2 * samples;
}

// ---------------------------------------------------------------------------
// Playlist: the .mp3 files at the root of the filesystem, in name order

constexpr size_t MAX_TRACKS = 32;
String tracks[MAX_TRACKS];
size_t trackCount = 0;

void loadPlaylist() {
  trackCount = 0;
  File root = LittleFS.open("/");
  for (File f = root.openNextFile(); f && trackCount < MAX_TRACKS;
       f = root.openNextFile()) {
    String name = f.name();
    String lower = name;
    lower.toLowerCase();
    if (!f.isDirectory() && lower.endsWith(".mp3")) {
      tracks[trackCount++] = "/" + name;
    }
  }
  // Insertion sort: openNextFile() returns the files in no particular order.
  for (size_t i = 1; i < trackCount; ++i) {
    for (size_t j = i; j > 0 && tracks[j] < tracks[j - 1]; --j) {
      String t = tracks[j];
      tracks[j] = tracks[j - 1];
      tracks[j - 1] = t;
    }
  }
}

// ---------------------------------------------------------------------------
// BOOT button: a press only counts after the button has been seen released,
// so holding it does not skip track after track.  Polled once per frame
// (every 26 ms or less).

bool keyWasUp = false;

bool keyPressed() {
  const bool down = digitalRead(KEY_PIN) == LOW;
  const bool pressed = down && keyWasUp;
  keyWasUp = !down;
  return pressed;
}

// ---------------------------------------------------------------------------
// Load report: the decoder's share of the CPU.  The elapsed time comes from
// micros(), since the cycle counter stops while the idle task waits for an
// interrupt (as it does whenever i2s.write() blocks).

uint32_t decodeCycles = 0;
uint32_t lastReportUs = 0;
uint32_t decodeErrors = 0;

void report(bool force = false) {
  const uint32_t nowUs = micros();
  if (!force && nowUs - lastReportUs < 2000000) {
    return;
  }
  const float load =
      100.0f * decodeCycles / ((nowUs - lastReportUs) * static_cast<float>(getCpuFrequencyMhz()));
  lastReportUs = nowUs;
  decodeCycles = 0;
  Serial.printf("  decoder CPU load: %.1f %%; corrupt frames skipped: %u\n",
                load, static_cast<unsigned>(decodeErrors));
}

// ---------------------------------------------------------------------------
// Streaming decoder

// The decoder wants at least one whole frame in front of it (MAINBUF_SIZE,
// 1940 bytes, covers the largest one); the buffer holds two, refilled from
// the file whenever less than one is left.
constexpr int IN_BUF_SIZE = 2 * MAINBUF_SIZE;
uint8_t inBuf[IN_BUF_SIZE];
HMP3Decoder decoder = nullptr;

// Files often start with an ID3v2 tag (title, artist, cover art).  Its
// length is in its header, so it can be skipped instead of searched through:
// cover art may contain bytes that look like an MP3 frame header.
void skipId3(File &f) {
  uint8_t h[10];
  if (f.read(h, sizeof(h)) == sizeof(h) && h[0] == 'I' && h[1] == 'D' &&
      h[2] == '3') {
    // A 28-bit "syncsafe" size (7 bits per byte), excluding the header and
    // the optional 10-byte footer (flag 0x10).
    const uint32_t size = (h[6] & 0x7F) << 21 | (h[7] & 0x7F) << 14 |
                          (h[8] & 0x7F) << 7 | (h[9] & 0x7F);
    f.seek(10 + size + ((h[5] & 0x10) ? 10 : 0));
  } else {
    f.seek(0);
  }
}

// Plays one file.  Returns false if BOOT was pressed to skip it.
bool playFile(const String &path) {
  File f = LittleFS.open(path, "r");
  if (!f) {
    Serial.printf("Cannot open %s\n", path.c_str());
    return true;
  }
  Serial.printf("Playing %s (%u KB)\n", path.c_str(),
                static_cast<unsigned>(f.size() / 1024));
  skipId3(f);

  uint8_t *readPtr = inBuf;
  int bytesLeft = 0;
  bool eof = false;
  bool formatShown = false;
  decodeErrors = 0;

  while (true) {
    if (keyPressed()) {
      f.close();
      return false;
    }

    // Top the buffer up: move what is left to the front, then read.
    if (bytesLeft < MAINBUF_SIZE && !eof) {
      memmove(inBuf, readPtr, bytesLeft);
      readPtr = inBuf;
      const int n = f.read(inBuf + bytesLeft, IN_BUF_SIZE - bytesLeft);
      if (n <= 0) {
        eof = true;
      } else {
        bytesLeft += n;
      }
    }

    const int offset = MP3FindSyncWord(readPtr, bytesLeft);
    if (offset < 0) {
      if (eof) {
        break;  // nothing but trailing tags or padding left
      }
      // No frame header in the buffer: drop it, but keep the last byte,
      // which may be the first half of a header.
      readPtr += bytesLeft - 1;
      bytesLeft = 1;
      continue;
    }
    readPtr += offset;
    bytesLeft -= offset;

    const uint32_t t0 = esp_cpu_get_cycle_count();
    const int err = MP3Decode(decoder, &readPtr, &bytesLeft, pcm, 0);
    decodeCycles += esp_cpu_get_cycle_count() - t0;

    if (err == ERR_MP3_INDATA_UNDERFLOW) {
      if (eof) {
        break;  // a truncated last frame
      }
      continue;  // read more
    }
    if (err == ERR_MP3_MAINDATA_UNDERFLOW) {
      // Normal right after a sync or skip: the frame refers to data from
      // frames before it (the bit reservoir), which were not decoded.
      continue;
    }
    if (err) {
      // A corrupt frame: step past this sync word and look for the next.
      ++decodeErrors;
      ++readPtr;
      --bytesLeft;
      continue;
    }

    MP3FrameInfo info;
    MP3GetLastFrameInfo(decoder, &info);
    if (!formatShown) {
      formatShown = true;
      Serial.printf("  MPEG-%s layer %d, %d Hz, %s, %d kbps\n",
                    info.version == 0 ? "1" : info.version == 1 ? "2" : "2.5",
                    info.layer, info.samprate,
                    info.nChans == 2 ? "stereo" : "mono", info.bitrate / 1000);
    }
    setRate(info.samprate);
    const size_t samples = toStereo(pcm, info.outputSamps, info.nChans);
    i2s.write(reinterpret_cast<uint8_t *>(pcm), samples * sizeof(int16_t));

    report();
  }
  f.close();
  report(true);
  return true;
}

size_t trackIndex = 0;

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(KEY_PIN, INPUT_PULLUP);
  delay(500);

  if (!LittleFS.begin(false)) {
    Serial.println("No LittleFS filesystem: select the partition scheme \"No OTA "
                   "(1MB APP/3MB SPIFFS)\", flash again, then run upload-data.sh.");
    while (true) {
      delay(1000);
    }
  }
  loadPlaylist();
  Serial.printf("%u tracks; filesystem %u of %u KB used\n",
                static_cast<unsigned>(trackCount),
                static_cast<unsigned>(LittleFS.usedBytes() / 1024),
                static_cast<unsigned>(LittleFS.totalBytes() / 1024));

  decoder = MP3InitDecoder();
  if (!decoder) {
    Serial.println("Not enough memory for the MP3 decoder");
    while (true) {
      delay(1000);
    }
  }

  i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);
  if (!i2s.begin(I2S_MODE_STD, 44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("Failed to start I2S");
    while (true) {
      delay(1000);
    }
  }
  i2sRate = 44100;
  lastReportUs = micros();
}

void loop() {
  if (trackCount == 0) {
    Serial.println("No .mp3 files found; copy some to data/ and run upload-data.sh.");
    delay(5000);
    return;
  }
  if (!playFile(tracks[trackIndex])) {
    Serial.println("  skipped");
  }
  trackIndex = (trackIndex + 1) % trackCount;
}
