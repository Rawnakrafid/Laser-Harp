#include <Arduino.h>
#include <driver/i2s.h>
#include "notes_data.h"

/* ------------------------------------------------------------------
 * Laser Harp - ESP32 audio engine: Multi-Scale Polyphonic Resampler
 * 
 * Switchable Scales via Push Button on GPIO 18 (D18 -> GND):
 *   Scale 0: C Major (Baseline Diatonic: C4, D4, E4, F4, G4, A4, B4)
 *   Scale 1: D Natural Minor / Tokyo Drift (D4, E4, F4, G4, A4, Bb4, C5)
 *   Scale 2: C Minor Blues (C4, Eb4, F4, F#4, G4, Bb4, C5)
 *   Scale 3: C Arabic / Phrygian Dominant (C4, Db4, E4, F4, G4, Ab4, Bb4)
 *   Scale 4: C Lydian / Sci-Fi Dream (C4, D4, E4, F#4, G4, A4, B4)
 *
 * Tokyo Drift Hook on Scale 1:
 *   Notes: D4 - D4 - D4 - F4 - D4 - D4 - C5 - D4 - Bb4 - A4
 *   Beams:  0 -  0 -  0 -  2 -  0 -  0 -  6 -  0 -   5 -  4
 *
 * Behavior:
 * 1. Triggers sound instantly when a beam is BLOCKED (light interrupted).
 * 2. Push button on GPIO 18 cycles through the 5 scales.
 * 3. Fixed-point 16.16 phase-accumulator resampling for pitch-accurate accidentals (Bb4, C5, F#4, etc.).
 * 4. Zero-Latency Start: Skips lead-in silence in audio samples.
 * 5. One-Shot Natural Decay: Notes decay naturally without looping.
 * ------------------------------------------------------------------ */

#define ATMEGA_RX_PIN     16
#define ATMEGA_TX_PIN     17
#define SCALE_BUTTON_PIN  18
#define NUM_BEAMS         7

// Set to 0 if your circuit sends 0 when light is blocked (light lost).
// Set to 1 if your circuit sends 1 when light is blocked.
#define BEAM_BLOCKED_VALUE 0

#define I2S_SAMPLE_RATE   8000
#define I2S_DMA_BUF_COUNT 8
#define I2S_DMA_BUF_LEN   256

// Fixed-point 16.16 phase steps:
// 1.000000x (Natural pitch)   = round(1.000000 * 65536) = 65536
// +1 Semitone (2^(1/12))      = round(1.059463 * 65536) = 69433
// 2.000000x (+1 Octave, C5)   = round(2.000000 * 65536) = 131072

#define FP_STEP_NATURAL  65536u
#define FP_STEP_SEMI_UP  69433u
#define FP_STEP_OCT_UP  131072u

// Lead-in silence offset (samples) for instant note attack
const uint32_t NOTE_START_OFFSETS[NUM_BEAMS] = {
  1440, // C4 (Base 0)
  1550, // D4 (Base 1)
  1435, // E4 (Base 2)
  1450, // F4 (Base 3)
  1490, // G4 (Base 4)
  1450, // A4 (Base 5)
  1450  // B4 (Base 6)
};

struct ScaleNote {
  uint8_t     baseSample; // 0..6 -> C4..B4 in NOTE_TABLE
  uint32_t    step_fp;    // 16.16 fixed-point phase increment
  const char* name;       // Display name
};

struct ScaleDefinition {
  const char*       name;
  const char*       description;
  ScaleNote         notes[NUM_BEAMS];
};

#define NUM_SCALES 5

const ScaleDefinition SCALES[NUM_SCALES] = {
  // Scale 0: C Major (Baseline)
  {
    "C Major (Standard)",
    "Natural diatonic scale for classic nursery & folk songs",
    {
      { 0, FP_STEP_NATURAL, "C4" },
      { 1, FP_STEP_NATURAL, "D4" },
      { 2, FP_STEP_NATURAL, "E4" },
      { 3, FP_STEP_NATURAL, "F4" },
      { 4, FP_STEP_NATURAL, "G4" },
      { 5, FP_STEP_NATURAL, "A4" },
      { 6, FP_STEP_NATURAL, "B4" }
    }
  },

  // Scale 1: D Natural Minor (Tokyo Drift / Anime)
  {
    "D Minor (Tokyo Drift / Anime)",
    "Tokyo Drift Riff: Beams 0-0-0-2-0-0-6-0-5-4 (D-D-D-F-D-D-C-D-Bb-A)",
    {
      { 1, FP_STEP_NATURAL, "D4" },
      { 2, FP_STEP_NATURAL, "E4" },
      { 3, FP_STEP_NATURAL, "F4" },
      { 4, FP_STEP_NATURAL, "G4" },
      { 5, FP_STEP_NATURAL, "A4" },
      { 5, FP_STEP_SEMI_UP, "Bb4" },  // A4 + 1 semitone = Bb4
      { 0, FP_STEP_OCT_UP,  "C5"  }   // C4 + 1 octave   = C5
    }
  },

  // Scale 2: C Minor Blues (Rock / Jam)
  {
    "C Minor Blues (Rock & Blues)",
    "Blues scale with flattened 3rd, 5th, and 7th",
    {
      { 0, FP_STEP_NATURAL, "C4"  },
      { 1, FP_STEP_SEMI_UP, "Eb4" },  // D4 + 1 semitone = Eb4
      { 3, FP_STEP_NATURAL, "F4"  },
      { 3, FP_STEP_SEMI_UP, "F#4" },  // F4 + 1 semitone = F#4
      { 4, FP_STEP_NATURAL, "G4"  },
      { 5, FP_STEP_SEMI_UP, "Bb4" },  // A4 + 1 semitone = Bb4
      { 0, FP_STEP_OCT_UP,  "C5"  }   // C4 + 1 octave   = C5
    }
  },

  // Scale 3: C Arabic / Phrygian Dominant (Exotic Harp)
  {
    "C Phrygian Dominant (Exotic Arabic)",
    "Mysterious Middle Eastern scale with Db and Bb",
    {
      { 0, FP_STEP_NATURAL, "C4"  },
      { 0, FP_STEP_SEMI_UP, "Db4" },  // C4 + 1 semitone = Db4
      { 2, FP_STEP_NATURAL, "E4"  },
      { 3, FP_STEP_NATURAL, "F4"  },
      { 4, FP_STEP_NATURAL, "G4"  },
      { 4, FP_STEP_SEMI_UP, "Ab4" },  // G4 + 1 semitone = Ab4
      { 5, FP_STEP_SEMI_UP, "Bb4" }   // A4 + 1 semitone = Bb4
    }
  },

  // Scale 4: C Lydian (Dreamy / Sci-Fi)
  {
    "C Lydian (Dreamy & Sci-Fi)",
    "Cinematic dream scale with raised 4th (F#)",
    {
      { 0, FP_STEP_NATURAL, "C4"  },
      { 1, FP_STEP_NATURAL, "D4"  },
      { 2, FP_STEP_NATURAL, "E4"  },
      { 3, FP_STEP_SEMI_UP, "F#4" },  // F4 + 1 semitone = F#4
      { 4, FP_STEP_NATURAL, "G4"  },
      { 5, FP_STEP_NATURAL, "A4"  },
      { 6, FP_STEP_NATURAL, "B4"  }
    }
  }
};

uint8_t  currentScale = 0;      // 0..4
uint8_t  lastMask     = 0xFF;
int8_t   currentBeam  = -1;     // -1 = silence/idle, 0..6 = active beam
uint32_t currentPos_fp = 0;     // 16.16 fixed point playback position

static inline bool isBeamBlocked(uint8_t mask, uint8_t beam) {
#if BEAM_BLOCKED_VALUE == 0
  return ((mask & (1 << beam)) == 0);
#else
  return ((mask & (1 << beam)) != 0);
#endif
}

static void printCurrentScale() {
  const ScaleDefinition &scale = SCALES[currentScale];
  Serial.println("\n==================================================");
  Serial.printf(">>> SCALE [%d/%d]: %s <<<\n", currentScale + 1, NUM_SCALES, scale.name);
  Serial.printf("Desc: %s\n", scale.description);
  Serial.println("--------------------------------------------------");
  for (int b = 0; b < NUM_BEAMS; b++) {
    Serial.printf("  Beam %d: %-4s (base: %s, step: 0x%05X)\n",
                  b, scale.notes[b].name,
                  NOTE_TABLE[scale.notes[b].baseSample].len ? NOTE_TABLE[scale.notes[b].baseSample].data ? "OK" : "" : "",
                  scale.notes[b].step_fp);
  }
  if (currentScale == 1) {
    Serial.println("\n  *** Tokyo Drift Hook: Beams 0-0-0-2-0-0-6-0-5-4 ***");
  }
  Serial.println("==================================================");
}

static void i2sInit() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_MSB),
    .intr_alloc_flags = 0,
    .dma_buf_count = I2S_DMA_BUF_COUNT,
    .dma_buf_len = I2S_DMA_BUF_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, NULL);
  i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

static void fillAudio(int frames) {
  static uint16_t buf[I2S_DMA_BUF_LEN * 2];

  for (int i = 0; i < frames; i++) {
    uint8_t out8 = 128;   // 128 is DAC midpoint (silence)

    if (currentBeam >= 0 && currentBeam < NUM_BEAMS) {
      const ScaleNote &sn = SCALES[currentScale].notes[currentBeam];
      const NoteSample &baseNote = NOTE_TABLE[sn.baseSample];
      const uint32_t idx = currentPos_fp >> 16;

      if (idx < baseNote.len) {
        out8 = baseNote.data[idx];
        currentPos_fp += sn.step_fp;
      } else {
        // Sample finished naturally -> stop audio (no looping!)
        currentBeam = -1;
      }
    }

    uint16_t out16 = ((uint16_t)out8) << 8;
    buf[i * 2]     = out16;
    buf[i * 2 + 1] = out16;
  }

  size_t bytesWritten = 0;
  i2s_write(I2S_NUM_0, buf, frames * 2 * sizeof(uint16_t), &bytesWritten, portMAX_DELAY);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, ATMEGA_RX_PIN, ATMEGA_TX_PIN);

  // Pushbutton on GPIO 18 (D18) with internal pull-up
  pinMode(SCALE_BUTTON_PIN, INPUT_PULLUP);

  i2sInit();
  printCurrentScale();
}

void loop() {
  // ---------------------------------------------------------------
  // 1. Push Button Handling (Scale Switching with 50ms Debounce)
  // ---------------------------------------------------------------
  static int lastReading = HIGH;
  static int buttonState = HIGH;
  static uint32_t lastDebounceTime = 0;
  const uint32_t debounceDelay = 50;

  const int reading = digitalRead(SCALE_BUTTON_PIN);
  if (reading != lastReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      // Falling edge: Button pressed (Active-LOW)
      if (buttonState == LOW) {
        currentScale = (currentScale + 1) % NUM_SCALES;
        currentBeam  = -1; // Silence any ongoing note on scale change
        printCurrentScale();
      }
    }
  }
  lastReading = reading;

  // ---------------------------------------------------------------
  // 2. Laser Harp Beam Processing via Serial2
  // ---------------------------------------------------------------
  if (Serial2.available()) {
    const uint8_t mask = Serial2.read();
    const uint8_t changed = mask ^ lastMask;

    if (changed != 0) {
      // Find the HIGHEST beam that was just newly blocked
      int8_t newlyBlockedHighest = -1;
      for (int8_t b = NUM_BEAMS - 1; b >= 0; b--) {
        const uint8_t bit = (1 << b);
        if (changed & bit) {
          if (isBeamBlocked(mask, b)) {
            newlyBlockedHighest = b;
            break; // Found highest newly blocked beam!
          }
        }
      }

      if (newlyBlockedHighest >= 0) {
        currentBeam = newlyBlockedHighest;
        const ScaleNote &sn = SCALES[currentScale].notes[currentBeam];
        currentPos_fp = ((uint32_t)NOTE_START_OFFSETS[sn.baseSample]) << 16; // Instant attack

        Serial.printf("Beam %d BLOCKED -> playing %s (%s)\n",
                      currentBeam, sn.name, SCALES[currentScale].name);
      }
    }

    lastMask = mask;
  }

  // ---------------------------------------------------------------
  // 3. Audio DMA Fill
  // ---------------------------------------------------------------
  fillAudio(I2S_DMA_BUF_LEN);
}
