# ATmega32A Pin Configuration — Laser Harp

The ATmega32A is pin- and code-compatible with the ATmega32 (same DIP-40
package, same device signature `0x1E9502`), so everything below applies
directly and the existing `board = ATmega32` in `atmega32-fw/platformio.ini`
/ `avrdude -p atmega32` target do not need to change.

## Full DIP-40 pinout with this project's assignments

| Pin | Name | Alt. function | Assignment in this project |
|----:|------|----------------|------------------------------|
| 1 | PB0 | T0/XCK | Note-indicator LED — Beam 0 |
| 2 | PB1 | T1 | Note-indicator LED — Beam 1 |
| 3 | PB2 | INT2/AIN0 | Note-indicator LED — Beam 2 |
| 4 | PB3 | OC0/AIN1 | Note-indicator LED — Beam 3 |
| 5 | PB4 | SS (ISP) | Note-indicator LED — Beam 4 * |
| 6 | PB5 | MOSI (ISP) | Note-indicator LED — Beam 5 * |
| 7 | PB6 | MISO (ISP) | Note-indicator LED — Beam 6 * |
| 8 | PB7 | SCK (ISP) | Note-indicator LED — Beam 7 * |
| 9 | RESET | | Reset button / ISP header |
| 10 | VCC | | +5V |
| 11 | GND | | Ground |
| 12 | XTAL2 | | 16MHz crystal |
| 13 | XTAL1 | | 16MHz crystal |
| 14 | PD0 | RXD | UART RX from ESP32 (optional, see note) |
| 15 | PD1 | TXD | UART TX to ESP32 |
| 16 | PD2 | INT0 | Free |
| 17 | PD3 | INT1 | Free |
| 18 | PD4 | OC1B | Free |
| 19 | PD5 | **OC1A** | **Audio out** → buzzer/speaker (Timer1 CTC) |
| 20 | PD6 | ICP1 | Free |
| 21 | PD7 | OC2 | Free |
| 22 | PC0 | SCL | Free (I2C if ever needed) |
| 23 | PC1 | SDA | Free (I2C if ever needed) |
| 24 | PC2 | TCK (JTAG) | Avoid — see JTAG note |
| 25 | PC3 | TMS (JTAG) | Avoid — see JTAG note |
| 26 | PC4 | TDO (JTAG) | Avoid — see JTAG note |
| 27 | PC5 | TDI (JTAG) | Avoid — see JTAG note |
| 28 | PC6 | TOSC1 | Free |
| 29 | PC7 | TOSC2 | Free |
| 30 | AVCC | | +5V (through LC filter if available) |
| 31 | GND | | Ground |
| 32 | AREF | | Decouple with ~100nF to GND (using AVCC as ADC reference) |
| 33 | PA7 | ADC7 | Photosensor — Beam 7 |
| 34 | PA6 | ADC6 | Photosensor — Beam 6 |
| 35 | PA5 | ADC5 | Photosensor — Beam 5 |
| 36 | PA4 | ADC4 | Photosensor — Beam 4 |
| 37 | PA3 | ADC3 | Photosensor — Beam 3 |
| 38 | PA2 | ADC2 | Photosensor — Beam 2 |
| 39 | PA1 | ADC1 | Photosensor — Beam 1 |
| 40 | PA0 | ADC0 | Photosensor — Beam 0 |

\* PB4–PB7 double as the ISP header (SS/MOSI/MISO/SCK). LEDs there are fine
electrically (use ≥330Ω series resistors), but if in-circuit reprogramming
ever fails with them attached, disconnect the LEDs for that one flash — a
loaded MISO/SCK line is a classic cause of flaky ISP uploads.

## Why each group landed where it did

**PORTA → all 8 photosensors (ADC0–ADC7).** PORTA is the only port with
built-in ADC muxing on this chip, so it's the only sane place for up to 8
analog beam-sensor channels — no external multiplexer needed for the
proposal's 6–8 beam range.

**PD5/OC1A → audio.** This is Timer1's compare-match output pin, fixed by
silicon — not a choice, a requirement. CTC mode toggling OC1A gives an
exact-frequency square wave for the piezo/speaker.

**PD0/PD1 → UART to ESP32.** Fixed USART pins. Per the project's UART frame
design (1 byte, 1 bit per beam, ATmega32 → ESP32 only), you technically only
need TX (PD1) wired to the ESP32's RX; wire RX (PD0) too if you ever want
config/ack bytes coming back.

**PORTB → the 8 note-indicator LEDs.** Chosen over PORTC specifically to
sidestep the JTAG conflict below — PORTB is otherwise fully free once you're
past initial ISP flashing, and 8 pins maps one-to-one with 8 beams.

## JTAG gotcha on PORTC (PC2–PC5)

If the JTAGEN fuse is set (Atmel's factory default on some parts), PC2–PC5
are claimed by the on-chip JTAG debug interface and will **not** behave as
plain GPIO until you either:

- Clear the `JTD` bit in software: `MCUCSR |= (1 << JTD); MCUCSR |= (1 << JTD);`
  — must be written twice within 4 clock cycles per the datasheet, and this
  has to happen every boot (it doesn't persist across reset), or
- Permanently disable JTAG by clearing the `JTAGEN` fuse.

This is why the LEDs are routed to PORTB above instead — one less thing to
debug three weeks from now when "half the LEDs don't light."

## Power / reference notes

- AVCC (pin 30) should be tied to VCC (5V), ideally through a small LC/ferrite
  filter per the datasheet if you have the parts, to keep ADC noise down.
- AREF (pin 32) gets a ~100nF decoupling cap to GND when using AVCC as the
  ADC voltage reference (the usual choice — no need for an external
  reference for this project).
- Add the standard 22pF caps from XTAL1/XTAL2 to GND for the 16MHz crystal,
  and don't forget the fuse setting from Phase 0 (external crystal, not the
  factory-default internal RC oscillator) or none of the ADC/Timer1 timing
  math here will hold.
