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

#define TOTAL_TRACKS 7     // 001.mp3 through 007.mp3

HardwareSerial dfSerial(1);
DFRobotDFPlayerMini dfPlayer;

uint8_t lastMask = 0x00;
unsigned long lastRxMs = 0;
const unsigned long LINK_TIMEOUT_MS = 1000;

uint8_t currentTrack = 1;
unsigned long lastTriggerMs = 0;
const unsigned long DEBOUNCE_MS = 300;

void printDetail(uint8_t type, int value) {
  switch (type) {
    case TimeOut:
      Serial.println(F("DFPlayer: Time Out!"));
      break;
    case WrongStack:
      Serial.println(F("DFPlayer: Stack Wrong!"));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("DFPlayer: Card Inserted!"));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("DFPlayer: Card Removed!"));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("DFPlayer: Card Online!"));
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("DFPlayer: Number "));
      Serial.print(value);
      Serial.println(F(" Play Finished!"));
      break;
    case DFPlayerError:
      Serial.print(F("DFPlayer: Error Code "));
      Serial.println(value);
      break;
    default:
      break;
  }
}

void setup()
{
  Serial.begin(115200);                                     // USB debug console
  Serial2.begin(9600, SERIAL_8N1, ATMEGA_RX_PIN, ATMEGA_TX_PIN);
  dfSerial.begin(9600, SERIAL_8N1, DF_RX_PIN, DF_TX_PIN);

  delay(600);

  if (!dfPlayer.begin(dfSerial)) {
    Serial.println("DFPlayer Mini not responding - check wiring/power/SD card.");
  } else {
    dfPlayer.volume(18);        // Safe stable volume: prevents brownout lockup (0-30)
    dfPlayer.enableDAC();
    Serial.println("DFPlayer Mini ready (Volume: 18 - Stable, Sequential Loop 1-7).");
  }

  lastRxMs = millis();
  Serial.println("Ready - waiting for ATmega32 beam data on Serial2 (9600 baud).");
}

void loop()
{
  if (dfPlayer.available()) {
    printDetail(dfPlayer.readType(), dfPlayer.read());
  }

  if (Serial2.available()) {
    const uint8_t mask = Serial2.read();
    lastRxMs = millis();

    const uint8_t changed = mask ^ lastMask;
    
    // Check if beam 0 was just interrupted (transition from 0 to 1)
    if ((changed & 0x01) && (mask & 0x01)) {
      if (millis() - lastTriggerMs > DEBOUNCE_MS) {
        const uint8_t trackToPlay = currentTrack;
        Serial.printf("Beam 0 interrupted -> Playing track %04u (000%u.mp3)\n", trackToPlay, trackToPlay);
        dfPlayer.playMp3Folder(trackToPlay);

        // Advance to next music track in loop: 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7 -> 1 ...
        currentTrack = (currentTrack % TOTAL_TRACKS) + 1;
        lastTriggerMs = millis();
      }
    }

    lastMask = mask;
  }

  if (millis() - lastRxMs > LINK_TIMEOUT_MS) {
    // dead-link timeout
  }
}
