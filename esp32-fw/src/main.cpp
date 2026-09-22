#include <Arduino.h>
#include <driver/dac.h>
#include "notes_data.h"

/* ------------------------------------------------------------------
 * Laser Harp - ESP32 polyphonic DAC audio engine
 *
 * Plays notes via the ESP32's built-in DAC on GPIO25, using a
 * dedicated FreeRTOS task for rock-solid sample timing.
 *
 * Audio output: GPIO25 -> amplifier (PAM8403) -> speaker.
 * Beam 0 -> C4, Beam 1 -> D4, ... Beam 6 -> B4.
 * Multiple beams mix together when blocked simultaneously.
 * ------------------------------------------------------------------ */

#define ATMEGA_RX_PIN 16
#define ATMEGA_TX_PIN 17
#define DAC_PIN 25

#define NUM_BEAMS 7
#define SAMPLE_RATE 8000
#define SAMPLE_PERIOD_US (1000000 / SAMPLE_RATE)  // 125 µs

uint8_t lastMask = 0x00;
unsigned long lastRxMs = 0;
const unsigned long LINK_TIMEOUT_MS = 1000;

// Per-beam playback state
volatile bool beamActive[NUM_BEAMS] = {false};
volatile uint32_t notePos[NUM_BEAMS] = {0};

// Audio output task running on Core 1
void audioTask(void *param) {
  // Enable DAC output on channel 1 (GPIO25)
  dac_output_enable(DAC_CHANNEL_1);

  while (true) {
    unsigned long t0 = micros();

    int32_t mixSum = 0;
    uint8_t activeCount = 0;

    for (uint8_t b = 0; b < NUM_BEAMS; b++) {
      if (!beamActive[b]) continue;

      const NoteSample &note = NOTE_TABLE[b];
      if (notePos[b] >= note.len) {
        beamActive[b] = false;
        continue;
      }

      int16_t sample = (int16_t)note.data[notePos[b]] - 128;
      mixSum += sample;
      activeCount++;
      notePos[b]++;
    }

    uint8_t outVal;
    if (activeCount > 0) {
      int16_t mixed = (int16_t)(mixSum / activeCount);
      outVal = (uint8_t)(mixed + 128);
    } else {
      outVal = 128;
    }

    // Direct DAC write - fast, no Arduino overhead
    dac_output_voltage(DAC_CHANNEL_1, outVal);

    // Maintain precise 8000 Hz sample rate
    unsigned long elapsed = micros() - t0;
    if (elapsed < SAMPLE_PERIOD_US) {
      delayMicroseconds(SAMPLE_PERIOD_US - elapsed);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, ATMEGA_RX_PIN, ATMEGA_TX_PIN);

  Serial.println("=== ESP32 Laser Harp Audio Engine ===");

  // ---------- HARDWARE TEST: Generate a loud 440Hz sine wave ----------
  // This proves GPIO25 -> amp -> speaker path works independently of
  // any note data or beam logic.
  Serial.println("TEST: Playing 440Hz sine wave on GPIO25 for 1 second...");
  dac_output_enable(DAC_CHANNEL_1);

  for (int i = 0; i < SAMPLE_RATE; i++) {  // 1 second of audio
    // 440 Hz sine wave, full amplitude (0-255)
    float angle = 2.0f * 3.14159f * 440.0f * i / SAMPLE_RATE;
    uint8_t val = (uint8_t)(128 + 127 * sin(angle));
    dac_output_voltage(DAC_CHANNEL_1, val);
    delayMicroseconds(SAMPLE_PERIOD_US);
  }
  dac_output_voltage(DAC_CHANNEL_1, 128);  // silence
  Serial.println("TEST: Sine wave done. Did you hear a tone?");

  // ---------- Now test with actual note C4 data ----------
  Serial.println("TEST: Playing noteC4 from flash for 1 second...");
  uint32_t samplesToPlay = min((uint32_t)SAMPLE_RATE, noteC4Len);
  for (uint32_t i = 0; i < samplesToPlay; i++) {
    dac_output_voltage(DAC_CHANNEL_1, noteC4[i]);
    delayMicroseconds(SAMPLE_PERIOD_US);
  }
  dac_output_voltage(DAC_CHANNEL_1, 128);
  Serial.println("TEST: Note C4 done. Did you hear it?");

  // ---------- Start the audio task on Core 1 ----------
  xTaskCreatePinnedToCore(
    audioTask,    // function
    "audio",      // name
    4096,         // stack size
    NULL,         // parameter
    5,            // priority (high)
    NULL,         // task handle
    1             // run on Core 1 (loop() runs on Core 0)
  );

  lastRxMs = millis();
  Serial.println("Ready - 7-beam polyphonic DAC mixer on GPIO25.");
  Serial.println("Waiting for ATmega32 beam data on Serial2 (9600 baud).");
}

uint8_t countActive() {
  uint8_t count = 0;
  for (uint8_t b = 0; b < NUM_BEAMS; b++) {
    if (beamActive[b]) count++;
  }
  return count;
}

void loop() {
  if (Serial2.available()) {
    const uint8_t mask = Serial2.read();
    lastRxMs = millis();

    const uint8_t changed = mask ^ lastMask;
    for (uint8_t beam = 0; beam < NUM_BEAMS; beam++) {
      const uint8_t bit = (uint8_t)(1 << beam);
      if ((changed & bit) && (mask & bit)) {
        Serial.printf("Beam %u blocked -> note ON (mixing with %u other active)\n",
                       beam, countActive());
        notePos[beam] = 0;
        beamActive[beam] = true;
      }
    }
    lastMask = mask;
  }

  if (millis() - lastRxMs > LINK_TIMEOUT_MS) {
    // dead-link hook
  }

  delay(1);  // yield to other tasks
}
