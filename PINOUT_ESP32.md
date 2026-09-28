# ESP32 Pin Configuration — Audio + Scale Switch + 16×2 LCD

This covers the ESP32 audio board only (the one running `esp32-fw`). It
doesn't touch the ATmega1 (beam sensing) or ATmega2 (mode/relay) pinouts —
see `PINOUT.md` and `PINOUT_LASER_CONTROLLER.md` for those.

## Pins already committed by existing firmware

| GPIO | Function | Notes |
|-----:|----------|-------|
| 25 | I2S DAC channel | Fixed by the ESP32's built-in DAC hardware — cannot be reassigned |
| 26 | I2S DAC channel | Fixed by the ESP32's built-in DAC hardware — cannot be reassigned |
| 16 | UART2 RX (`ATMEGA_RX_PIN`) | Receives the beam bitmask from ATmega1/ATmega2 |
| 17 | UART2 TX (`ATMEGA_TX_PIN`) | Currently unused by ATmega1, reserved for future use |

## New: scale-switch button

| GPIO | Function |
|-----:|----------|
| 18 | Scale-switch button (one leg to GPIO18, other leg to GND, internal pull-up enabled in firmware — no external resistor needed) |

Same debounced-edge pattern as your existing string-select button: on a
clean press, advance `currentScale` (C major → C# → Bb → … → wrap around)
and apply it as a fixed offset into whichever note table is active. This
doesn't require any change to the UART link or the ATmega side at all —
it's entirely local to the ESP32.

## New: 16×2 LCD (bare parallel HD44780, no I2C backpack)

Since you've got the bare 16-pin module, wiring is in 4-bit mode (the
standard choice — halves the pin count vs. 8-bit mode with zero downside)
and RW is tied straight to GND, since you only ever write to the display
and never read from it.

| LCD pin | Name | Goes to |
|--------:|------|---------|
| 1 | VSS | GND |
| 2 | VDD | +5V |
| 3 | V0 | Contrast pot wiper (see below) |
| 4 | RS | GPIO13 |
| 5 | RW | GND (tied low — write-only) |
| 6 | E | GPIO14 |
| 7 | D0 | Not connected (4-bit mode) |
| 8 | D1 | Not connected (4-bit mode) |
| 9 | D2 | Not connected (4-bit mode) |
| 10 | D3 | Not connected (4-bit mode) |
| 11 | D4 | GPIO27 |
| 12 | D5 | GPIO32 |
| 13 | D6 | GPIO33 |
| 14 | D7 | GPIO5 |
| 15 | LED+ | +5V through a ~220Ω resistor (backlight anode) |
| 16 | LED- | GND (backlight cathode) |

Pull +5V for the LCD from the ESP32 dev board's 5V/VIN pin (same rail your
USB power already brings in) — the logic pins (RS/E/D4-D7) are driven
straight from the ESP32's 3.3V GPIOs, which is the standard way people run
these bare HD44780 modules and works reliably in practice, even though the
LCD itself is powered at 5V.

### The contrast pot

This is the pot you're asking about, and it's the one every HD44780
tutorial shows — a **10kΩ potentiometer**, wired as a simple voltage
divider:

```
+5V ──[pot outer leg]
GND ──[pot other outer leg]
       pot wiper ──── LCD pin 3 (V0)
```

Turning it sweeps V0 between 0V and 5V, which is what controls whether the
characters are visible at all (too far one way = blank, too far the other
way = solid blocks). It's what people colloquially call the "brightness"
knob, even though technically it's contrast, not the backlight's actual
light output.

If you *also* want to dim the backlight's actual brightness (the LED+ /
LED- pair, i.e. the light behind the characters, separate from contrast),
swap the fixed 220Ω resistor on LED+ for a small pot (100Ω–1kΩ, wired as a
rheostat: one outer leg + wiper feeding LED+, other outer leg left
unconnected) — that's a second, optional pot, distinct from the contrast
one. Most builds skip this and just use the fixed resistor.

## Free GPIOs after this

5, 13, 14, 18, 27, 32, 33 are all spoken for above. Still free for later
use: 19, 21, 22, 23 (21/22 are the ESP32's conventional I2C pins, worth
keeping free in case you switch to an I2C backpack LCD down the line),
plus the ADC-only/input-only pins if you ever need an analog read.
