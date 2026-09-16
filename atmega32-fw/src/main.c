#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

/* ------------------------------------------------------------------
 * Laser Harp - ATmega32 beam detection + UART link to ESP32
 *
 * All 7 beams (PA0-PA6 / ADC0-6), one per note (C4..B4). Bit i of the
 * UART mask = 1 means beam i is currently blocked. Sent on any change,
 * plus a periodic heartbeat so the ESP32 can tell "all clear" from
 * "link dead".
 * ------------------------------------------------------------------ */

#define NUM_BEAMS              7    /* PA0-PA6, one per recorded note */
#define CALIBRATION_SAMPLES   50    /* boot-time baseline average per beam, beams assumed clear */
#define CONFIRM_SAMPLES        5    /* consecutive samples required before flipping state */
#define TRIGGER_NUM             6   /* trigger below baseline * 6/10 */
#define TRIGGER_DEN             10
#define RELEASE_NUM              8  /* release above baseline * 8/10 */
#define RELEASE_DEN             10
#define HEARTBEAT_LOOPS        200  /* ~200ms at the ~1ms sweep delay below */

typedef struct {
    uint16_t trigger_thresh;
    uint16_t release_thresh;
    uint8_t  blocked;
    uint8_t  confirm_count;
} beam_state_t;

static beam_state_t beams[NUM_BEAMS];

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

int main(void)
{
    DDRB = 0x7F;   /* PB0-PB6 as outputs: one indicator LED per beam */

    adc_init();
    uart_init();

    /* Baseline calibration: assume all beams are clear at boot */
    for (uint8_t ch = 0; ch < NUM_BEAMS; ch++) {
        uint32_t sum = 0;
        for (uint8_t i = 0; i < CALIBRATION_SAMPLES; i++) {
            sum += adc_read(ch);
            _delay_ms(2);
        }
        const uint16_t baseline = (uint16_t)(sum / CALIBRATION_SAMPLES);
        beams[ch].trigger_thresh = (uint16_t)((uint32_t)baseline * TRIGGER_NUM / TRIGGER_DEN);
        beams[ch].release_thresh = (uint16_t)((uint32_t)baseline * RELEASE_NUM / RELEASE_DEN);
        beams[ch].blocked        = 0;
        beams[ch].confirm_count  = 0;
    }

    uint8_t  last_mask       = 0x00;
    uint16_t heartbeat_ticks = 0;

    while (1) {
        uint8_t mask = 0x00;

        for (uint8_t ch = 0; ch < NUM_BEAMS; ch++) {
            const uint16_t sample = adc_read(ch);
            beam_state_t *b = &beams[ch];

            if (!b->blocked && sample < b->trigger_thresh) {
                if (++b->confirm_count >= CONFIRM_SAMPLES) {
                    b->blocked = 1;
                    b->confirm_count = 0;
                }
            } else if (b->blocked && sample > b->release_thresh) {
                if (++b->confirm_count >= CONFIRM_SAMPLES) {
                    b->blocked = 0;
                    b->confirm_count = 0;
                }
            } else {
                b->confirm_count = 0;
            }

            if (b->blocked) {
                PORTB |= (uint8_t)(1 << ch);
                mask   |= (uint8_t)(1 << ch);
            } else {
                PORTB &= (uint8_t)~(1 << ch);
            }
        }

        if (mask != last_mask) {
            uart_send(mask);
            last_mask = mask;
            heartbeat_ticks = 0;
        } else if (++heartbeat_ticks >= HEARTBEAT_LOOPS) {
            uart_send(mask);           /* heartbeat: link-alive signal even with no change */
            heartbeat_ticks = 0;
        }

        _delay_ms(1);
    }
}
