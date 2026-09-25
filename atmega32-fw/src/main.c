#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

/* ------------------------------------------------------------------
 * Laser Harp - ATmega32 beam detection + UART link to ESP32
 *
 * Two-mode version: a physical switch picks between
 *   MANUAL - real beam sensors, live playing (this is the existing
 *            single-beam detection logic, unchanged)
 *   AUTO   - ignores the sensors and sends a canned demo sequence
 *            instead, useful for testing the ESP32 + speaker chain
 *            before all the physical beams are wired up
 *
 * MODE SWITCH WIRING: one leg to PD2, the other to GND. Open (pin
 * reads HIGH via the internal pull-up) = MANUAL. Closed to GND (pin
 * reads LOW) = AUTO. PD2 is free - PD0/PD1 are the UART to the ESP32.
 *
 * The AUTO demo pattern below is an original one I made up for this
 * test (a scan up and down the 7 beams, three arpeggiated chords,
 * then all beams together) - not any existing song. It's just a
 * wiring/pipeline check; swap DEMO_SEQUENCE for whatever you want
 * once you're driving this from something else.
 *
 * Bit i of the mask sent over UART = 1 means beam i is "blocked".
 * ------------------------------------------------------------------ */

#define NUM_ACTIVE_BEAMS      1     /* raise to 8 once beam 0 is proven - unchanged, only affects MANUAL mode */
#define CALIBRATION_SAMPLES  50     /* boot-time baseline average, beams assumed clear */
#define CONFIRM_SAMPLES       5     /* consecutive samples required before flipping state */
#define TRIGGER_NUM            6    /* trigger below baseline * 6/10 */
#define TRIGGER_DEN            10
#define RELEASE_NUM             8   /* release above baseline * 8/10 */
#define RELEASE_DEN            10
#define HEARTBEAT_LOOPS       200   /* ~200ms at the 1ms loop delay below */

#define MODE_SWITCH_PIN  PD2

typedef struct {
    uint8_t  mask;
    uint16_t durationMs;
} SequenceStep;

/* Original demo pattern - not any existing song: scan up the 7 beams,
 * scan back down, three arpeggiated chords, then all 7 beams together. */
static const SequenceStep DEMO_SEQUENCE[] = {
    {0x01, 200}, {0x02, 200}, {0x04, 200}, {0x08, 200},
    {0x10, 200}, {0x20, 200}, {0x40, 200},
    {0x20, 200}, {0x10, 200}, {0x08, 200}, {0x04, 200}, {0x02, 200}, {0x01, 200},
    {0x00, 200},
    {0x15, 400},   /* beams 0+2+4 */
    {0x2A, 400},   /* beams 1+3+5 */
    {0x49, 600},   /* beams 0+3+6 */
    {0x00, 300},
    {0x7F, 500},   /* all 7 beams together */
    {0x00, 500},
};
#define DEMO_SEQUENCE_LEN (sizeof(DEMO_SEQUENCE) / sizeof(DEMO_SEQUENCE[0]))

static void adc_init(void)
{
    ADMUX  = (1 << REFS0);                                     /* AVCC ref, right-adjusted, start on channel 0 */
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0); /* enable, /128 -> 125kHz ADC clock */
}

static uint16_t adc_read(uint8_t channel)
{
    ADMUX = (ADMUX & 0xE0) | (channel & 0x1F);   /* keep REFS bits, select channel */
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC)) {}
    return ADC;
}

/* USART, 9600 baud @ 16MHz (~0.16% error, fine for this) */
static void uart_init(void)
{
    const uint16_t ubrr = 103;
    UBRRH = (uint8_t)(ubrr >> 8);
    UBRRL = (uint8_t)(ubrr & 0xFF);
    UCSRB = (1 << TXEN);                                       /* TX only for now */
    UCSRC = (1 << URSEL) | (1 << UCSZ1) | (1 << UCSZ0);        /* 8N1; URSEL required to hit UCSRC not UBRRH */
}

static void uart_send(uint8_t data)
{
    while (!(UCSRA & (1 << UDRE))) {}
    UDR = data;
}

static void mode_switch_init(void)
{
    DDRD  &= (uint8_t)~(1 << MODE_SWITCH_PIN);   /* input */
    PORTD |= (uint8_t)(1 << MODE_SWITCH_PIN);    /* internal pull-up */
}

static uint8_t is_auto_mode(void)
{
    return (PIND & (1 << MODE_SWITCH_PIN)) == 0;   /* LOW (switched to GND) = AUTO */
}

/* _delay_ms() needs a compile-time constant, so step through it in
 * small fixed chunks for a runtime-variable delay. */
static void delay_ms_var(uint16_t ms)
{
    while (ms--) {
        _delay_ms(1);
    }
}

static void run_auto_demo(void)
{
    for (uint8_t i = 0; i < DEMO_SEQUENCE_LEN; i++) {
        if (!is_auto_mode()) {
            uart_send(0x00);   /* switch flipped mid-sequence - clear and drop back to manual immediately */
            return;
        }
        uart_send(DEMO_SEQUENCE[i].mask);
        delay_ms_var(DEMO_SEQUENCE[i].durationMs);
    }
    uart_send(0x00);   /* always end a full pass on all-clear */
}

int main(void)
{
    DDRB |= (1 << PB0);            /* beam-0 indicator LED */

    /* Quick startup blink test: 3 blinks to confirm startup */
    for (uint8_t i = 0; i < 3; i++) {
        PORTB |= (1 << PB0);
        _delay_ms(100);
        PORTB &= (uint8_t)~(1 << PB0);
        _delay_ms(100);
    }

    adc_init();
    uart_init();
    mode_switch_init();

    /* Baseline calibration */
    uint32_t sum = 0;
    for (uint8_t i = 0; i < CALIBRATION_SAMPLES; i++) {
        sum += adc_read(0);
        _delay_ms(2);
    }
    uint16_t baseline = (uint16_t)(sum / CALIBRATION_SAMPLES);

    /* Fallback protection */
    if (baseline < 200) {
        baseline = 600;
    }

    const uint16_t trigger_thresh  = (uint16_t)((uint32_t)baseline * TRIGGER_NUM / TRIGGER_DEN);
    const uint16_t release_thresh  = (uint16_t)((uint32_t)baseline * RELEASE_NUM / RELEASE_DEN);

    uint8_t  blocked         = 0;
    uint8_t  confirm_count   = 0;
    uint8_t  last_mask       = 0x00;
    uint16_t heartbeat_ticks = 0;

    while (1) {
        if (is_auto_mode()) {
            run_auto_demo();
            last_mask = 0x00;   /* so MANUAL resumes cleanly if the switch flips back */
            continue;
        }

        /* MANUAL mode: identical beam-detect logic to the original firmware */
        const uint16_t sample = adc_read(0);

        if (!blocked && sample < trigger_thresh) {
            if (++confirm_count >= CONFIRM_SAMPLES) {
                blocked = 1;
                confirm_count = 0;
            }
        } else if (blocked && sample > release_thresh) {
            if (++confirm_count >= CONFIRM_SAMPLES) {
                blocked = 0;
                confirm_count = 0;
            }
        } else {
            confirm_count = 0;
        }

        if (blocked) {
            PORTB |= (1 << PB0);
        } else {
            PORTB &= (uint8_t)~(1 << PB0);
        }

        const uint8_t mask = blocked ? 0x01 : 0x00;

        if (mask != last_mask) {
            uart_send(mask);
            last_mask = mask;
            heartbeat_ticks = 0;
        } else if (++heartbeat_ticks >= HEARTBEAT_LOOPS) {
            uart_send(mask);           /* heartbeat */
            heartbeat_ticks = 0;
        }

        _delay_ms(1);
    }
}
