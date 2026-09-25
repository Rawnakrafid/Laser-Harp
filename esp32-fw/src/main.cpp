#include <Arduino.h>
#include <driver/i2s.h>
#include "string2/notes_data_string2.h"

/* Single-note version: no mixing, no overlap. Whichever beam you just
 * blocked plays its own sample from notes_data_string2.h (the metallic
 * "string type 2" set), replacing whatever was playing before. */

#define ATMEGA_RX_PIN 16
#define ATMEGA_TX_PIN 17
#define NUM_BEAMS 7

#define I2S_SAMPLE_RATE   8000
#define I2S_DMA_BUF_COUNT 8
#define I2S_DMA_BUF_LEN   256

uint8_t lastMask = 0x00;

int8_t   currentNote = -1;   // -1 = nothing playing
uint32_t currentPos  = 0;

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
      const NoteSample2 &note = NOTE_TABLE_STRING2[currentNote];
      if (currentPos < note.len) {
        out8 = note.data[currentPos];
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
  i2sInit();
  Serial.println("String2 (metallic set) test build: block any beam to hear its note (no mixing).");
}

void loop() {
  if (Serial2.available()) {
    const uint8_t mask = Serial2.read();
    const uint8_t changed = mask ^ lastMask;
    for (uint8_t beam = 0; beam < NUM_BEAMS; beam++) {
      const uint8_t bit = (uint8_t)(1 << beam);
      if ((changed & bit) && (mask & bit)) {
        currentNote = beam;
        currentPos  = 0;
        Serial.printf("beam %u -> note %u\n", beam, beam);
      }
    }
    lastMask = mask;
  }
  fillAudio(I2S_DMA_BUF_LEN);
}
