#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>

/* ------------------------------------------------------------------
 * Laser Harp - ESP32 audio engine (DFPlayer Mini version)
 *
 * The DFPlayer Mini has its own microSD slot, its own MP3 decoder, and
 * its own class-D amp driving a speaker directly - so the ESP32 no
 * longer touches audio decoding or an SD card itself. Its only job is
 * to receive the beam-state byte from the ATmega32 and tell the
 * DFPlayer which track to play.
 *
 * Two separate UARTs are in play here:
 *  - Serial2 (hardware UART2, GPIO16/17): ATmega32 link, unchanged
 *    from before.
 *  - Serial1 (hardware UART1, remapped to GPIO26/27): DFPlayer Mini
 *    link. UART1's *default* pins (9/10) are wired to the ESP32's
 *    internal flash on most dev boards, so it's remapped here to two
 *    plain free GPIOs instead - that's normal and expected on ESP32.
 *
 * SD card side: format FAT32, create a folder literally named "MP3"
 * at the card's root, and put 0001_C4.mp3 ... 0007_B4.mp3 inside it.
 * playMp3Folder(N) plays whichever file in that folder starts with
 * the 4-digit prefix N - the numbering Muttakin already used lines up
 * with this exactly, so the files don't need renaming.
 * ------------------------------------------------------------------ */

// UART2 to ATmega32 - keeps USB Serial (pins 1/3) free for the debug console
#define ATMEGA_RX_PIN 16   // ESP32 RX2  <-- ATmega32 PD1 (TXD) THROUGH A VOLTAGE DIVIDER (5V -> ~3.3V)
#define ATMEGA_TX_PIN 17   // ESP32 TX2  --> ATmega32 PD0 (RXD); direct wire is fine

// UART1 to DFPlayer Mini
#define DF_RX_PIN 26       // ESP32 RX1  <-- DFPlayer TX
#define DF_TX_PIN 27       // ESP32 TX1  --> DFPlayer RX (direct wire is fine; DFPlayer's RX tolerates 3.3V logic)

HardwareSerial dfSerial(1);
DFRobotDFPlayerMini dfPlayer;

uint8_t lastMask = 0x00;
unsigned long lastRxMs = 0;
const unsigned long LINK_TIMEOUT_MS = 1000;
const uint8_t NUM_NOTE_TRACKS = 7;   // 0001_C4.mp3 ... 0007_B4.mp3

void setup()
{
  Serial.begin(115200);                                     // USB debug console
  Serial2.begin(9600, SERIAL_8N1, ATMEGA_RX_PIN, ATMEGA_TX_PIN);
  dfSerial.begin(9600, SERIAL_8N1, DF_RX_PIN, DF_TX_PIN);

  if (!dfPlayer.begin(dfSerial)) {
    Serial.println("DFPlayer Mini not responding - check wiring/power/SD card.");
  } else {
    dfPlayer.volume(20);        // 0-30
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
    for (uint8_t beam = 0; beam < NUM_NOTE_TRACKS; beam++) {
      const uint8_t bit = (uint8_t)(1 << beam);
      if ((changed & bit) && (mask & bit)) {          // this beam just got blocked -> note on
        const uint8_t track = beam + 1;                // beam 0 -> 0001_C4.mp3, beam 1 -> 0002_D4.mp3, ...
        Serial.printf("Beam %u blocked -> playMp3Folder(%u)\n", beam, track);
        dfPlayer.playMp3Folder(track);
      }
      // beam cleared: let the note ring out naturally instead of cutting it off
    }
    lastMask = mask;
  }

  if (millis() - lastRxMs > LINK_TIMEOUT_MS) {
    // With only beam 0 wired up there's nothing extra to silence here, but
    // once several beams are live this is where a dead-link timeout would
    // go (dfPlayer.stop()) so nothing gets stuck sounding forever.
  }
}
