#define F_CPU 1000000UL   /* internal RC oscillator, factory-default fuses -- no crystal on this board */
#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

/* ------------------------------------------------------------------------
 * 7-laser light-show controller (standalone board: "atmega-laser")
 *
 * This chip does NOT talk to ATmega1 or the ESP32 -- no UART, no relay.
 * It directly drives the SAME 7 laser diodes that ATmega1's LDR
 * photosensors already watch.
 *
 *   MANUAL mode: all 7 lasers held continuously ON, exactly like the
 *                original always-on wiring. You block them by hand as
 *                usual -- nothing downstream changes.
 *   AUTO mode:   this chip switches the lasers on/off in a fixed pattern.
 *                From the LDR's point of view, "laser off" looks
 *                identical to "beam blocked by a hand", so ATmega1, the
 *                ESP32, and the whole audio pipeline keep working with
 *                zero changes on their end.
 *
 * One push-button on PD2 toggles between the two modes.
 *
 * Hardware per laser (x7, PC0-PC6): laser 'S'/'+' -> +5V directly,
 * laser '-' -> transistor collector, transistor emitter -> GND,
 * GPIO -> two 220ohm resistors in series -> transistor base.
 *
 * PC2-PC5 double as JTAG pins -- MCUCSR/JTD is cleared below so they
 * behave as plain GPIO without needing to touch the JTAGEN fuse.
 * ------------------------------------------------------------------------ */

#define LASER_MASK       0x7Fu     /* PC0-PC6 */
#define MODE_SWITCH_PIN  PD2

typedef enum { MODE_MANUAL = 0, MODE_AUTO = 1 } mode_t;
static mode_t current_mode = MODE_MANUAL;

typedef struct {
    uint8_t  mask;          /* bit i = laser i ON */
    uint16_t duration_ms;
} step_t;

/* Generic scan-up/down + arpeggiated-chord demo pattern.
 * Not derived from any song -- just a placeholder light show. */
static const step_t DEMO_SEQUENCE[] = {
    {0x01, 150}, {0x02, 150}, {0x04, 150}, {0x08, 150},
    {0x10, 150}, {0x20, 150}, {0x40, 150},
    {0x40, 150}, {0x20, 150}, {0x10, 150}, {0x08, 150},
    {0x04, 150}, {0x02, 150}, {0x01, 150},
    {0x15, 400},   /* lasers 0+2+4 */
    {0x2A, 400},   /* lasers 1+3+5 */
    {0x49, 400},   /* lasers 0+3+6 */
    {0x7F, 600},   /* all 7        */
    {0x00, 300},   /* pause        */
};
#define DEMO_LEN (sizeof(DEMO_SEQUENCE) / sizeof(DEMO_SEQUENCE[0]))

static inline void lasers_write(uint8_t mask) {
    PORTC = (uint8_t)((PORTC & (uint8_t)~LASER_MASK) | (mask & LASER_MASK));
}

/* Debounced press detector -- call roughly once per millisecond.
 * Returns 1 exactly once per confirmed press (HIGH -> LOW held stable
 * for ~20 consecutive samples), else 0. */
static uint8_t switch_pressed(void) {
    static uint8_t last_stable = 1;   /* idles HIGH via internal pull-up */
    static uint8_t candidate   = 1;
    static uint8_t count       = 0;

    uint8_t sample = (PIND & (1 << MODE_SWITCH_PIN)) ? 1 : 0;

    if (sample != candidate) {
        candidate = sample;
        count = 0;
    } else if (count < 20) {
        count++;
    }

    if (count == 20 && candidate != last_stable) {
        uint8_t was_press = (uint8_t)((last_stable == 1) && (candidate == 0));
        last_stable = candidate;
        return was_press;
    }
    return 0;
}

/* 1ms-resolution wait that keeps polling the switch and returns early
 * (with the mode already flipped) the instant a press is confirmed. */
static void delay_ms_checked(uint16_t ms) {
    for (uint16_t i = 0; i < ms; i++) {
        _delay_ms(1);
        if (switch_pressed()) {
            current_mode = (current_mode == MODE_MANUAL) ? MODE_AUTO : MODE_MANUAL;
            return;
        }
    }
}

int main(void) {
    /* Clear JTD so PC2-PC5 behave as plain GPIO (must be written twice
     * within 4 clock cycles). Skip this only if you've permanently
     * disabled the JTAGEN fuse instead. */
    MCUCSR |= (1 << JTD);
    MCUCSR |= (1 << JTD);

    DDRC  |= LASER_MASK;                /* PC0-PC6 as outputs */
    lasers_write(0x00);

    DDRD  &= (uint8_t)~(1 << MODE_SWITCH_PIN);  /* PD2 as input       */
    PORTD |= (1 << MODE_SWITCH_PIN);            /* internal pull-up   */

    while (1) {
        if (current_mode == MODE_MANUAL) {
            lasers_write(LASER_MASK);           /* all lasers ON */
            while (current_mode == MODE_MANUAL) {
                delay_ms_checked(1000);         /* idles, polling the switch every ms */
            }
        } else {
            for (uint8_t i = 0; i < DEMO_LEN && current_mode == MODE_AUTO; i++) {
                lasers_write(DEMO_SEQUENCE[i].mask);
                delay_ms_checked(DEMO_SEQUENCE[i].duration_ms);
            }
        }
    }
}
