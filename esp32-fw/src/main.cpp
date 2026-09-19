#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>

/* ------------------------------------------------------------------
 * Laser Harp - ESP32 audio engine (DFPlayer Mini version, 7 real beams)
 *
 * Receives a 1-byte beam-state mask from the ATmega32 (bit i = beam i
 * blocked, i = 0..6) and plays that beam's own note directly:
 *   beam 0 -> track 1 (0001_C4.mp3), beam 1 -> track 2 (0002_D4.mp3),
 *   ... beam 6 -> track 7 (0007_B4.mp3)
 *
 * SD card layout (confirmed still in use): a folder literally named
 * "MP3" at the card's root, containing files whose names start with a
 * 4-digit prefix (0001...mp3 .. 0007...mp3) - played via
 * dfPlayer.playMp3Folder(N).
 * ------------------------------------------------------------------ */

// UART2 to ATmega32 - keeps USB Serial (pins 1/3) free for the debug console
#define ATMEGA_RX_PIN 16   // ESP32 RX2  <-- ATmega32 PD1 (TXD) THROUGH A VOLTAGE DIVIDER (5V -> ~3.3V)
#define ATMEGA_TX_PIN 17   // ESP32 TX2  --> ATmega32 PD0 (RXD); direct wire is fine

// UART1 to DFPlayer Mini
#define DF_RX_PIN 26       // ESP32 RX1  <-- DFPlayer TX
#define DF_TX_PIN 27       // ESP32 TX1  --> DFPlayer RX (direct wire is fine; DFPlayer's RX tolerates 3.3V logic)

#define NUM_BEAMS 7         // beams 0-6 -> tracks 1-7 (0001_C4.mp3 .. 0007_B4.mp3)

HardwareSerial dfSerial(1);
DFRobotDFPlayerMini dfPlayer;

uint8_t lastMask = 0x00;
unsigned long lastRxMs = 0;
const unsigned long LINK_TIMEOUT_MS = 1000;

// Per-beam debounce so a fast run across different beams doesn't get
// blocked by another beam's cooldown - only re-triggering the *same*
// beam too quickly is guarded against.
unsigned long lastTriggerMs[NUM_BEAMS] = {0};
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

  if (!dfPlayer.begin(dfSerial, false)) { // false = no ACK required (prevents TimeOut lockups)
    Serial.println("DFPlayer Mini not responding - check wiring/power/SD card.");
  } else {
    dfPlayer.volume(24);        // Stable volume (0-30)
    dfPlayer.enableDAC();
    Serial.println("DFPlayer Mini ready (Volume: 24, ACK: Off).");
  }

  lastRxMs = millis();
  Serial.println("Ready - waiting for ATmega32 beam data on Serial2 (9600 baud).");
}

unsigned long lastGlobalPlayMs = 0;
const unsigned long GLOBAL_COOLDOWN_MS = 250; // DFPlayer needs at least 250ms to start decoding a track

void loop()
{
  if (dfPlayer.available()) {
    printDetail(dfPlayer.readType(), dfPlayer.read());
  }

  if (Serial2.available()) {
    const uint8_t mask = Serial2.read();
    lastRxMs = millis();

    const uint8_t changed = mask ^ lastMask;
    for (uint8_t beam = 0; beam < NUM_BEAMS; beam++) {
      const uint8_t bit = (uint8_t)(1 << beam);
      if ((changed & bit) && (mask & bit)) {              // this beam just got blocked -> note on
        if (millis() - lastGlobalPlayMs > GLOBAL_COOLDOWN_MS && millis() - lastTriggerMs[beam] > DEBOUNCE_MS) {
          const uint8_t track = beam + 1;                  // beam 0 -> track 1, beam 1 -> track 2, ...
          Serial.printf("Beam %u blocked -> playing track %04u (000%u...mp3)\n", beam, track, track);
          dfPlayer.playMp3Folder(track);
          lastTriggerMs[beam] = millis();
          lastGlobalPlayMs = millis();
        }
      }
      // beam cleared: let the note ring out naturally instead of cutting it off
    }
    lastMask = mask;
  }

  if (millis() - lastRxMs > LINK_TIMEOUT_MS) {
    // dead-link timeout hook, if ever needed
  }
}
