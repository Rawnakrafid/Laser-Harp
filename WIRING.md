# Laser Harp — Wiring Reference (all 7 beams)

Current as of the DFPlayer Mini audio design, scaled to all 7 beams. Use
this to reverify every connection on the bench. Matches the code in
`atmega32-fw/src/main.c` and `esp32-fw/src/main.cpp`.

## Components in this build

- ATmega32(A), DIP-40
- 16MHz crystal + 2× 22pF ceramic caps
- 1× 100nF ceramic cap (AREF decoupling)
- Resistors: 10kΩ ×11, 220Ω ×7 (only these two values used, per what's on hand —
  7× 10kΩ for the LDR dividers, 1× 10kΩ reset pull-up, 3× 10kΩ for the UART
  divider; 7× 220Ω for the 7 indicator LEDs)
- 7× LDR (one per beam)
- 7× LED (one per beam, indicator)
- USBasp programmer
- ESP32 dev board (esp32doit-devkit-v1)
- DFPlayer Mini module + its own microSD card
- 1× small speaker (wired to the DFPlayer, not the ESP32)
- 5V supply for the ATmega32 side (separate from the ESP32's own USB power)

## Power rails

| # | Connection |
|---|---|
| A1 | 5V supply (+) → breadboard rail "5V-A" |
| A2 | 5V supply (–) → breadboard rail "GND" |
| A3 | ESP32 GND pin → same "GND" rail (ESP32 is powered separately via its own USB cable — only grounds are shared) |

"5V-A" and "GND" below refer to these two rails.

## ATmega32 core (required for the chip to run at all)

| # | Connection |
|---|---|
| B1 | ATmega32 pin 10 (VCC) → 5V-A |
| B2 | ATmega32 pin 30 (AVCC) → 5V-A |
| B3 | ATmega32 pin 11 (GND) → GND |
| B4 | ATmega32 pin 31 (GND) → GND |
| B5 | ATmega32 pin 32 (AREF) → 100nF cap → GND |
| B6 | ATmega32 pin 9 (RESET) → 10kΩ resistor → 5V-A (pull-up) |
| B7 | ATmega32 pin 12 (XTAL2) → crystal leg 1 |
| B8 | ATmega32 pin 13 (XTAL1) → crystal leg 2 |
| B9 | crystal leg 1 (same node as B7) → 22pF cap → GND |
| B10 | crystal leg 2 (same node as B8) → 22pF cap → GND |

Sanity check before going further: confirm Phase 0's fuse setting (external
16MHz crystal, not the factory 1MHz internal RC) is still in effect — all
the ADC/UART timing in the firmware assumes a real 16MHz clock.

## All 7 beam sensors (LDR dividers)

Same circuit repeated on each channel: 5V-A → LDR leg 1 → LDR leg 2 → node →
ATmega32 pin → node → 10kΩ resistor → GND.

| Beam | ATmega32 pin |
|---|---|
| 0 | pin 40 (PA0 / ADC0) |
| 1 | pin 39 (PA1 / ADC1) |
| 2 | pin 38 (PA2 / ADC2) |
| 3 | pin 37 (PA3 / ADC3) |
| 4 | pin 36 (PA4 / ADC4) |
| 5 | pin 35 (PA5 / ADC5) |
| 6 | pin 34 (PA6 / ADC6) |

## All 7 indicator LEDs

Same circuit repeated on each channel: ATmega32 pin → 220Ω resistor → LED
anode (long leg) → LED cathode (short leg) → GND.

| Beam | ATmega32 pin |
|---|---|
| 0 | pin 1 (PB0) |
| 1 | pin 2 (PB1) |
| 2 | pin 3 (PB2) |
| 3 | pin 4 (PB3) |
| 4 | pin 5 (PB4)* |
| 5 | pin 6 (PB5)* |
| 6 | pin 7 (PB6)* |

\* PB4–PB6 double as part of the ISP header (SS/MOSI/MISO). The LEDs are
fine there with the 220Ω in series, but if in-circuit reflashing ever gets
flaky, disconnect these three LEDs first and retry.

## ISP header (USBasp, for flashing the ATmega32)

| # | Connection |
|---|---|
| E1 | USBasp MISO → ATmega32 pin 7 (PB6) |
| E2 | USBasp MOSI → ATmega32 pin 6 (PB5) |
| E3 | USBasp SCK → ATmega32 pin 8 (PB7) |
| E4 | USBasp RESET → ATmega32 pin 9 (RESET, same node as B6) |
| E5 | USBasp VCC → 5V-A |
| E6 | USBasp GND → GND |

USBasp 10-pin header reference: pin 1 = MOSI, pin 2 = VCC, pin 5 = RESET,
pin 7 = SCK, pin 9 = MISO; all other even pins = GND. Check your board's
silkscreen labels first if it has them — more reliable than counting pins.

## ATmega32 ↔ ESP32 UART link

| # | Connection |
|---|---|
| F1 | ATmega32 pin 15 (PD1/TXD) → 10kΩ resistor → node "F" |
| F2 | node F → 10kΩ resistor → 10kΩ resistor (two in series) → GND |
| F3 | node F → ESP32 GPIO16 (RX2) |
| F4 | ESP32 GPIO17 (TX2) → ATmega32 pin 14 (PD0/RXD) — direct wire, no divider needed this direction |
| F5 | ESP32 GND → GND (same as A3 — confirms the shared ground is actually there) |

F1/F2 form a 10k : 20k divider (one 10kΩ on top, two 10kΩ in series on the
bottom) — drops the ATmega32's 5V TX signal to ~3.3V so it's safe for the
ESP32's non-5V-tolerant GPIO16. **Never wire PD1 straight to the ESP32.**

## ESP32 ↔ DFPlayer Mini

| # | Connection |
|---|---|
| G1 | ESP32 5V (or VIN) → DFPlayer VCC |
| G2 | ESP32 GND → DFPlayer GND |
| G3 | ESP32 GPIO27 → DFPlayer RX |
| G4 | ESP32 GPIO26 ← DFPlayer TX |
| G5 | DFPlayer SPK_1 → speaker terminal 1 |
| G6 | DFPlayer SPK_2 → speaker terminal 2 |

No divider needed on G3/G4 — the DFPlayer's RX tolerates 3.3V logic
directly from the ESP32. No SD module, DAC, or amp wiring needed on the
ESP32 itself anymore — the DFPlayer handles SD, decoding, and amplification
on its own.

## DFPlayer's own microSD card (not a wiring connection, but check it)

Card goes directly into the DFPlayer Mini's slot — separate from
everything above. Confirmed still using: a folder named exactly `MP3` at
the root, containing `0001_C4.mp3` … `0007_B4.mp3` (played via
`playMp3Folder(N)`, matching `esp32-fw/src/main.cpp`).

## Bring-up order

1. Build Section B + beam 0's LDR + LED + Section E only. Flash the
   ATmega32. Confirm the beam-0 LED responds to blocking that beam before
   wiring the rest.
2. Add the remaining 6 LDR/LED pairs one at a time, checking each one's
   LED responds correctly before moving to the next — much easier to spot
   a bad divider or a cold solder joint this way than wiring all 7 first.
3. Build Section G on the ESP32 side (DFPlayer + speaker), with the SD
   card loaded. Flash the ESP32. Confirm "DFPlayer Mini ready" over
   serial with nothing on Serial2 yet.
4. Add Section F (the UART link) last. Block each beam in turn and
   confirm the matching "Beam N blocked -> playing track..." line in the
   serial monitor, and that beam's note playing through the speaker.
