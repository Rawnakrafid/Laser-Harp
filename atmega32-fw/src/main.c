#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>

int main(void) {
    DDRB |= (1 << PB0);
    
    while(1) {
        PORTB |= (1 << PB0);   // LED on
        _delay_ms(500);         // 500ms at 16MHz
        PORTB &= ~(1 << PB0);  // LED off
        _delay_ms(500);         // 500ms at 16MHz
    }
}