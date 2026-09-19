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

#define NUM_BEAMS 7         // 7 active beams (0 to 6)

HardwareSerial dfSerial(1);
DFRobotDFPlayerMini dfPlayer;

uint8_t lastMask = 0x00;
unsigned long lastRxMs = 0;
const unsigned long LINK_TIMEOUT_MS = 1000;

unsigned long lastTriggerMs[NUM_BEAMS] = {0};
const unsigned long DEBOUNCE_MS = 300;

int8_t activeBeam = -1;
bool isPlaying = false;
unsigned long lastGlobalPlayMs = 0;
const unsigned long MIN_RETRIGGER_MS = 600; // Minimum time before restarting same track

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
      Serial.print(F("DFPlayer: Track Finished -> Ready for next interrupt.\n"));
      isPlaying = false;
      activeBeam = -1;
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
    dfPlayer.volume(22);        // Clean stable volume (0-30)
    dfPlayer.enableDAC();
    Serial.println("DFPlayer Mini ready (Continuous Playback Mode).");
  }

  lastRxMs = millis();
  Serial.println("Ready - waiting for ATmega32 beam data on Serial2 (9600 baud).");
}

unsigned long playStartMs = 0;
const unsigned long TRACK_LENGTH_MS = 15000; // Plays up to 15 seconds uninterrupted unless track finishes earlier

void loop()
{
  if (dfPlayer.available()) {
    printDetail(dfPlayer.readType(), dfPlayer.read());
  }

  // Auto-reset isPlaying if track has finished or max duration reached
  if (isPlaying && (millis() - playStartMs > TRACK_LENGTH_MS)) {
    Serial.println("Track play window complete -> Ready for next trigger.");
    isPlaying = false;
  }

  if (Serial2.available()) {
    const uint8_t mask = Serial2.read();
    lastRxMs = millis();

    const uint8_t changed = mask ^ lastMask;
    for (uint8_t beam = 0; beam < NUM_BEAMS; beam++) {
      const uint8_t bit = (uint8_t)(1 << beam);
      
      // Beam just transitioned from CLEAR to BLOCKED
      if ((changed & bit) && (mask & bit)) {
        if (!isPlaying) {
          Serial.printf("Beam %u (LDR %u) hit -> Starting 0008.mp3\n", beam, beam + 1);
          dfPlayer.playMp3Folder(8);
          isPlaying = true;
          playStartMs = millis();
        } else {
          // Song is already playing smoothly -> Do NOT restart it!
        }
      }
    }
    lastMask = mask;
  }

  if (millis() - lastRxMs > LINK_TIMEOUT_MS) {
    // dead-link timeout hook
  }
}
