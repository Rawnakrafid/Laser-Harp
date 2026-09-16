#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>

/* ------------------------------------------------------------------
 * Laser Harp - ESP32 audio engine (DFPlayer Mini version, 7 beams)
 *
 * Receives a 1-byte beam-state mask from the ATmega32 (bit i = beam i
 * blocked, i = 0..6) and tells the DFPlayer which track to play.
 *
 * Switched from playMp3Folder() to playFolder() for faster track
 * selection: playMp3Folder() has to pattern-match a 4-digit prefix
 * against arbitrary filenames in a folder named "MP3", which can add
 * a few hundred ms of scan delay depending on the card. playFolder()
 * addresses a folder/file pair by strict numeric position (folder
 * "01", files "001.mp3".."007.mp3") and is the faster, low-latency
 * selection method per DFRobot's own docs. SD card layout is now:
 *   /01/001.mp3  (beam 0, C4)
 *   /01/002.mp3  (beam 1, D4)
 *   ... /01/007.mp3 (beam 6, B4)
 * ------------------------------------------------------------------ */

// UART2 to ATmega32 - keeps USB Serial (pins 1/3) free for the debug console
#define ATMEGA_RX_PIN 16   // ESP32 RX2  <-- ATmega32 PD1 (TXD) THROUGH A VOLTAGE DIVIDER (5V -> ~3.3V)
#define ATMEGA_TX_PIN 17   // ESP32 TX2  --> ATmega32 PD0 (RXD); direct wire is fine

// UART1 to DFPlayer Mini
#define DF_RX_PIN 26       // ESP32 RX1  <-- DFPlayer TX
#define DF_TX_PIN 27       // ESP32 TX1  --> DFPlayer RX (direct wire is fine; DFPlayer's RX tolerates 3.3V logic)

#define DF_FOLDER 1        // SD card folder "01"
#define NUM_BEAMS 7        // beams 0-6, tracks 001.mp3-007.mp3

HardwareSerial dfSerial(1);
DFRobotDFPlayerMini dfPlayer;

uint8_t lastMask = 0x00;
unsigned long lastRxMs = 0;
const unsigned long LINK_TIMEOUT_MS = 1000;

void setup()
{
  Serial.begin(115200);                                     // USB debug console
  Serial2.begin(9600, SERIAL_8N1, ATMEGA_RX_PIN, ATMEGA_TX_PIN);
  dfSerial.begin(9600, SERIAL_8N1, DF_RX_PIN, DF_TX_PIN);

  if (!dfPlayer.begin(dfSerial)) {
    Serial.println("DFPlayer Mini not responding - check wiring/power/SD card.");
  } else {
    dfPlayer.volume(30);        // 0-30, maxed out
    Serial.println("DFPlayer Mini ready.");
  }

  lastRxMs = millis();
  Serial.println("Ready - waiting for ATmega32 beam data on Serial2 (9600 baud).");
}

void loop()
{
  if (Serial2.available()) {
    const uint8_t mask = Serial2.read();
    lastRxMs = millis();

    const uint8_t changed = mask ^ lastMask;
    for (uint8_t beam = 0; beam < NUM_BEAMS; beam++) {
      const uint8_t bit = (uint8_t)(1 << beam);
      if ((changed & bit) && (mask & bit)) {          // this beam just got blocked -> note on
        const uint8_t track = beam + 1;                // beam 0 -> 001.mp3, beam 1 -> 002.mp3, ...
        Serial.printf("Beam %u blocked -> playFolder(%u, %u)\n", beam, DF_FOLDER, track);
        dfPlayer.playFolder(DF_FOLDER, track);
      }
      // beam cleared: let the note ring out naturally instead of cutting it off
    }
    lastMask = mask;
  }

  if (millis() - lastRxMs > LINK_TIMEOUT_MS) {
    // Once several beams are live, this is where a dead-link timeout
    // would go (dfPlayer.stop()) so nothing gets stuck sounding forever.
  }
}
