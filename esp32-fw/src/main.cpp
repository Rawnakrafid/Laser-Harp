#include <Arduino.h>
#include <driver/i2s.h>
#include "notes_data.h"
#include "string2/notes_data_string2.h"

/* ------------------------------------------------------------------
 * Laser Harp - ESP32 audio engine (Clean One-Shot, No Looping)
 *
 * Behavior:
 * 1. Triggers sound when a beam is BLOCKED (light interrupted).
 * 2. NO PERSISTENCE / NO LOOPING: Plays the natural note sample once
 *    from start to finish, then stops. Holding a beam does not repeat
 *    or loop.
 * 3. NO SOUND ON LIGHT RESTORED: Removing your hand does not trigger
 *    any sound.
 * 4. Highest-Note Priority: If multiple beams are blocked at once,
 *    the highest note plays.
 * ------------------------------------------------------------------ */

#define ATMEGA_RX_PIN 16
#define ATMEGA_TX_PIN 17
#define NUM_BEAMS 7

// Set to 0 if your circuit sends 0 when light is blocked (light lost).
// Set to 1 if your circuit sends 1 when light is blocked.
#define BEAM_BLOCKED_VALUE 0

// Sound Set Selection:
// 0 = Metallic/Gamelan single notes (string2/notes_data_string2.h)
// 1 = Salamander Grand Piano Chords (notes_data.h)
#define USE_PIANO_CHORDS 0

#define I2S_SAMPLE_RATE   8000
#define I2S_DMA_BUF_COUNT 8
#define I2S_DMA_BUF_LEN   256

struct NoteItem {
  const uint8_t* data;
  uint32_t len;
  const char* name;
};

// Metallic Note Set (string2)
const NoteItem METALLIC_NOTES[NUM_BEAMS] = {
  { noteBb4,     noteBb4Len,     "Bb4"     }, // Beam 0
  { noteB4,      noteB4Len,      "B4"      }, // Beam 1
  { noteEb5,     noteEb5Len,     "Eb5"     }, // Beam 2
  { noteF5,      noteF5Len,      "F5"      }, // Beam 3
  { noteFs5,     noteFs5Len,     "F#5"     }, // Beam 4
  { noteAb5,     noteAb5Len,     "Ab5"     }, // Beam 5
  { noteBbChord, noteBbChordLen, "BbChord" }  // Beam 6
};

// Piano Chord Set (notes_data.h)
const NoteItem PIANO_CHORDS[NUM_BEAMS] = {
  { chordCmaj, chordCmajLen, "C major (Sa)"  }, // Beam 0
  { chordDmin, chordDminLen, "D minor (Re)"  }, // Beam 1
  { chordEmin, chordEminLen, "E minor (Ga)"  }, // Beam 2
  { chordFmaj, chordFmajLen, "F major (Ma)"  }, // Beam 3
  { chordGmaj, chordGmajLen, "G major (Pa)"  }, // Beam 4
  { chordAmin, chordAminLen, "A minor (Dha)" }, // Beam 5
  { chordBdim, chordBdimLen, "B dim   (Ni)"  }  // Beam 6
};

#if USE_PIANO_CHORDS == 1
  #define ACTIVE_TABLE PIANO_CHORDS
#else
  #define ACTIVE_TABLE METALLIC_NOTES
#endif

uint8_t lastMask = 0xFF;
int8_t  currentNote = -1;       // -1 = silence/idle, 0..6 = active beam
uint32_t currentPos = 0;        // current position in the sample

static inline bool isBeamBlocked(uint8_t mask, uint8_t beam) {
#if BEAM_BLOCKED_VALUE == 0
  return ((mask & (1 << beam)) == 0);
#else
  return ((mask & (1 << beam)) != 0);
#endif
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

    if (currentNote >= 0 && currentNote < NUM_BEAMS) {
      const NoteItem &note = ACTIVE_TABLE[currentNote];

      if (currentPos < note.len) {
        out8 = note.data[currentPos];
        currentPos++;
      } else {
        // Sample finished naturally -> stop audio (no looping!)
        currentNote = -1;
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
  i2sInit();
  Serial.println("==================================================");
  Serial.println("Laser Harp ESP32 - Natural Playback (No Loop)");
  Serial.printf("Sound Set: %s\n", USE_PIANO_CHORDS ? "Piano Chords" : "Metallic Notes");
  Serial.printf("Trigger: Plays when beam is BLOCKED (bit == %d)\n", BEAM_BLOCKED_VALUE);
  Serial.println("==================================================");
}

void loop() {
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
        currentNote = newlyBlockedHighest;
        currentPos  = 0;  // Start playing from beginning once
        Serial.printf("beam %d BLOCKED -> note %s\n",
                      currentNote, ACTIVE_TABLE[currentNote].name);
      }
    }

    lastMask = mask;
  }

  fillAudio(I2S_DMA_BUF_LEN);
}
