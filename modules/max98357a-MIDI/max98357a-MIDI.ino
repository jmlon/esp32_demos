// MIDI song player for the ESP32-C3 and the MAX98357A, synthesized with LEAF
// (https://github.com/spiricom/LEAF): 2-operator FM voices plus a few simple
// drums.  A port of stm32_blackpill/audio/midi, scaled down for the C3.
//
// The ESP32-C3 has no FPU, and LEAF computes in single-precision float, so
// every addition and multiplication is a software-emulated libgcc call
// (measured at 160 MHz: about 110 cycles per addition, 140 per
// multiplication).  The Black Pill plays 8 FM voices at 32 kHz; the C3 plays
// 4 at 12 kHz, with the envelopes at a control rate (see CONTROL_FRAMES).
// An FM voice then costs about 2100 cycles per sample, a drum 900 to 3100,
// out of 13300.  The sample song averages up to 88 % of the CPU.  The load
// report shows how much is left; raise MELODIC_VOICES only while the average
// load stays well below 100 %.  Notes beyond MELODIC_VOICES steal the oldest
// one.  Building with -O2 measured no faster than the core's default -Os.
//
// The song is not parsed on the board.  stm32_blackpill/tools/midi2h.py turns
// a Standard MIDI File into song.h, a flat list of timed note events; to play
// another song, regenerate it:
//   python3 tools/midi2h.py path/to/song.mid -o <this folder>/song.h
// then rebuild.  The song plays once; press BOOT (GPIO9) to play it again.
// The title, then the CPU load, voice use and clipping, are printed on the
// USB serial port (/dev/ttyACM0).
//
// Instruments: each MIDI channel's General MIDI program picks one of a dozen
// 2-operator FM patches (piano, mallets, bells, organ, plucked, bass,
// strings, voices, brass, reeds, flutes, synth leads, steel drums).
// Channel 10 plays drums: kicks, snares, clap, hi-hats, toms and cymbals,
// each built from LEAF noise, sine, filter and envelope objects.
//
// Board: ESP32C3 Dev Module
// USB CDC On Boot: Enabled
// Upload Mode: UART0/Hardware CDC
// USB Mode: Hardware CDC and JTAG
// With arduino-cli use: -b esp32:esp32:esp32c3:CDCOnBoot=cdc
//
// Wiring, the same as modules/max98357a (ESP32-C3 -> MAX98357A):
//   BCLK -> GPIO3, LRC -> GPIO1, DIN -> GPIO10, VIN -> 5V, GND -> GND
// Every sample goes to both slots, so one board with SD open, or the left
// and right boards of modules/max98357a, all play the whole song.
//
// Required libraries:
//   - ESP_I2S (bundled with the ESP32 Arduino core 3.x)
//   - LEAF, not in the Arduino library index; install it once with
//     stm32_blackpill/tools/install-leaf.sh

#include <Arduino.h>
#include "ESP_I2S.h"
#include "esp_cpu.h"
#include <leaf.h>
#include "song.h"

namespace {

// ---------------------------------------------------------------------------
// I2S output

constexpr int8_t I2S_BCLK = 3;
constexpr int8_t I2S_LRC = 1;
constexpr int8_t I2S_DIN = 10;
constexpr uint8_t KEY_PIN = 9;  // BOOT button, LOW while pressed

constexpr uint32_t SAMPLE_RATE = 12000;
static_assert(SAMPLE_RATE % 1000 == 0, "song times are whole milliseconds");
constexpr uint32_t SAMPLES_PER_MS = SAMPLE_RATE / 1000;

// loop() renders one block and hands it to i2s.write(), which blocks until
// the driver's DMA ring (6 x 240 frames, 120 ms) has room for it.  Song
// events are applied once per block, so their timing is accurate to a block
// (5 ms).
constexpr uint32_t BLOCK_FRAMES = 64;

I2SClass i2s;
int16_t block[BLOCK_FRAMES * 2];

// ---------------------------------------------------------------------------
// LEAF

LEAF leaf;

// Every LEAF object lives in this pool, allocated at init (LEAF avoids
// malloc).  The load report shows how much of it is used.
char leafMemory[8192];

// Noise for the drums: tNoise calls this every sample, so it must be cheap.
// xorshift32 (Marsaglia), scaled to 0..1.
Lfloat randomNumber() {
  static uint32_t state = 2463534242u;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return static_cast<Lfloat>(state >> 8) * (1.0f / 16777216.0f);
}

// ---------------------------------------------------------------------------
// Filters (tBiQuad with RBJ Audio EQ Cookbook coefficients; LEAF's tSVF
// would link 32 KB of lookup tables)

void setBandPass(tBiQuad f, float hz, float q) {
  const float w0 = 2.0f * PI * hz / SAMPLE_RATE;
  const float cosW0 = cosf(w0);
  const float alpha = sinf(w0) / (2.0f * q);
  const float a0 = 1.0f + alpha;
  tBiQuad_setCoefficients(f, alpha / a0, 0.0f, -alpha / a0,
                          -2.0f * cosW0 / a0, (1.0f - alpha) / a0);
}

void setHighPass(tBiQuad f, float hz, float q) {
  const float w0 = 2.0f * PI * hz / SAMPLE_RATE;
  const float cosW0 = cosf(w0);
  const float alpha = sinf(w0) / (2.0f * q);
  const float a0 = 1.0f + alpha;
  const float b0 = (1.0f + cosW0) / (2.0f * a0);
  tBiQuad_setCoefficients(f, b0, -2.0f * b0, b0, -2.0f * cosW0 / a0,
                          (1.0f - alpha) / a0);
}

// ---------------------------------------------------------------------------
// 2-operator FM voice (as in stm32_blackpill/audio/fmsynth_leaf)
//
// The modulator shifts the carrier's phase:
//   out = amp(t) * sin(2 pi fc t + I(t) * sin(2 pi fm t))
// with fc and fm the note frequency times each operator's ratio and I(t)
// the modulation index in radians.  Velocity scales both the loudness and
// the index, so harder notes are also brighter.  detuneCents adds a second
// carrier a few cents off, for the shimmer of bells and steel drums.

struct Envelope {
  float attackMs;
  float decayMs;
  float sustain;
  float releaseMs;
};

struct FmPatch {
  float carrierRatio;
  float modulatorRatio;
  float index;        // peak modulation index I, in radians
  float detuneCents;  // second carrier's offset; 0 = single carrier
  Envelope amp;
  Envelope indexEnv;
};

//                               c:m ratios   I   detune  amp {A, D, S, R}         index {A, D, S, R}
constexpr FmPatch EPIANO     = {1.0f, 14.0f,  1.5f, 7, {2, 2500, 0, 400},       {1, 200, 0, 100}};
constexpr FmPatch MALLET     = {1.0f,  4.0f,  3.0f, 0, {1, 700, 0, 200},        {1, 40, 0, 40}};
constexpr FmPatch BELL       = {1.0f,  1.4f,  8.0f, 3, {2, 3000, 0, 500},       {1, 1500, 0, 500}};
constexpr FmPatch ORGAN      = {1.0f,  1.0f,  1.2f, 0, {8, 50, 0.9f, 80},       {8, 50, 1.0f, 80}};
constexpr FmPatch PLUCK      = {1.0f,  3.0f,  2.5f, 0, {1, 1500, 0, 250},       {1, 300, 0.1f, 200}};
constexpr FmPatch BASS       = {1.0f,  1.0f,  4.0f, 0, {2, 800, 0.59f, 150},    {1, 300, 0.24f, 150}};
constexpr FmPatch STRINGS    = {1.0f,  1.0f,  1.5f, 0, {150, 300, 0.8f, 500},   {300, 500, 0.6f, 500}};
constexpr FmPatch VOICES     = {1.0f,  1.0f,  0.8f, 0, {120, 300, 0.85f, 400},  {150, 300, 0.7f, 400}};
constexpr FmPatch BRASS      = {1.0f,  1.0f,  5.0f, 0, {60, 150, 0.78f, 150},   {60, 150, 0.78f, 150}};
constexpr FmPatch REED       = {3.0f,  2.0f,  2.0f, 0, {50, 100, 0.86f, 120},   {50, 100, 0.63f, 120}};
constexpr FmPatch FLUTE      = {1.0f,  1.0f,  0.5f, 0, {60, 100, 0.9f, 120},    {80, 100, 0.6f, 120}};
constexpr FmPatch SYNTH_LEAD = {1.0f,  1.0f,  3.0f, 0, {5, 100, 0.9f, 150},     {5, 200, 0.8f, 150}};
// After the DX7's STEEL DRUM: a carrier modulated at 2x with a fast index
// decay, and two carriers a few cents apart.
constexpr FmPatch STEEL_DRUM = {1.0f,  2.0f,  2.0f, 3, {1, 2500, 0, 400},       {1, 400, 0.15f, 300}};

// General MIDI programs come in families of 8.
const FmPatch *patchForProgram(uint8_t program) {
  switch (program) {
    case 11: case 12: case 13: case 15:  // vibraphone, marimba, xylophone,
      return &MALLET;                    // dulcimer
    case 114:
      return &STEEL_DRUM;
    case 52: case 53: case 54: case 85: case 91:  // choir, voice, synth voice
      return &VOICES;
    default:
      break;
  }
  switch (program / 8) {
    case 0: return &EPIANO;      // pianos
    case 1: return &BELL;        // chromatic percussion
    case 2: return &ORGAN;       // organs, accordion, harmonica
    case 3: return &PLUCK;       // guitars
    case 4: return &BASS;        // basses
    case 5: return &STRINGS;     // strings, harp, timpani
    case 6: return &STRINGS;     // ensembles
    case 7: return &BRASS;       // brass
    case 8: return &REED;        // reeds
    case 9: return &FLUTE;       // pipes
    case 10: return &SYNTH_LEAD; // synth leads
    case 11: return &STRINGS;    // synth pads
    case 12: return &STRINGS;    // synth effects
    case 13: return &PLUCK;      // ethnic (sitar, banjo, koto, ...)
    case 14: return &MALLET;     // percussive
    default: return &EPIANO;     // sound effects
  }
}

// Envelopes run at a control rate, once every CONTROL_FRAMES samples, and
// each sample steps linearly towards the next control value.  On the C3 a
// tADSRS_tick costs about 700 cycles, a fifth of an FM voice; an addition
// per sample costs about 110.  A 0.67 ms control period still resolves the
// fastest attacks of the patches (1 ms).
constexpr uint32_t CONTROL_FRAMES = 8;
static_assert(BLOCK_FRAMES % CONTROL_FRAMES == 0, "blocks hold whole control periods");
constexpr float CONTROL_RATE = static_cast<float>(SAMPLE_RATE) / CONTROL_FRAMES;
constexpr float CONTROL_STEP = 1.0f / CONTROL_FRAMES;

void initControlEnvelope(tADSRS *env, float attackMs, float decayMs,
                         float sustain, float releaseMs) {
  tADSRS_init(env, attackMs, decayMs, sustain, releaseMs, &leaf);
  tADSRS_setSampleRate(*env, CONTROL_RATE);
  // tADSRS glides to a new velocity by a fixed fraction (0.01) per tick, not
  // per millisecond; keep the glide time of a per-sample envelope.
  (*env)->factor = 1.0f - powf(0.99f, CONTROL_FRAMES);
  (*env)->oneMinusFactor = 1.0f - (*env)->factor;
}

// An envelope has ended once it is idle, or holds a sustain level of 0: a
// decaying patch (piano, mallets, drums) sits in its sustain stage until the
// note-off, which drums never get, but there is nothing left to hear.
bool envelopeDone(tADSRS env) {
  return env->state == env_idle ||
         (env->state == env_sustain && env->sustainLevel <= 0);
}

void applyEnvelope(tADSRS env, const Envelope &e) {
  tADSRS_setAttack(env, e.attackMs);
  tADSRS_setDecay(env, e.decayMs);
  tADSRS_setSustain(env, e.sustain);
  tADSRS_setRelease(env, e.releaseMs);
}

constexpr float DETUNE_MIX = 0.5f;

// tCycle has no phase-modulation input, but its phase accumulator is a
// public field (a uint32_t, one cycle = 2^32): shifting it around one tick
// reads the sine at phase + offset.  The offset goes through Q8.24, so it
// costs one multiplication and one float-to-int conversion; |cycles| < 128
// fits.
float tickWithPhaseOffset(tCycle osc, float cycles) {
  const uint32_t offset =
      static_cast<uint32_t>(static_cast<int32_t>(cycles * 16777216.0f)) << 8;
  osc->phase += offset;
  const float out = tCycle_tick(osc);
  osc->phase -= offset;
  return out;
}

struct Voice {
  tCycle carrier;
  tCycle carrier2;
  tCycle modulator;
  tADSRS amp;
  tADSRS index;
  const FmPatch *patch = nullptr;
  float indexCycles = 0;  // I / (2 pi): phase offset at full index envelope
  float ampLevel = 0;     // envelope values, stepped once per sample
  float ampStep = 0;
  float phaseModLevel = 0;  // indexCycles * index envelope
  float phaseModStep = 0;
  uint32_t startedAt = 0; // sample clock of the note-on, for stealing
  uint8_t channel = 0;
  uint8_t note = 0;
  bool held = false;      // between note-on and note-off

  void init() {
    tCycle_init(&carrier, &leaf);
    tCycle_init(&carrier2, &leaf);
    tCycle_init(&modulator, &leaf);
    initControlEnvelope(&amp, 1, 1, 0, 1);
    initControlEnvelope(&index, 1, 1, 0, 1);
  }

  void noteOn(uint8_t ch, uint8_t n, float velocity, const FmPatch *p,
              uint32_t now) {
    if (p != patch) {  // the envelope setters are relatively costly
      patch = p;
      applyEnvelope(amp, p->amp);
      applyEnvelope(index, p->indexEnv);
      indexCycles = p->index / (2.0f * PI);
    }
    const float hz = LEAF_midiToFrequency(n);
    tCycle_setFreq(carrier, hz * p->carrierRatio);
    tCycle_setFreq(carrier2, hz * p->carrierRatio * exp2f(p->detuneCents / 1200.0f));
    tCycle_setFreq(modulator, hz * p->modulatorRatio);
    // tADSRS squares its velocity argument: amplitude follows velocity
    // squared, the index velocity itself.  Both envelopes rise from their
    // current level, so a stolen voice glides into the new note.
    tADSRS_on(amp, velocity);
    tADSRS_on(index, sqrtf(velocity));
    channel = ch;
    note = n;
    held = true;
    startedAt = now;
  }

  void noteOff() {
    tADSRS_off(amp);
    tADSRS_off(index);
    held = false;
  }

  bool silent() const { return envelopeDone(amp); }

  // Once per control period, before its CONTROL_FRAMES ticks.
  void control() {
    ampStep = (tADSRS_tick(amp) - ampLevel) * CONTROL_STEP;
    phaseModStep =
        (indexCycles * tADSRS_tick(index) - phaseModLevel) * CONTROL_STEP;
  }

  float tick() {
    ampLevel += ampStep;
    phaseModLevel += phaseModStep;
    const float phaseMod = phaseModLevel * tCycle_tick(modulator);
    float out = tickWithPhaseOffset(carrier, phaseMod);
    if (patch->detuneCents != 0) {
      out = (out + DETUNE_MIX * tickWithPhaseOffset(carrier2, phaseMod)) *
            (1.0f / (1.0f + DETUNE_MIX));
    }
    return out * ampLevel;
  }
};

// The sample song needs up to 7 at once; with fewer voices its densest
// chords steal notes, which the end-of-song report counts.
constexpr uint8_t MELODIC_VOICES = 4;
Voice voices[MELODIC_VOICES];
uint32_t stolenVoices = 0;

// A note of a channel goes to the voice already playing that note (a
// retrigger), else to a silent voice, else to the voice that was released
// the longest ago, else to the oldest held note.
Voice &allocateVoice(uint8_t channel, uint8_t note) {
  Voice *released = nullptr;
  Voice *oldest = &voices[0];
  for (Voice &v : voices) {
    if (!v.silent() && v.channel == channel && v.note == note) {
      return v;
    }
  }
  for (Voice &v : voices) {
    if (v.silent()) {
      return v;
    }
    if (!v.held && (!released || static_cast<int32_t>(v.startedAt - released->startedAt) < 0)) {
      released = &v;
    }
    if (static_cast<int32_t>(v.startedAt - oldest->startedAt) < 0) {
      oldest = &v;
    }
  }
  if (released) {
    return *released;
  }
  ++stolenVoices;
  return *oldest;
}

// ---------------------------------------------------------------------------
// Drums (General MIDI channel 10)
//
// Every drum is the same small recipe with different settings: a sine whose
// pitch drops from toneHz + dropHz to toneHz (kicks and toms), plus white
// noise through a band-pass or high-pass filter (snares, clap, hi-hats,
// cymbals), under an exponential decay.  Drums ignore note-offs.  Each kind
// has its own voice, so a hit cuts the previous one of the same kind; the
// open hi-hat is cut by the closed one, as on a real kit.
//
// At 12 kHz there is nothing above 6 kHz, so the hi-hat and cymbal filters
// sit 2.5 to 3 kHz lower than on the Black Pill.  The kick has no noise
// click: at 5 % level it cost as much as the rest of the kick.

enum class NoiseFilter : uint8_t { BandPass, HighPass };

struct DrumSound {
  float toneHz;
  float dropHz;      // extra pitch at the hit, decaying with dropMs
  float dropMs;
  float toneLevel;
  float noiseLevel;
  NoiseFilter filter;
  float filterHz;
  float filterQ;
  float decayMs;
};

//                              tone   drop  dropMs tone  noise  filter                  Hz     Q      decay
constexpr DrumSound KICK       = {48,   120,  40,    1.0f, 0,     NoiseFilter::HighPass,  3000, 0.7f,  350};
constexpr DrumSound SIDE_STICK = {420,  0,    1,     0.4f, 0.5f,  NoiseFilter::BandPass,  2500, 1.0f,  40};
constexpr DrumSound SNARE      = {185,  40,   20,    0.35f, 0.9f, NoiseFilter::BandPass,  3000, 0.6f,  180};
constexpr DrumSound CLAP       = {0,    0,    1,     0,    1.0f,  NoiseFilter::BandPass,  1200, 1.5f,  150};
constexpr DrumSound CLOSED_HAT = {0,    0,    1,     0,    0.8f,  NoiseFilter::HighPass,  4000, 0.7f,  45};
constexpr DrumSound OPEN_HAT   = {0,    0,    1,     0,    0.8f,  NoiseFilter::HighPass,  4000, 0.7f,  350};
constexpr DrumSound CRASH      = {0,    0,    1,     0,    0.7f,  NoiseFilter::HighPass,  2500, 0.7f,  1200};
constexpr DrumSound RIDE       = {0,    0,    1,     0,    0.5f,  NoiseFilter::HighPass,  3500, 1.0f,  800};

struct DrumVoice {
  tCycle tone;
  tNoise noise;
  tBiQuad filter;
  tADSRS amp;
  tADSRS pitch;
  const DrumSound *sound = nullptr;
  float toneHz = 0;
  float ampLevel = 0;  // stepped once per sample, as in Voice
  float ampStep = 0;

  void init() {
    tCycle_init(&tone, &leaf);
    tNoise_init(&noise, WhiteNoise, &leaf);
    tBiQuad_init(&filter, &leaf);
    initControlEnvelope(&amp, 0.5f, 100, 0, 5);
    initControlEnvelope(&pitch, 0.1f, 40, 0, 5);
  }

  // tonePitchHz overrides the sound's tone (toms are tuned per note).
  void hit(const DrumSound &s, float velocity, float tonePitchHz = 0) {
    if (&s != sound) {
      sound = &s;
      tADSRS_setDecay(amp, s.decayMs);
      tADSRS_setDecay(pitch, s.dropMs);
      if (s.filter == NoiseFilter::BandPass) {
        setBandPass(filter, s.filterHz, s.filterQ);
      } else {
        setHighPass(filter, s.filterHz, s.filterQ);
      }
    }
    toneHz = tonePitchHz > 0 ? tonePitchHz : s.toneHz;
    tCycle_setPhase(tone, 0);  // every kick starts the same way
    tADSRS_on(amp, velocity);
    tADSRS_on(pitch, 1.0f);
  }

  bool silent() const { return envelopeDone(amp); }

  // Once per control period; the pitch drop moves in 0.67 ms steps.
  void control() {
    ampStep = (tADSRS_tick(amp) - ampLevel) * CONTROL_STEP;
    if (sound->toneLevel != 0) {
      tCycle_setFreq(tone, toneHz + sound->dropHz * tADSRS_tick(pitch));
    }
  }

  float tick() {
    ampLevel += ampStep;
    float out = 0;
    if (sound->toneLevel != 0) {
      out = sound->toneLevel * tCycle_tick(tone);
    }
    if (sound->noiseLevel != 0) {
      out += sound->noiseLevel * tBiQuad_tick(filter, tNoise_tick(noise));
    }
    return out * ampLevel;
  }
};

enum DrumSlot : uint8_t { KICK_SLOT, SNARE_SLOT, CLAP_SLOT, HAT_SLOT, TOM_SLOT, CYMBAL_SLOT, DRUM_SLOTS };
DrumVoice drums[DRUM_SLOTS];

void playDrum(uint8_t note, float velocity) {
  switch (note) {
    case 35: case 36: drums[KICK_SLOT].hit(KICK, velocity); break;
    case 37: drums[SNARE_SLOT].hit(SIDE_STICK, velocity); break;
    case 38: case 40: drums[SNARE_SLOT].hit(SNARE, velocity); break;
    case 39: drums[CLAP_SLOT].hit(CLAP, velocity); break;
    case 42: case 44: drums[HAT_SLOT].hit(CLOSED_HAT, velocity); break;
    case 46: drums[HAT_SLOT].hit(OPEN_HAT, velocity); break;
    case 49: case 52: case 55: case 57: drums[CYMBAL_SLOT].hit(CRASH, velocity); break;
    case 51: case 59: drums[CYMBAL_SLOT].hit(RIDE, velocity); break;
    case 41: case 43: case 45: case 47: case 48: case 50:
      // Low floor tom (41) to high tom (50), about 70 to 200 Hz.
      drums[TOM_SLOT].hit(KICK, velocity, 70.0f * exp2f((note - 41) / 6.0f));
      break;
    default: break;  // tools/midi2h.py already dropped the others
  }
}

// ---------------------------------------------------------------------------
// Song player

constexpr uint8_t DRUM_CHANNEL = 9;

uint8_t channelProgram[16];
uint32_t eventIndex = 0;
uint32_t nextEventMs = 0;
uint32_t songStartSample = 0;
uint32_t sampleClock = 0;  // frames rendered so far
bool playing = false;

// The song plays once, then waits for the BOOT button.  A press only counts
// after the button has been seen released, so holding BOOT as the song ends
// does not restart it, and contact bounce cannot either.
bool keyWasUp = false;
bool songStarted = false;
bool songFinished = false;

bool keyPressed() {
  const bool down = digitalRead(KEY_PIN) == LOW;
  const bool pressed = down && keyWasUp;
  keyWasUp = !down;
  return pressed;
}

void startSong() {
  for (uint8_t &p : channelProgram) {
    p = 0;  // GM default: Acoustic Grand Piano
  }
  eventIndex = 0;
  nextEventMs = SONG_EVENT_COUNT ? SONG_EVENTS[0].deltaMs : 0;
  songStartSample = sampleClock;
  stolenVoices = 0;
  playing = true;
  songStarted = true;
}

void noteOff(uint8_t channel, uint8_t note) {
  for (Voice &v : voices) {
    if (v.held && v.channel == channel && v.note == note) {
      v.noteOff();
    }
  }
}

void applyEvent(const SongEvent &e) {
  if (e.channel == SONG_NOP) {
    return;
  }
  if (e.channel & SONG_PROGRAM) {
    channelProgram[e.channel & 0x0F] = e.note;
    return;
  }
  const float velocity = e.velocity / 127.0f;
  if (e.channel == DRUM_CHANNEL) {
    if (e.velocity) {
      playDrum(e.note, velocity);
    }
  } else if (e.velocity) {
    allocateVoice(e.channel, e.note)
        .noteOn(e.channel, e.note, velocity,
                patchForProgram(channelProgram[e.channel]), sampleClock);
  } else {
    noteOff(e.channel, e.note);
  }
}

bool anythingSounding() {
  for (const Voice &v : voices) {
    if (!v.silent()) return true;
  }
  for (const DrumVoice &d : drums) {
    if (!d.silent()) return true;
  }
  return false;
}

// Called once per block, before rendering it.
void runSong() {
  if (!playing) {
    if (keyPressed()) {
      startSong();
    }
    return;
  }
  const uint32_t nowMs = (sampleClock - songStartSample) / SAMPLES_PER_MS;
  while (eventIndex < SONG_EVENT_COUNT && nextEventMs <= nowMs) {
    applyEvent(SONG_EVENTS[eventIndex++]);
    if (eventIndex < SONG_EVENT_COUNT) {
      nextEventMs += SONG_EVENTS[eventIndex].deltaMs;
    }
  }
  if (eventIndex >= SONG_EVENT_COUNT && !anythingSounding()) {
    playing = false;
    keyWasUp = false;
    songFinished = true;
  }
}

// ---------------------------------------------------------------------------
// Rendering

// Output gain: a single full-velocity voice peaks at 45 % of full scale.
// Dense passages can sum past full scale; those samples are clipped and
// counted in the load report.
constexpr float OUTPUT_GAIN = 0.45f;
uint32_t clippedSamples = 0;
uint8_t peakVoicesInUse = 0;

void renderBlock(int16_t *out) {
  runSong();

  // Only sounding voices are computed.  One started by runSong() above is
  // already in its attack; one that ends during the block adds zeros.
  Voice *active[MELODIC_VOICES];
  uint8_t activeCount = 0;
  for (Voice &v : voices) {
    if (!v.silent()) active[activeCount++] = &v;
  }
  DrumVoice *activeDrums[DRUM_SLOTS];
  uint8_t drumCount = 0;
  for (DrumVoice &d : drums) {
    if (!d.silent()) activeDrums[drumCount++] = &d;
  }
  if (activeCount > peakVoicesInUse) peakVoicesInUse = activeCount;

  constexpr float SCALE = 32767.0f * OUTPUT_GAIN;
  for (uint32_t i = 0; i < BLOCK_FRAMES; ++i) {
    if (i % CONTROL_FRAMES == 0) {
      for (uint8_t k = 0; k < activeCount; ++k) active[k]->control();
      for (uint8_t k = 0; k < drumCount; ++k) activeDrums[k]->control();
    }
    float mix = 0;
    for (uint8_t k = 0; k < activeCount; ++k) mix += active[k]->tick();
    for (uint8_t k = 0; k < drumCount; ++k) mix += activeDrums[k]->tick();
    float scaled = mix * SCALE;
    if (scaled > 32767.0f || scaled < -32767.0f) {
      scaled = scaled > 0 ? 32767.0f : -32767.0f;
      ++clippedSamples;
    }
    const int16_t sample = static_cast<int16_t>(scaled);
    out[2 * i] = sample;
    out[2 * i + 1] = sample;
  }
  sampleClock += BLOCK_FRAMES;
}

// ---------------------------------------------------------------------------
// Reporting, with the CPU cycle counter for the render time

uint32_t busyCycles = 0;
uint32_t peakBlockCycles = 0;

void report() {
  static uint32_t lastUs = micros();
  static uint32_t lastMs = 0;

  if (songStarted) {
    songStarted = false;
    Serial.print("Playing: ");
    Serial.print(SONG_TITLE);
    Serial.print(" (");
    Serial.print(SONG_LENGTH_MS / 1000.0f, 1);
    Serial.println(" s)");
  }
  if (songFinished) {
    songFinished = false;
    Serial.print("Song finished, ");
    Serial.print(stolenVoices);
    Serial.println(" notes stolen; press BOOT (GPIO9) to play it again.");
  }

  const uint32_t nowMs = millis();
  if (nowMs - lastMs < 2000) {
    return;
  }
  lastMs = nowMs;

  // Average load = cycles spent rendering / cycles elapsed; peak = the
  // slowest block against the time one block plays.  The elapsed time comes
  // from micros(): the cycle counter stops while the idle task waits for an
  // interrupt, as it does whenever i2s.write() blocks.  A peak above 100 % is
  // absorbed by the DMA ring; an average near 100 % means the ring runs dry
  // and the audio stutters.
  const uint32_t nowUs = micros();
  const float cpuMhz = getCpuFrequencyMhz();
  const float avg = 100.0f * busyCycles / ((nowUs - lastUs) * cpuMhz);
  const float peak = 100.0f * peakBlockCycles /
                     (cpuMhz * 1000000.0f / SAMPLE_RATE * BLOCK_FRAMES);
  lastUs = nowUs;
  busyCycles = 0;
  peakBlockCycles = 0;

  Serial.print("  CPU load: avg ");
  Serial.print(avg, 1);
  Serial.print(" %, peak ");
  Serial.print(peak, 1);
  Serial.print(" %; voices: ");
  Serial.print(peakVoicesInUse);
  Serial.print(" of ");
  Serial.print(MELODIC_VOICES);
  Serial.print("; clipped samples: ");
  Serial.print(clippedSamples);
  Serial.print("; LEAF pool: ");
  Serial.print(leaf_pool_get_used(&leaf));
  Serial.print(" of ");
  Serial.print(sizeof(leafMemory));
  Serial.println(" bytes");
  peakVoicesInUse = 0;
  clippedSamples = 0;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(KEY_PIN, INPUT_PULLUP);

  LEAF_init(&leaf, SAMPLE_RATE, leafMemory, sizeof(leafMemory), randomNumber);
  for (Voice &v : voices) {
    v.init();
  }
  for (DrumVoice &d : drums) {
    d.init();
  }

  i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("Failed to start I2S");
    while (true) {
      delay(1000);
    }
  }

  // The MAX98357A powers up when it detects BCLK and needs a few
  // milliseconds to settle: start with 20 ms of silence.
  for (uint32_t ms = 0; ms < 20; ms += BLOCK_FRAMES / SAMPLES_PER_MS) {
    i2s.write(block, sizeof(block));
  }

  startSong();
}

void loop() {
  const uint32_t t0 = esp_cpu_get_cycle_count();
  renderBlock(block);
  const uint32_t cycles = esp_cpu_get_cycle_count() - t0;
  busyCycles += cycles;
  if (cycles > peakBlockCycles) {
    peakBlockCycles = cycles;
  }

  // Blocks until the DMA ring has room for the whole block.
  i2s.write(block, sizeof(block));

  report();
}
