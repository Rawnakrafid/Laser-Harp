#include <Arduino.h>
#include <driver/i2s.h>
#include "notes_data.h"
#include "instruments_data.h"

/* ------------------------------------------------------------------
 * Laser Harp - ESP32 Audio Engine: 4 Switchable Instruments (C Major)
 * 
 * Controlled by Push Button on GPIO 18 (D18 -> GND):
 *   Instrument 0: Acoustic Piano (Default C Major, C4..B4)
 *   Instrument 1: Acoustic Guitar (Karplus-Strong Plucked String, C Major)
 *   Instrument 2: Harmonium (Rich Dual-Reed Indian Pump Organ, C Major)
 *   Instrument 3: Metallic (Tokyo Drift Bell / Synth Mallet, C Major)
 *
 * All 4 instruments share the standard 7-beam C Major scale:
 *   Beam 0 -> C4 (Middle C, ~261.6 Hz)
 *   Beam 1 -> D4 (~293.7 Hz)
 *   Beam 2 -> E4 (~329.6 Hz)
 *   Beam 3 -> F4 (~349.2 Hz)
 *   Beam 4 -> G4 (~392.0 Hz)
 *   Beam 5 -> A4 (~440.0 Hz)
 *   Beam 6 -> B4 (~493.9 Hz)
 *
 * Behavior:
 * 1. Triggers sound instantly when a beam is BLOCKED (light interrupted).
 * 2. Push button on GPIO 18 cycles sequentially through the 4 instruments.
 * 3. Zero-latency attack: Piano skips recorded silence; synthetic instruments start instantly.
 * 4. Clean natural decay: Notes play once to completion without looping.
 * 5. Highest-note priority if multiple beams are broken simultaneously.
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

const char* const NOTE_NAMES[NUM_BEAMS] = {
  "C4 (Middle C)",
  "D4",
  "E4",
  "F4",
  "G4",
  "A4",
  "B4"
};

// Lead-in silence offset (samples) for instant note attack on Piano
const uint32_t PIANO_START_OFFSETS[NUM_BEAMS] = {
  1440, // C4
  1550, // D4
  1435, // E4
  1450, // F4
  1490, // G4
  1450, // A4
  1450  // B4
};

#define NUM_INSTRUMENTS 4

struct Instrument {
  const char*       name;
  const char*       description;
  const NoteSample* samples;
  const uint32_t*   offsets; // NULL if instant start (offset 0)
};

const Instrument INSTRUMENTS[NUM_INSTRUMENTS] = {
  {
    "Acoustic Piano",
    "Natural grand piano samples with warm acoustic decay",
    NOTE_TABLE,
    PIANO_START_OFFSETS
  },
  {
    "Acoustic Guitar",
    "Karplus-Strong physical string model with warm body resonance",
    GUITAR_TABLE,
    NULL
  },
  {
    "Harmonium",
    "Rich dual-reed Indian pump organ with natural acoustic chorus",
    HARMONIUM_TABLE,
    NULL
  },
  {
    "Metallic (Tokyo Drift)",
    "Sharp metallic FM bell / synth mallet - perfect for Tokyo Drift riffs",
    METALLIC_TABLE,
    NULL
  }
};

uint8_t  currentInstrument = 0;   // 0 = Piano (Default)
uint8_t  lastMask          = 0xFF;
int8_t   currentNote       = -1;   // -1 = silence/idle, 0..6 = active beam
uint32_t currentPos        = 0;

static inline bool isBeamBlocked(uint8_t mask, uint8_t beam) {
#if BEAM_BLOCKED_VALUE == 0
  return ((mask & (1 << beam)) == 0);
#else
  return ((mask & (1 << beam)) != 0);
#endif
}

static void printCurrentInstrument() {
  const Instrument &inst = INSTRUMENTS[currentInstrument];
  Serial.println("\n==================================================");
  Serial.printf(">>> ACTIVE INSTRUMENT [%d/%d]: %s <<<\n",
                currentInstrument + 1, NUM_INSTRUMENTS, inst.name);
  Serial.printf("Sound: %s\n", inst.description);
  Serial.println("Scale: C Major (C4, D4, E4, F4, G4, A4, B4)");
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
  const Instrument &inst = INSTRUMENTS[currentInstrument];

  for (int i = 0; i < frames; i++) {
    uint8_t out8 = 128;   // 128 is DAC midpoint (silence)

    if (currentNote >= 0 && currentNote < NUM_BEAMS) {
      const NoteSample &note = inst.samples[currentNote];

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

  // Pushbutton on GPIO 18 (D18) with internal pull-up
  pinMode(SCALE_BUTTON_PIN, INPUT_PULLUP);

  i2sInit();
  printCurrentInstrument();
}

void loop() {
  // ---------------------------------------------------------------
  // 1. Push Button Handling (Instrument Switching with 50ms Debounce)
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
        currentInstrument = (currentInstrument + 1) % NUM_INSTRUMENTS;
        currentNote = -1; // Silence any ongoing note immediately
        printCurrentInstrument();
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
        currentNote = newlyBlockedHighest;
        const Instrument &inst = INSTRUMENTS[currentInstrument];
        currentPos = (inst.offsets != NULL) ? inst.offsets[currentNote] : 0; // Instant attack

        Serial.printf("[%s] Beam %d BLOCKED -> playing %s\n",
                      inst.name, currentNote, NOTE_NAMES[currentNote]);
      }
    }

    lastMask = mask;
  }

  // ---------------------------------------------------------------
  // 3. Audio DMA Fill
  // ---------------------------------------------------------------
  fillAudio(I2S_DMA_BUF_LEN);
}
