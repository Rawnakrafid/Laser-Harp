#include <Arduino.h>
#include <driver/i2s.h>
#include "notes_data.h"

/* ------------------------------------------------------------------
 * Laser Harp - ESP32 audio engine, SELF-MIXING VERSION (no DFPlayer)
 *
 * This branch replaces the DFPlayer Mini entirely. The DFPlayer can only
 * decode and play ONE file at a time - it has no way to mix multiple
 * notes together, which is required for real polyphony (several beams
 * sounding at once). So this version has the ESP32 itself generate the
 * audio: each note's raw waveform is embedded in flash (notes_data.h,
 * converted ahead of time from the same mp3s used on the DFPlayer's SD
 * card, via `ffmpeg -ar 8000 -ac 1 -f u8 -acodec pcm_u8`), and every
 * beam that's currently sounding has its own independent playback
 * position. Each audio tick, the ESP32 adds together the current sample
 * of every active note (averaged, to avoid clipping) and streams the
 * result out continuously via the ESP32's built-in DAC (I2S in
 * "built-in DAC" mode), on GPIO25 AND GPIO26 simultaneously (both carry
 * an identical copy of the mixed signal, so either pin can be used).
 *
 * IMPORTANT HARDWARE CHANGE from the DFPlayer version: the ESP32's DAC
 * output is weak and low-voltage - it cannot drive a speaker directly at
 * a usable volume. You need a small amplifier between GPIO25 (or 26) and
 * the speaker - a cheap PAM8403-style class-D amp module is the standard
 * choice. See WIRING_POLYPHONY.md for the exact connections. The
 * DFPlayer module and its SD card are not used at all in this version;
 * ESP32 GPIO26/27 (previously wired to the DFPlayer's TX/RX) are free.
 *
 * NOT YET IMPLEMENTED (separate feature, discussed but not built here):
 * sustaining a note for as long as its beam stays blocked. Right now,
 * exactly like the DFPlayer version, a beam-block event starts its note
 * from the beginning and it plays through to its own natural end -
 * multiple beams can now overlap/mix, but releasing a beam early doesn't
 * cut its note off, and holding a beam doesn't extend it.
 *
 * NOT YET COMPILE-TESTED: this sandbox has no ESP32/Arduino toolchain to
 * verify against (unlike the ATmega32 firmware, which is checked with
 * real avr-gcc every time). The I2S built-in-DAC API used below is a
 * well-established pattern, but if `pio run` reports a compile error,
 * paste the exact error back rather than assuming this needs a rewrite -
 * it's very likely a small, fixable API-signature mismatch against
 * whatever ESP-IDF version PlatformIO happens to fetch.
 * ------------------------------------------------------------------ */

// UART2 to ATmega32 - unchanged from the DFPlayer version. The ATmega32
// side (beam sensing) doesn't change at all for this feature.
#define ATMEGA_RX_PIN 16   // ESP32 RX2  <-- ATmega32 PD1 (TXD) THROUGH THE VOLTAGE DIVIDER (5V -> ~3.3V)
#define ATMEGA_TX_PIN 17   // ESP32 TX2  --> ATmega32 PD0 (RXD); direct wire is fine

#define NUM_BEAMS 7

#define I2S_SAMPLE_RATE   8000   // must match the rate notes_data.h was generated at
#define I2S_DMA_BUF_COUNT 8
#define I2S_DMA_BUF_LEN   256    // sample-frames per DMA buffer (256 stereo 16-bit frames = 1KB/buffer)

uint8_t lastMask = 0x00;
unsigned long lastRxMs = 0;
const unsigned long LINK_TIMEOUT_MS = 1000;

// Per-beam playback state: is this beam's note currently sounding, and how
// far into its own sample data has it played. Independent per beam, which
// is what makes overlap/mixing possible - beam 0 can be partway through
// its note while beam 3 just started, and both get summed together below.
bool beamActive[NUM_BEAMS] = {false, false, false, false, false, false, false};
uint32_t notePos[NUM_BEAMS] = {0, 0, 0, 0, 0, 0, 0};

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
  i2s_set_pin(I2S_NUM_0, NULL);                    // NULL pin config -> route to the internal DAC, not external I2S pins
  i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);        // both GPIO25 and GPIO26 carry the same mixed signal
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// Mixes every currently-active beam's note together for `frames` sample
// steps and pushes the result to the DAC. Blocks briefly if the DMA
// buffer is full - that's normal and is what paces this to real playback
// speed, so calling this once per loop() iteration is enough; nothing
// else in loop() is slow enough to starve it.
static void fillAudio(int frames) {
  static uint16_t buf[I2S_DMA_BUF_LEN * 2];   // interleaved L/R, 16-bit words

  for (int i = 0; i < frames; i++) {
    int32_t mixSum = 0;
    uint8_t activeCount = 0;

    for (uint8_t b = 0; b < NUM_BEAMS; b++) {
      if (!beamActive[b]) continue;

      const NoteSample &note = NOTE_TABLE[b];
      if (notePos[b] >= note.len) {
        beamActive[b] = false;      // this note reached its own natural end
        continue;
      }

      // Samples are stored unsigned (0-255, silence = 128) - shift to
      // signed for the addition, then we'll shift back afterward.
      int16_t sample = (int16_t)note.data[notePos[b]] - 128;
      mixSum += sample;
      activeCount++;
      notePos[b]++;
    }

    int16_t mixed = 0;
    if (activeCount > 0) {
      // Average instead of a raw sum - keeps several overlapping notes
      // from adding up past what the DAC can represent (clipping/crackle).
      mixed = (int16_t)(mixSum / activeCount);
    }

    uint8_t out8 = (uint8_t)(mixed + 128);
    // The built-in DAC reads the TOP 8 bits of each 16-bit I2S word - this
    // left-shift is what actually gets the sample value to the DAC.
    uint16_t out16 = ((uint16_t)out8) << 8;

    buf[i * 2]     = out16;   // left  (GPIO26)
    buf[i * 2 + 1] = out16;   // right (GPIO25) - identical, so either pin works
  }

  size_t bytesWritten = 0;
  i2s_write(I2S_NUM_0, buf, frames * 2 * sizeof(uint16_t), &bytesWritten, portMAX_DELAY);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, ATMEGA_RX_PIN, ATMEGA_TX_PIN);

  i2sInit();

  lastRxMs = millis();
  Serial.println("Ready - ESP32 self-mixing audio engine (no DFPlayer). Waiting for ATmega32 beam data on Serial2 (9600 baud).");
}

void loop() {
  if (Serial2.available()) {
    const uint8_t mask = Serial2.read();
    lastRxMs = millis();

    const uint8_t changed = mask ^ lastMask;
    for (uint8_t beam = 0; beam < NUM_BEAMS; beam++) {
      const uint8_t bit = (uint8_t)(1 << beam);
      if ((changed & bit) && (mask & bit)) {
        // beam just got blocked -> (re)start this note from the beginning.
        // Other beams' notes, if active, are untouched and keep mixing in.
        notePos[beam] = 0;
        beamActive[beam] = true;
        Serial.printf("Beam %u blocked -> note %u starts (mixing with whatever else is active)\n", beam, beam);
      }
      // No action on release yet - see the NOT YET IMPLEMENTED note above.
    }
    lastMask = mask;
  }

  // Keep the DAC continuously fed. This call paces itself (blocks briefly
  // if the DMA buffer's still full from the last chunk), so this alone is
  // enough to keep steady playback without a separate timer or task.
  fillAudio(I2S_DMA_BUF_LEN);

  if (millis() - lastRxMs > LINK_TIMEOUT_MS) {
    // dead-link hook, if ever needed
  }
}
