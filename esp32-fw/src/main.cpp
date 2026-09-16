#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

/* ------------------------------------------------------------------
 * Laser Harp - ESP32 audio engine
 *
 * Receives a 1-byte beam-state mask from the ATmega32 over a dedicated
 * hardware UART (Serial2). Bit i of the mask = 1 means beam i is
 * currently blocked. On any beam's blocked-edge, plays that beam's
 * note straight off the SD card as MP3 (software-decoded on the ESP32
 * via ESP8266Audio's bundled libhelix decoder - no extra lib_deps
 * needed beyond earlephilhower/ESP8266Audio, already in platformio.ini).
 *
 * Quick-start audio output: ESP32's internal DAC on GPIO25 (mono,
 * 8-bit) so you can hear a real note with zero extra audio hardware
 * beyond a small speaker/amp on GPIO25. Swap AudioOutputI2S's mode to
 * EXTERNAL_I2S + wire an I2S DAC module (e.g. MAX98357A) later for
 * proper quality and true multi-note polyphony.
 * ------------------------------------------------------------------ */

// UART2 to ATmega32 - keeps USB Serial (pins 1/3) free for the debug console
#define ATMEGA_RX_PIN 16   // ESP32 RX2  <-- ATmega32 PD1 (TXD) THROUGH A VOLTAGE DIVIDER (5V -> ~3.3V, see wiring notes)
#define ATMEGA_TX_PIN 17   // ESP32 TX2  --> ATmega32 PD0 (RXD); direct wire is fine, 3.3V reads as HIGH on the 5V AVR

#define SD_CS_PIN 5        // microSD module chip-select; MOSI/MISO/SCK use ESP32's default VSPI pins (23/19/18)

// Beam index -> note file on the SD card, in the same numbering Muttakin's
// Notes/ folder already uses (0001_C4.mp3 ... 0007_B4.mp3), so no renaming
// was needed - just copy the mp3s onto the card as-is. Extend this array
// (and NUM_ACTIVE_BEAMS on the ATmega32) together when scaling past beam 0.
const char *NOTE_FILES[] = {
  "/0001_C4.mp3",   // beam 0
  "/0002_D4.mp3",   // beam 1
  "/0003_E4.mp3",   // beam 2
  "/0004_F4.mp3",   // beam 3
  "/0005_G4.mp3",   // beam 4
  "/0006_A4.mp3",   // beam 5
  "/0007_B4.mp3",   // beam 6
};
const uint8_t NUM_NOTE_FILES = sizeof(NOTE_FILES) / sizeof(NOTE_FILES[0]);

AudioFileSourceSD *file;
AudioGeneratorMP3  *mp3;
AudioOutputI2S     *out;

uint8_t lastMask = 0x00;
unsigned long lastRxMs = 0;
const unsigned long LINK_TIMEOUT_MS = 1000;

void playNote(const char *path)
{
  if (mp3->isRunning()) {
    mp3->stop();
  }
  if (file->isOpen()) {
    file->close();
  }
  if (file->open(path)) {
    mp3->begin(file, out);
  } else {
    Serial.printf("SD: could not open %s\n", path);
  }
}

void setup()
{
  Serial.begin(115200);                                   // USB debug console
  Serial2.begin(9600, SERIAL_8N1, ATMEGA_RX_PIN, ATMEGA_TX_PIN);

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD card init failed - check wiring and that it's formatted FAT32.");
  }

  out = new AudioOutputI2S(0, AudioOutputI2S::INTERNAL_DAC);  // built-in DAC, GPIO25
  out->SetGain(0.7);
  file = new AudioFileSourceSD();
  mp3  = new AudioGeneratorMP3();

  lastRxMs = millis();
  Serial.println("Ready - waiting for ATmega32 beam data on Serial2 (9600 baud).");
}

void loop()
{
  if (mp3->isRunning() && !mp3->loop()) {
    mp3->stop();
  }

  if (Serial2.available()) {
    const uint8_t mask = Serial2.read();
    lastRxMs = millis();

    const uint8_t changed = mask ^ lastMask;
    for (uint8_t beam = 0; beam < NUM_NOTE_FILES; beam++) {
      const uint8_t bit = (uint8_t)(1 << beam);
      if ((changed & bit) && (mask & bit)) {     // this beam just got blocked -> note on
        Serial.printf("Beam %u blocked -> playing %s\n", beam, NOTE_FILES[beam]);
        playNote(NOTE_FILES[beam]);
      }
      // beam cleared: let the sample ring out naturally instead of cutting it off
    }
    lastMask = mask;
  }

  if (millis() - lastRxMs > LINK_TIMEOUT_MS) {
    // With only beam 0 wired up there's nothing extra to silence here, but
    // once several beams are live this is where a dead-link timeout should
    // force the active note off so nothing gets stuck sounding forever.
  }
}
