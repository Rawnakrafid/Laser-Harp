#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

/* ------------------------------------------------------------------
 * Laser Harp - ATmega32 #2: mode switch (MANUAL relay / AUTO demo)
 *
 * Sits between ATmega #1 (beam detection) and the ESP32 (audio):
 *
 *   ATmega #1  --TXD-->  RXD (PD0) [this chip] TXD (PD1)  -->  ESP32
 *
 * Both hops run the same USART peripheral (RX and TX are independent
 * pins/paths on one hardware UART, so this works as a single chip,
 * no software/bit-banged serial needed), at 9600 baud 8N1, matching
 * both existing links unchanged.
 *
 * A switch on PD2 picks the mode:
 *   MANUAL (switch open, PD2 reads HIGH via internal pull-up):
 *     every byte that arrives from ATmega #1 is immediately relayed
 *     to the ESP32, unchanged - this is a pure passthrough, so live
 *     playing behaves exactly as it did with one ATmega.
 *   AUTO (switch closed to GND, PD2 reads LOW):
 *     bytes from ATmega #1 are drained/ignored, and this chip sends
 *     its own canned demo sequence to the ESP32 instead - same
 *     original demo pattern as before (a scan up and down the 7
 *     beams, three arpeggiated chords, then all 7 beams together).
 *     Not any existing song - just a wiring/pipeline check pattern,
 *     safe to swap out for anything else later.
 *
 * MODE SWITCH WIRING: one leg to PD2, the other to GND.
 * ------------------------------------------------------------------ */

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

/* USART, 9600 baud @ 16MHz (~0.16% error) - same settings as the
 * existing ATmega1<->ESP32 link on both sides of this relay. */
static void uart_init(void)
{
    const uint16_t ubrr = 103;
    UBRRH = (uint8_t)(ubrr >> 8);
    UBRRL = (uint8_t)(ubrr & 0xFF);
    UCSRB = (1 << TXEN) | (1 << RXEN);                          /* both directions needed here */
    UCSRC = (1 << URSEL) | (1 << UCSZ1) | (1 << UCSZ0);         /* 8N1 */
}

static void uart_send(uint8_t data)
{
    while (!(UCSRA & (1 << UDRE))) {}
    UDR = data;
}

static uint8_t uart_available(void)
{
    return (UCSRA & (1 << RXC)) != 0;
}

static uint8_t uart_read(void)
{
    return UDR;
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
 * small fixed chunks for a runtime-variable delay. While waiting we
 * also keep draining ATmega #1's incoming bytes so its UART buffer
 * never backs up while we're off in AUTO mode. */
static void auto_delay_ms(uint16_t ms)
{
    while (ms--) {
        if (uart_available()) {
            (void)uart_read();   /* discard - AUTO mode ignores the real sensor */
        }
        _delay_ms(1);
    }
}

static void run_auto_demo(void)
{
    for (uint8_t i = 0; i < DEMO_SEQUENCE_LEN; i++) {
        if (!is_auto_mode()) {
            uart_send(0x00);   /* switch flipped mid-sequence - clear and drop back to MANUAL immediately */
            return;
        }
        uart_send(DEMO_SEQUENCE[i].mask);
        auto_delay_ms(DEMO_SEQUENCE[i].durationMs);
    }
    uart_send(0x00);   /* always end a full pass on all-clear */
}

static void run_manual_relay(void)
{
    if (uart_available()) {
        uart_send(uart_read());   /* pure passthrough: ATmega #1 -> ESP32, unchanged */
    }
}

int main(void)
{
    uart_init();
    mode_switch_init();

    while (1) {
        if (is_auto_mode()) {
            run_auto_demo();
        } else {
            run_manual_relay();
        }
    }
}
