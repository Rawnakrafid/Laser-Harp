#ifndef F_CPU
#define F_CPU 1000000UL /* 1 MHz Internal Calibrated RC Oscillator (lfuse: 0xe1) */
#endif

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdbool.h>

/* ==================================================================
 * Laser Harp - ATmega32 #2: Optical "Player Piano" Laser Controller
 *
 * HOW THIS WORKS:
 * ------------------------------------------------------------------
 * This chip controls the LASER DIODES directly. It does NOT need to
 * send any UART or electrical signals to the ESP32.
 *
 * SENSING MECHANISM:
 * - When a laser is ON, light shines on ATmega #1's LDR sensor.
 * - When this chip turns a laser OFF, the LDR goes dark.
 * - ATmega #1 detects the sudden darkness exactly as if a human hand
 *   just physically blocked the beam!
 * - ATmega #1 lights up its note-indicator LED and transmits the note
 *   over UART to the ESP32 to produce audio.
 *
 * MODES:
 * 1. MANUAL MODE:
 *    - All lasers are continuously ON (emitting light).
 *    - The human plays the harp by physically blocking the beams.
 *    - Mode LED is OFF.
 *
 * 2. AUTO MODE:
 *    - Lasers are normally ON (idle / silence).
 *    - To play a note, this chip momentarily turns OFF that beam's laser!
 *    - After the note duration, the laser turns back ON (release).
 *    - Button 2 advances to the next song in the playlist.
 *    - Mode LED is ON.
 *
 * CLOCK:
 * - Runs 100% on the internal 1 MHz RC oscillator. NO external crystal needed!
 * ================================================================== */

/* --- Control Buttons (Active LOW with internal pull-up) --- */
#define BTN_MODE_PIN        PD2  /* Pin 16: Pushbutton to GND (Manual <-> Auto) */
#define BTN_SONG_PIN        PD3  /* Pin 17: Pushbutton to GND (Next Song) */

/* --- Optional Status LEDs --- */
#define LED_MODE_PIN        PD0  /* Pin 14: ON in Auto Mode, OFF in Manual */
#define LED_SONG_PIN        PD1  /* Pin 15: Pulses when changing songs */

/* --- Laser Control Pins (Pins 22 to 28 = PC0 to PC6) ---
 * High = Transistor ON -> Laser ON (Emitting Light)
 * Low  = Transistor OFF -> Laser OFF (Darkness / "Hand Blocking Beam")
 */
#define LASER_B0_PIN        PC0  /* Pin 22: Beam 0 (Note C4 - Current Bench Test) */
#define LASER_B1_PIN        PC1  /* Pin 23: Beam 1 (Note D4) */
#define LASER_B2_PIN        PC2  /* Pin 24: Beam 2 (Note E4) */
#define LASER_B3_PIN        PC3  /* Pin 25: Beam 3 (Note F4) */
#define LASER_B4_PIN        PC4  /* Pin 26: Beam 4 (Note G4) */
#define LASER_B5_PIN        PC5  /* Pin 27: Beam 5 (Note A4) */
#define LASER_B6_PIN        PC6  /* Pin 28: Beam 6 (Note B4) */

#define DEBOUNCE_MS         30   /* Button debounce time */
#define RELEASE_GAP_MS      50   /* Laser turns back ON between notes so sensor resets */

/* Beam definitions (0 to 6) and REST */
#define BEAM_REST           0xFF /* No laser turned off = silence */
#define BEAM_0_C4           0
#define BEAM_1_D4           1
#define BEAM_2_E4           2
#define BEAM_3_F4           3
#define BEAM_4_G4           4
#define BEAM_5_A4           5
#define BEAM_6_B4           6

typedef struct {
    uint8_t  beam;        /* Which beam laser to turn OFF (or BEAM_REST for silence) */
    uint16_t duration_ms; /* Note duration in milliseconds */
} NoteEvent;

typedef struct {
    const char*       title;
    const NoteEvent*  notes;
    uint16_t          count;
} Song;

/* ==================================================================
 * SONG PLAYLIST
 * ================================================================== */

/* ==================================================================
 * SONG PLAYLIST (8 Curated Diatonic C4..B4 Songs)
 * ================================================================== */

/* --- Song 0: Jingle Bells --- */
static const NoteEvent SONG_JINGLE_BELLS[] = {
    /* "Jingle bells, jingle bells, jingle all the way" */
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 500}, {BEAM_REST, 100},
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 500}, {BEAM_REST, 100},
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 700}, {BEAM_REST, 200},

    /* "Oh what fun it is to ride in a one-horse open sleigh" */
    {BEAM_3_F4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_3_F4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 200}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 200}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 450}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 600}, {BEAM_REST, 200},

    /* "Jingle bells, jingle bells, jingle all the way" */
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 500}, {BEAM_REST, 100},
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 500}, {BEAM_REST, 100},
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 700}, {BEAM_REST, 200},

    /* "Oh what fun it is to ride in a one-horse open sleigh!" */
    {BEAM_3_F4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_3_F4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 200}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_0_C4, 800}, {BEAM_REST, 800}
};

/* --- Song 1: Seven Nation Army (The White Stripes) --- */
static const NoteEvent SONG_SEVEN_NATION[] = {
    {BEAM_2_E4, 450}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 300}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 300}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 300}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 550}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_6_B4, 700}, {BEAM_REST, 200},

    {BEAM_2_E4, 450}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 300}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 300}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 300}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_6_B4, 700}, {BEAM_REST, 350},

    {BEAM_2_E4, 450}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 300}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 300}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 300}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 550}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_6_B4, 700}, {BEAM_REST, 200},

    {BEAM_2_E4, 450}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 300}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 300}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 300}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_6_B4, 800}, {BEAM_REST, 800}
};

/* --- Song 2: Super Mario Bros Theme --- */
static const NoteEvent SONG_SUPER_MARIO[] = {
    {BEAM_2_E4, 180}, {BEAM_REST, 60}, {BEAM_2_E4, 180}, {BEAM_REST, 120}, {BEAM_2_E4, 180}, {BEAM_REST, 120},
    {BEAM_0_C4, 180}, {BEAM_REST, 60}, {BEAM_2_E4, 220}, {BEAM_REST, 120}, {BEAM_4_G4, 350}, {BEAM_REST, 400},

    {BEAM_0_C4, 250}, {BEAM_REST, 150}, {BEAM_4_G4, 250}, {BEAM_REST, 150}, {BEAM_2_E4, 250}, {BEAM_REST, 150},
    {BEAM_5_A4, 250}, {BEAM_REST, 100}, {BEAM_6_B4, 250}, {BEAM_REST, 100}, {BEAM_5_A4, 200}, {BEAM_REST, 100},
    {BEAM_4_G4, 350}, {BEAM_REST, 150},

    {BEAM_2_E4, 250}, {BEAM_REST, 100}, {BEAM_4_G4, 250}, {BEAM_REST, 100}, {BEAM_5_A4, 250}, {BEAM_REST, 100},
    {BEAM_3_F4, 200}, {BEAM_REST, 100}, {BEAM_4_G4, 200}, {BEAM_REST, 100},
    {BEAM_2_E4, 250}, {BEAM_REST, 100}, {BEAM_0_C4, 200}, {BEAM_REST, 100}, {BEAM_1_D4, 200}, {BEAM_REST, 100},
    {BEAM_6_B4, 450}, {BEAM_REST, 800}
};

/* --- Song 3: Ode to Joy (Beethoven) --- */
static const NoteEvent SONG_ODE_TO_JOY[] = {
    {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_3_F4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_0_C4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 500}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 700}, {BEAM_REST, 250},

    {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_3_F4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_0_C4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 500}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_0_C4, 700}, {BEAM_REST, 800}
};

/* --- Song 4: Twinkle, Twinkle, Little Star --- */
static const NoteEvent SONG_TWINKLE[] = {
    {BEAM_0_C4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_5_A4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_5_A4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 700}, {BEAM_REST, 150},
    {BEAM_3_F4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_0_C4, 700}, {BEAM_REST, 800}
};

/* --- Song 5: Frère Jacques (Are You Sleeping) --- */
static const NoteEvent SONG_FRERE_JACQUES[] = {
    {BEAM_0_C4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_0_C4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 350}, {BEAM_REST, RELEASE_GAP_MS},

    {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 600}, {BEAM_REST, 100},
    {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 600}, {BEAM_REST, 100},

    {BEAM_4_G4, 200}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_5_A4, 200}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 200}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 200}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 200}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_5_A4, 200}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 200}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 200}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 350}, {BEAM_REST, RELEASE_GAP_MS},

    {BEAM_0_C4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_0_C4, 600}, {BEAM_REST, 100},
    {BEAM_0_C4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_0_C4, 700}, {BEAM_REST, 800}
};

/* --- Song 6: Row, Row, Row Your Boat --- */
static const NoteEvent SONG_ROW_BOAT[] = {
    {BEAM_0_C4, 400}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 400}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_0_C4, 300}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 200}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 450}, {BEAM_REST, 100},

    {BEAM_2_E4, 300}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 200}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 300}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 200}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 700}, {BEAM_REST, 150},

    {BEAM_5_A4, 200}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_5_A4, 200}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_5_A4, 200}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 200}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 200}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 200}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 200}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 200}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 200}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_0_C4, 200}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 200}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 200}, {BEAM_REST, RELEASE_GAP_MS},

    {BEAM_4_G4, 300}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_3_F4, 200}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 300}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 200}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_0_C4, 750}, {BEAM_REST, 800}
};

/* --- Song 7: Sparkle (Your Name / Kimi no Na wa - RADWIMPS) --- */
static const NoteEvent SONG_SPARKLE[] = {
    /* Iconic Intro Arpeggio */
    {BEAM_0_C4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_6_B4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_5_A4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 500}, {BEAM_REST, 150},

    {BEAM_0_C4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_4_G4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_6_B4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_5_A4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 600}, {BEAM_REST, 250},

    /* Main Verse Melody: "Mada kono sekai wa..." */
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 300}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 500}, {BEAM_REST, 150},

    /* "...boku wo kainarashitetai mitai da" */
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 600}, {BEAM_REST, 200},

    /* "Nozomidoori darou? Utsukushiku mogaku yo" */
    {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 300}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_0_C4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_6_B4, 500}, {BEAM_REST, 150},
    {BEAM_0_C4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 350}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_0_C4, 650}, {BEAM_REST, 250},

    /* "Tagai no sunadokei nagame nagara kisu wo shiyou yo" */
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 300}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 500}, {BEAM_REST, 150},
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 600}, {BEAM_REST, 200},

    /* Climax: "Sayonara kara ichiban tooi basho de machiawase wo shiyou" */
    {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 300}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_0_C4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_6_B4, 500}, {BEAM_REST, 150},
    {BEAM_0_C4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_1_D4, 250}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_2_E4, 250}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_4_G4, 350}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_5_A4, 650}, {BEAM_REST, 150},
    {BEAM_4_G4, 300}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_2_E4, 300}, {BEAM_REST, RELEASE_GAP_MS},
    {BEAM_1_D4, 300}, {BEAM_REST, RELEASE_GAP_MS}, {BEAM_0_C4, 800}, {BEAM_REST, 800}
};

/* Master Playlist */
static const Song PLAYLIST[] = {
    { "Jingle Bells",        SONG_JINGLE_BELLS,     sizeof(SONG_JINGLE_BELLS)     / sizeof(NoteEvent) },
    { "Seven Nation Army",   SONG_SEVEN_NATION,     sizeof(SONG_SEVEN_NATION)     / sizeof(NoteEvent) },
    { "Super Mario Bros",    SONG_SUPER_MARIO,      sizeof(SONG_SUPER_MARIO)      / sizeof(NoteEvent) },
    { "Ode to Joy",          SONG_ODE_TO_JOY,       sizeof(SONG_ODE_TO_JOY)       / sizeof(NoteEvent) },
    { "Twinkle Twinkle",     SONG_TWINKLE,          sizeof(SONG_TWINKLE)          / sizeof(NoteEvent) },
    { "Frere Jacques",       SONG_FRERE_JACQUES,    sizeof(SONG_FRERE_JACQUES)    / sizeof(NoteEvent) },
    { "Row Your Boat",       SONG_ROW_BOAT,         sizeof(SONG_ROW_BOAT)         / sizeof(NoteEvent) },
    { "Sparkle (Your Name)", SONG_SPARKLE,          sizeof(SONG_SPARKLE)          / sizeof(NoteEvent) }
};
#define NUM_SONGS (sizeof(PLAYLIST) / sizeof(Song))

/* ==================================================================
 * HARDWARE CONTROL
 * ================================================================== */

static void all_lasers_on(void)
{
    /* High = Lasers ON on both PORTB (Pins 1-7) and PORTC (Pins 22-28) */
    PORTB |= 0x7F;         /* Pins 1 to 7 (PB0 to PB6) */
    PORTC |= 0x7F;         /* Pins 22 to 28 (PC0 to PC6) */
    PORTD |= (1 << PD4);   /* Pin 18 (PD4) */
}

/* Set specific beam's laser state: true = ON (light), false = OFF (dark / note active) */
static void set_beam_laser(uint8_t beam, bool light_on)
{
    if (beam <= 6) {
        if (light_on) {
            PORTB |= (uint8_t)(1 << beam);
            PORTC |= (uint8_t)(1 << beam);
            if (beam == 0) PORTD |= (1 << PD4);
        } else {
            /* LOW = Laser OFF -> LDR goes dark -> note triggers! */
            PORTB &= (uint8_t)~(1 << beam);
            PORTC &= (uint8_t)~(1 << beam);
            if (beam == 0) PORTD &= (uint8_t)~(1 << PD4);
        }
    }
}

static void gpio_init(void)
{
    /* Disable JTAG to release PC2-PC5 (Pins 24-27) for standard GPIO */
    MCUCSR |= (1 << JTD);
    MCUCSR |= (1 << JTD);

    /* Buttons as inputs with internal pull-up */
    DDRD  &= (uint8_t)~((1 << BTN_MODE_PIN) | (1 << BTN_SONG_PIN));
    PORTD |= (uint8_t)((1 << BTN_MODE_PIN)  | (1 << BTN_SONG_PIN));

    /* Status LEDs as outputs */
    DDRD  |= (uint8_t)((1 << LED_MODE_PIN) | (1 << LED_SONG_PIN));
    PORTD &= (uint8_t)~((1 << LED_MODE_PIN) | (1 << LED_SONG_PIN));

    /* Laser pins: outputs on both PORTB (Pins 1-7) and PORTC (Pins 22-28) */
    DDRB  |= 0x7F;
    DDRC  |= 0x7F;
    DDRD  |= (uint8_t)(1 << PD4);

    /* Default state: All lasers ON */
    all_lasers_on();
}

/* ==================================================================
 * BUTTON DEBOUNCER
 * ================================================================== */

typedef struct {
    uint8_t  pin;
    uint8_t  last_reading;
    uint8_t  stable_state;
    uint16_t debounce_timer_ms;
} ButtonTracker;

static ButtonTracker btn_mode = { BTN_MODE_PIN, 1, 1, 0 };
static ButtonTracker btn_song = { BTN_SONG_PIN, 1, 1, 0 };

static bool update_button_edge(ButtonTracker* btn)
{
    const uint8_t reading = (PIND & (1 << btn->pin)) ? 1 : 0;
    bool pressed_edge = false;

    if (reading != btn->last_reading) {
        btn->debounce_timer_ms = DEBOUNCE_MS;
    } else if (btn->debounce_timer_ms > 0) {
        btn->debounce_timer_ms--;
        if (btn->debounce_timer_ms == 0) {
            if (btn->stable_state != reading) {
                btn->stable_state = reading;
                if (btn->stable_state == 0) { /* Active LOW press detected */
                    pressed_edge = true;
                }
            }
        }
    }

    btn->last_reading = reading;
    return pressed_edge;
}

/* ==================================================================
 * MAIN PROGRAM
 * ================================================================== */

int main(void)
{
    gpio_init();

    /* Let pull-ups and power rails settle */
    _delay_ms(50);
    btn_mode.last_reading = (PIND & (1 << BTN_MODE_PIN)) ? 1 : 0;
    btn_mode.stable_state = btn_mode.last_reading;
    btn_mode.debounce_timer_ms = 0;

    btn_song.last_reading = (PIND & (1 << BTN_SONG_PIN)) ? 1 : 0;
    btn_song.stable_state = btn_song.last_reading;
    btn_song.debounce_timer_ms = 0;

    /* Startup power-on test: blink Laser 0 to prove the chip is alive */
    for (uint8_t i = 0; i < 2; i++) {
        all_lasers_on();
        _delay_ms(200);
        set_beam_laser(0, false);
        _delay_ms(200);
    }
    all_lasers_on(); /* Stay solid ON in Manual Mode */

    bool is_auto_mode = false;
    uint8_t current_song_idx = 0;
    uint16_t current_note_idx = 0;
    uint16_t note_remaining_ms = 0;
    uint8_t currently_dark_beam = BEAM_REST;
    uint16_t song_led_pulse_ms = 0;

    while (1) {
        /* Check Mode Toggle Button */
        if (update_button_edge(&btn_mode)) {
            is_auto_mode = !is_auto_mode;

            if (is_auto_mode) {
                /* Enter AUTO mode: Turn Mode LED ON */
                PORTD |= (uint8_t)(1 << LED_MODE_PIN);
                current_note_idx = 0;
                note_remaining_ms = 0;
                currently_dark_beam = BEAM_REST;
            } else {
                /* Enter MANUAL mode: All lasers ON, Mode LED OFF */
                all_lasers_on();
                PORTD &= (uint8_t)~(1 << LED_MODE_PIN);
            }
        }

        /* Check Song Change Button */
        if (update_button_edge(&btn_song)) {
            if (is_auto_mode) {
                all_lasers_on();
                current_song_idx = (current_song_idx + 1) % NUM_SONGS;
                current_note_idx = 0;
                note_remaining_ms = 0;
                currently_dark_beam = BEAM_REST;
                song_led_pulse_ms = 150; /* Blink song LED */
            }
        }

        /* Pulse Song LED */
        if (song_led_pulse_ms > 0) {
            PORTD |= (uint8_t)(1 << LED_SONG_PIN);
            song_led_pulse_ms--;
        } else {
            PORTD &= (uint8_t)~(1 << LED_SONG_PIN);
        }

        /* Mode Execution */
        if (is_auto_mode) {
            if (note_remaining_ms == 0) {
                /* Previous note finished: turn laser back ON */
                if (currently_dark_beam != BEAM_REST) {
                    set_beam_laser(currently_dark_beam, true);
                    currently_dark_beam = BEAM_REST;
                }

                const Song* cur_song = &PLAYLIST[current_song_idx];
                if (current_note_idx >= cur_song->count) {
                    current_note_idx = 0; /* Loop song */
                }

                const NoteEvent* cur_note = &cur_song->notes[current_note_idx];

                if (cur_note->beam != BEAM_REST) {
                    /* Strike note: turn this laser OFF (darkness triggers ATmega1!) */
                    set_beam_laser(cur_note->beam, false);
                    currently_dark_beam = cur_note->beam;
                } else {
                    /* Rest: all lasers ON */
                    all_lasers_on();
                    currently_dark_beam = BEAM_REST;
                }

                note_remaining_ms = cur_note->duration_ms;
                current_note_idx++;
            } else {
                note_remaining_ms--;
            }
        } else {
            /* MANUAL MODE: Keep all lasers shining so human can block them */
            all_lasers_on();
        }

        _delay_ms(1); /* 1ms loop tick */
    }

    return 0;
}
