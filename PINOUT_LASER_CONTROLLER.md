# ATmega32 Pin Configuration — 7-Laser Light-Show Controller

This is a separate ATmega32 board from the beam-sensing one documented in
`PINOUT.md` — its job is driving 7 lasers on/off as a light show, with a
mode switch (MANUAL/AUTO) and a song-select switch (cycles through 4
pre-programmed sequences). It doesn't do beam sensing itself, so PORTA/the
ADC is unused here.

## Full DIP-40 pinout with this board's assignments

| Pin | Name | Alt. function | Assignment |
|----:|------|----------------|------------|
| 1 | PB0 | T0/XCK | Free |
| 2 | PB1 | T1 | Free |
| 3 | PB2 | INT2/AIN0 | Free |
| 4 | PB3 | OC0/AIN1 | Free |
| 5 | PB4 | SS (ISP) | Free * |
| 6 | PB5 | MOSI (ISP) | Free * |
| 7 | PB6 | MISO (ISP) | Free * |
| 8 | PB7 | SCK (ISP) | Free * |
| 9 | RESET | | Reset button / ISP header — 10k pull-up to VCC |
| 10 | VCC | | +5V |
| 11 | GND | | Ground |
| 12 | XTAL2 | | 16MHz crystal |
| 13 | XTAL1 | | 16MHz crystal |
| 14 | PD0 | RXD | Free (spare UART in, e.g. if this board ever needs to receive beam data) |
| 15 | PD1 | TXD | Free (spare UART out) |
| 16 | PD2 | INT0 | **Mode switch** — MANUAL / AUTO |
| 17 | PD3 | INT1 | **Song switch** — cycles through 4 songs |
| 18 | PD4 | OC1B | Free |
| 19 | PD5 | OC1A | Free |
| 20 | PD6 | ICP1 | Free |
| 21 | PD7 | OC2 | Free |
| 22 | PC0 | SCL | **Laser 1** → transistor base |
| 23 | PC1 | SDA | **Laser 2** → transistor base |
| 24 | PC2 | TCK (JTAG) | **Laser 3** → transistor base — see JTAG note |
| 25 | PC3 | TMS (JTAG) | **Laser 4** → transistor base — see JTAG note |
| 26 | PC4 | TDO (JTAG) | **Laser 5** → transistor base — see JTAG note |
| 27 | PC5 | TDI (JTAG) | **Laser 6** → transistor base — see JTAG note |
| 28 | PC6 | TOSC1 | **Laser 7** → transistor base |
| 29 | PC7 | TOSC2 | Free |
| 30 | AVCC | | +5V |
| 31 | GND | | Ground |
| 32 | AREF | | Not used (no ADC on this board) — leave floating or decouple with 100nF |
| 33 | PA7 | ADC7 | Free |
| 34 | PA6 | ADC6 | Free |
| 35 | PA5 | ADC5 | Free |
| 36 | PA4 | ADC4 | Free |
| 37 | PA3 | ADC3 | Free |
| 38 | PA2 | ADC2 | Free |
| 39 | PA1 | ADC1 | Free |
| 40 | PA0 | ADC0 | Free |

\* PB4–PB7 double as the ISP header (SS/MOSI/MISO/SCK). Nothing else is
wired to them here, so in-circuit reprogramming should be trouble-free.

## Per-laser driver circuit (repeat identically for all 7 channels)

```
+5V ──── LASER(+)
         LASER(-) ──── transistor collector
GPIO (PC0-PC6) ──[1kΩ]── transistor base
transistor emitter ──── GND
```

- Laser's positive lead goes straight to the +5V rail — not through the
  ATmega.
- Laser's negative lead goes to the transistor's collector.
- Transistor's emitter goes to GND.
- The GPIO pin drives the transistor's base through a 1kΩ resistor.

This way the GPIO only ever supplies the small base current needed to
switch the transistor; the laser's actual operating current comes from the
5V rail, not the microcontroller pin. A 2N2222 (or BC547/BC337) is plenty
for a typical small laser diode module's 20–40mA draw.

## Switches

Both wire the same simple way: one leg to the pin, the other leg to GND,
relying on the ATmega's internal pull-up (no external resistor needed).

- **Mode switch (PD2)** — a toggle switch. Open = HIGH = MANUAL. Closed to
  GND = LOW = AUTO.
- **Song switch (PD3)** — a momentary push button. Each debounced press
  advances `songIndex = (songIndex + 1) % 4`, cycling through the 4 songs.
  A single button is enough since it's just stepping through a fixed list.

## JTAG gotcha on PORTC (PC2–PC5)

Lasers 3–6 land on PC2–PC5, which double as the on-chip JTAG debug
interface. If the `JTAGEN` fuse is set (Atmel's factory default on some
parts), these pins will **not** behave as plain GPIO until you either:

- Clear the `JTD` bit in software at boot: `MCUCSR |= (1 << JTD); MCUCSR |= (1 << JTD);`
  — per the datasheet this must be written twice within 4 clock cycles, and
  it has to happen every boot (it doesn't persist across reset), or
- Permanently disable JTAG by clearing the `JTAGEN` fuse.

Same gotcha as the original beam-sensing board's `PINOUT.md` — worth fixing
once at the fuse level so it's not a recurring "why don't lasers 3–6 light
up" debugging session.

## Power / reference notes

- AVCC (pin 30) tied to VCC (5V) — a small LC/ferrite filter per the
  datasheet helps if you have the parts, though this board has no ADC
  activity to protect from noise.
- AREF (pin 32) can be left floating or decoupled with a ~100nF cap to GND;
  it's unused since this board doesn't read any analog inputs.
- Standard 22pF caps from XTAL1/XTAL2 to GND for the 16MHz crystal, and the
  external-crystal fuse setting (not the factory-default internal RC
  oscillator) — same as the other boards in this project, so all the
  `_delay_ms()`/UART timing math stays consistent across boards.
