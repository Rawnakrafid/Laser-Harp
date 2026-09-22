#include <Arduino.h>
#include <driver/i2s.h>

/* ------------------------------------------------------------------
 * TEMPORARY DIAGNOSTIC BUILD - NOT the real polyphony firmware.
 *
 * Purpose: rule out "the note sample data / mixing math is somehow too
 * quiet or broken" as the reason for no sound, by replacing it with the
 * simplest possible thing that should be unmistakably audible: a plain
 * square-wave beep tone, full amplitude, playing for as long as ANY
 * beam is blocked, silent when none are.
 *
 * If you hear this beep through the amp+speaker: the whole chain (ESP32
 * DAC -> amp -> speaker) is proven working end-to-end, and the real bug
 * is specifically in the sample data or mixing code in the real
 * main.cpp - go back to that with confidence it's a content problem,
 * not a wiring/power/amp problem.
 *
 * If you DON'T hear this beep either: the problem is NOT the audio
 * content - it's still somewhere in amp power / ground / volume pot /
 * output wiring, exactly what we were checking before this test.
 *
 * DO NOT commit this file. Once you're done testing, restore the real
 * version with:
 *     git checkout -- src/main.cpp
 * (safe to do - the real polyphony version is already committed).
 * ------------------------------------------------------------------ */

#define ATMEGA_RX_PIN 16
#define ATMEGA_TX_PIN 17

#define I2S_SAMPLE_RATE   8000
#define I2S_DMA_BUF_COUNT 8
#define I2S_DMA_BUF_LEN   256

#define TONE_HZ           500     // plain audible beep frequency
#define TONE_HALF_PERIOD  (I2S_SAMPLE_RATE / TONE_HZ / 2)   // samples per half-cycle

uint8_t lastMask = 0x00;
volatile bool toneOn = false;

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

// Full-amplitude square wave (as loud as this DAC can go) while toneOn,
// dead silence (mid-scale) otherwise. No sample data, no mixing, no
// averaging - the simplest possible test signal.
static void fillAudio(int frames) {
  static uint16_t buf[I2S_DMA_BUF_LEN * 2];
  static uint32_t phase = 0;

  for (int i = 0; i < frames; i++) {
    uint8_t out8 = 128;   // silence

    if (toneOn) {
      const bool highHalf = (phase / TONE_HALF_PERIOD) % 2 == 0;
      out8 = highHalf ? 255 : 0;   // full swing, as loud as the DAC gets
      phase++;
    } else {
      phase = 0;   // reset so the tone always starts clean, no click from a stale phase
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
  Serial.println("DIAGNOSTIC BUILD: plain beep test. Block any beam to hear a 500Hz tone.");
}

void loop() {
  if (Serial2.available()) {
    const uint8_t mask = Serial2.read();
    if (mask != lastMask) {
      toneOn = (mask != 0);
      Serial.printf("mask=0x%02X -> tone %s\n", mask, toneOn ? "ON" : "off");
      lastMask = mask;
    }
  }
  fillAudio(I2S_DMA_BUF_LEN);
}
