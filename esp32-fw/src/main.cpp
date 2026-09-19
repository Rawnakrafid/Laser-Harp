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
 *
 * NOTE (2026-09-19): a teammate's branch briefly replaced this with a
 * "play one fixed track (0008.mp3) on any beam, then ignore every beam
 * for up to 15s" design (a global isPlaying gate). That was reverted -
 * confirmed to be the actual cause of "beam plays once then goes dead
 * until reconnected": if the DFPlayerPlayFinished callback doesn't fire
 * reliably, isPlaying gets stuck true and every beam is ignored until
 * the 15s timeout. This file goes back to independent per-beam
 * triggering with no global lockout.
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
// beam too quickly is guarded against. No global "only one thing can
// play" gate - each beam is independent.
unsigned long lastTriggerMs[NUM_BEAMS] = {0};
const unsigned long DEBOUNCE_MS = 300;

// Minimum gap between ANY two commands sent to the DFPlayer over its own
// UART link, regardless of which beam triggered them. The DFPlayer Mini's
// serial receiver needs a little breathing room between command frames
// (0x7E ... 0xEF) - firing two commands back-to-back with ~0ms between
// them (e.g. a fast run across several beams, or two beams crossing the
// same ~1ms ATmega sample within one mask byte) doesn't corrupt anything
// immediately, but repeatedly doing it nudges the module's receiver a
// little further out of frame sync each time. That's consistent with
// "works perfectly at first, then degrades into a tick loop after a while
// of normal playing" - the desync accumulates rather than happening in
// one shot. Spacing every command out fixes it at the source instead of
// just reacting to the symptom once it appears.
unsigned long lastDfCommandMs = 0;
const unsigned long MIN_DF_COMMAND_GAP_MS = 100;

// Self-healing safety net: if the link still desyncs despite the spacing
// above (e.g. from noise on the wire), a run of TimeOut/WrongStack errors
// in a row is the module telling us it's lost frame sync. Re-running
// begin() forces a clean resync instead of staying stuck until someone
// power-cycles the board by hand.
//
// Guarded with its own cooldown (separate from RESYNC_COOLDOWN_MS's use
// elsewhere) so a module that's still unhappy right after a resync can't
// trigger another resync attempt within milliseconds - that "resync storm"
// (begin() firing over and over with no time for the module to actually
// settle) is worse than doing nothing, and can look exactly like total
// silence since the module never gets a stable moment to accept a play
// command in between.
uint8_t dfErrorStreak = 0;
const uint8_t DF_ERROR_STREAK_LIMIT = 3;
unsigned long lastResyncMs = 0;
const unsigned long RESYNC_COOLDOWN_MS = 10000;

void resyncDfPlayer() {
  const unsigned long now = millis();
  dfErrorStreak = 0;
  if (now - lastResyncMs < RESYNC_COOLDOWN_MS) {
    return;   // already tried recently - give it time to settle instead of hammering begin()
  }
  lastResyncMs = now;
  Serial.println(F("DFPlayer: too many errors in a row, re-syncing link..."));
  dfPlayer.begin(dfSerial);
  dfPlayer.volume(18);
  dfPlayer.enableDAC();
}

void printDetail(uint8_t type, int value) {
  switch (type) {
    case TimeOut:
      Serial.println(F("DFPlayer: Time Out!"));
      if (++dfErrorStreak >= DF_ERROR_STREAK_LIMIT) resyncDfPlayer();
      break;
    case WrongStack:
      Serial.println(F("DFPlayer: Stack Wrong!"));
      if (++dfErrorStreak >= DF_ERROR_STREAK_LIMIT) resyncDfPlayer();
      break;
    case DFPlayerCardInserted:
      Serial.println(F("DFPlayer: Card Inserted!"));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("DFPlayer: Card Removed!"));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("DFPlayer: Card Online!"));
      dfErrorStreak = 0;
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("DFPlayer: Number "));
      Serial.print(value);
      Serial.println(F(" Play Finished!"));
      dfErrorStreak = 0;         // a clean finished-track report means the link is healthy again
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

  delay(3000);   // DFPlayer's own datasheet: 1.5-3s (sometimes longer) to finish mounting
                 // the SD card before it will ACK any UART command - 600ms was too short

  bool dfOk = dfPlayer.begin(dfSerial);
  if (!dfOk) {
    Serial.println("DFPlayer not responding on first try, retrying once more...");
    delay(1000);
    dfOk = dfPlayer.begin(dfSerial);
  }

  if (!dfOk) {
    Serial.println("DFPlayer Mini not responding - check wiring/power/SD card.");
  } else {
    dfPlayer.volume(18);        // Safe stable volume: prevents brownout lockup (0-30)
    dfPlayer.enableDAC();
    Serial.println("DFPlayer Mini ready (Volume: 18 - Stable, 7-beam direct mapping).");
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
    for (uint8_t beam = 0; beam < NUM_BEAMS; beam++) {
      const uint8_t bit = (uint8_t)(1 << beam);
      if ((changed & bit) && (mask & bit)) {              // this beam just got blocked -> note on
        const unsigned long now = millis();
        if (now - lastTriggerMs[beam] > DEBOUNCE_MS && now - lastDfCommandMs >= MIN_DF_COMMAND_GAP_MS) {
          const uint8_t track = beam + 1;                  // beam 0 -> track 1, beam 1 -> track 2, ...
          Serial.printf("Beam %u blocked -> playing track %04u (000%u...mp3)\n", beam, track, track);
          dfPlayer.playMp3Folder(track);
          lastTriggerMs[beam] = now;
          lastDfCommandMs = now;
        }
        // else: dropped to protect the DFPlayer UART link from back-to-back
        // commands - the DFPlayer can only play one thing at a time anyway,
        // so losing an overlapping trigger costs nothing but a stricter
        // desync-immune link.
      }
      // beam cleared: let the note ring out naturally instead of cutting it off
    }
    lastMask = mask;
  }

  if (millis() - lastRxMs > LINK_TIMEOUT_MS) {
    // dead-link timeout hook, if ever needed
  }
}
