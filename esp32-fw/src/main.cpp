#include <Arduino.h>
#include <driver/i2s.h>
#include "notes_data.h"
#include "string2/notes_data_string2.h"

/* ------------------------------------------------------------------
 * ESP32 audio engine - two-mode version.
 *
 * A push button (BUTTON_PIN) toggles between two note libraries:
 *   mode 0 - the 7 piano chords     (notes_data.h        / NOTE_TABLE)
 *   mode 1 - the 7 metallic notes   (notes_data_string2.h / NOTE_TABLE_STRING2)
 * 14 notes total, 7 per mode, same beam -> index mapping either way.
 * This is exactly why those two headers were built with different
 * struct/array names (NoteSample/NOTE_TABLE vs NoteSample2/
 * NOTE_TABLE_STRING2) - so both can be #included here with zero
 * symbol collisions.
 *
 * Still single-note (no mixing/overlap), same as the current
 * diagnostic build: whichever beam you just blocked plays its sample
 * from the CURRENTLY SELECTED table, replacing whatever was playing.
 *
 * BUTTON WIRING: one leg to BUTTON_PIN, the other leg to GND. Uses
 * the ESP32's internal pull-up, so no external resistor is needed.
 * Pick a free GPIO for BUTTON_PIN - GPIO25/26 are already used
 * internally by the I2S built-in DAC, and 16/17 are the ATmega link,
 * so don't reuse those. GPIO4 is a safe default on most dev boards;
 * change it if that pin is already spoken for on your wiring.
 * ------------------------------------------------------------------ */

#define ATMEGA_RX_PIN 16
#define ATMEGA_TX_PIN 17
#define NUM_BEAMS 7

#define BUTTON_PIN   4
#define DEBOUNCE_MS  30

#define I2S_SAMPLE_RATE   8000
#define I2S_DMA_BUF_COUNT 8
#define I2S_DMA_BUF_LEN   256

uint8_t lastMask = 0x00;

int8_t   currentNote = -1;   // -1 = nothing playing
uint32_t currentPos  = 0;

uint8_t stringType = 0;      // 0 = piano chords, 1 = metallic set

// button debounce state
int lastButtonReading   = HIGH;
int stableButtonState   = HIGH;
unsigned long lastDebounceTime = 0;

static void buttonInit() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);   // button to GND; pin reads LOW when pressed
}

// Returns true exactly once per confirmed press (debounced HIGH -> LOW edge).
static bool buttonPressedEdge() {
  int reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }

  bool pressedEdge = false;
  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading != stableButtonState) {
      stableButtonState = reading;
      if (stableButtonState == LOW) {
        pressedEdge = true;
      }
    }
  }

  lastButtonReading = reading;
  return pressedEdge;
}

// Fetches the sample data pointer/length for a given beam under the
// currently selected mode. The two tables use differently-named
// struct types (NoteSample vs NoteSample2) but the same shape, so
// this just branches on which one to read from.
static void getNoteForBeam(uint8_t beam, const uint8_t* &data, uint32_t &len) {
  if (stringType == 0) {
    const NoteSample &note = NOTE_TABLE[beam];
    data = note.data;
    len  = note.len;
  } else {
    const NoteSample2 &note = NOTE_TABLE_STRING2[beam];
    data = note.data;
    len  = note.len;
  }
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
    uint8_t out8 = 128;   // silence

    if (currentNote >= 0) {
      const uint8_t* data;
      uint32_t len;
      getNoteForBeam((uint8_t)currentNote, data, len);

      if (currentPos < len) {
        out8 = data[currentPos];
        currentPos++;
      } else {
        currentNote = -1;   // note finished naturally
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
  buttonInit();
  i2sInit();
  Serial.println("Two-mode build ready: button toggles chord set / metallic set.");
  Serial.println("Mode 0 (piano chords) active. Block a beam to hear its note.");
}

void loop() {
  if (buttonPressedEdge()) {
    stringType = stringType ? 0 : 1;
    currentNote = -1;   // stop whatever was ringing so the switch is clean
    Serial.printf("Switched to mode %u (%s)\n", stringType, stringType == 0 ? "piano chords" : "metallic set");
  }

  if (Serial2.available()) {
    const uint8_t mask = Serial2.read();
    const uint8_t changed = mask ^ lastMask;
    for (uint8_t beam = 0; beam < NUM_BEAMS; beam++) {
      const uint8_t bit = (uint8_t)(1 << beam);
      if ((changed & bit) && (mask & bit)) {
        currentNote = beam;
        currentPos  = 0;
        Serial.printf("beam %u -> note %u (mode %u)\n", beam, beam, stringType);
      }
    }
    lastMask = mask;
  }
  fillAudio(I2S_DMA_BUF_LEN);
}
