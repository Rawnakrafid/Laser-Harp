#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

/* ------------------------------------------------------------------
 * Laser Harp - ATmega32 beam detection + UART link to ESP32
 *
 * Phase: single beam (beam 0, PA0/ADC0) proven end-to-end before
 * scaling to all 8. Sends a 1-byte state mask to the ESP32 on PD1
 * (TXD) any time a beam's state changes, plus a periodic heartbeat
 * so the ESP32 can tell "all clear" from "link dead".
 *
 * Bit i of the mask = 1 means beam i is currently blocked.
 * ------------------------------------------------------------------ */

#define NUM_ACTIVE_BEAMS      1     /* raise to 8 once beam 0 is proven */
#define CALIBRATION_SAMPLES  50     /* boot-time baseline average, beams assumed clear */
#define CONFIRM_SAMPLES       5     /* consecutive samples required before flipping state */
#define TRIGGER_NUM            6    /* trigger below baseline * 6/10 */
#define TRIGGER_DEN            10
#define RELEASE_NUM             8   /* release above baseline * 8/10 */
#define RELEASE_DEN            10
#define HEARTBEAT_LOOPS       200   /* ~200ms at the 1ms loop delay below */

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
    DDRB |= (1 << PB0);            /* beam-0 indicator LED */

    adc_init();
    uart_init();

    /* Baseline calibration: assume beam 0 is clear at boot */
    uint32_t sum = 0;
    for (uint8_t i = 0; i < CALIBRATION_SAMPLES; i++) {
        sum += adc_read(0);
        _delay_ms(2);
    }
    const uint16_t baseline        = (uint16_t)(sum / CALIBRATION_SAMPLES);
    const uint16_t trigger_thresh  = (uint16_t)((uint32_t)baseline * TRIGGER_NUM / TRIGGER_DEN);
    const uint16_t release_thresh  = (uint16_t)((uint32_t)baseline * RELEASE_NUM / RELEASE_DEN);

    uint8_t  blocked         = 0;
    uint8_t  confirm_count   = 0;
    uint8_t  last_mask       = 0x00;
    uint16_t heartbeat_ticks = 0;

    while (1) {
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
            uart_send(mask);           /* heartbeat: link-alive signal even with no change */
            heartbeat_ticks = 0;
        }

        _delay_ms(1);
    }
}
