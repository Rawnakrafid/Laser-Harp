# Wiring changes for the `esp32-polyphony` branch

This branch replaces the DFPlayer Mini with the ESP32's own built-in DAC,
so several beams can actually sound at the same time (real mixing). It
changes the **audio output** wiring only — the ATmega32 side (lasers,
LDRs, the UART link carrying the beam mask) is completely unchanged.

## What's removed

- DFPlayer Mini module — no longer used, can stay unplugged.
- Its SD card — no longer read at runtime (the note audio is now baked
  into the ESP32's own flash via `notes_data.h`).
- Whatever wiring existed between the DFPlayer and the ESP32 (its RX/TX
  lines, including the voltage divider on DFPlayer TX -> GPIO26) is now
  free/unused. You can leave it disconnected or repurpose those pins.
- The DFPlayer's speaker output (SPK_1/SPK_2) is gone too — the speaker
  now gets its signal from the new amp stage below.

## What's added: a small amplifier

The ESP32's DAC pins (GPIO25 and GPIO26) put out a *weak, low-voltage*
analog signal — enough for line-level input, but **not enough to drive a
speaker directly**. You need a small amplifier board between the ESP32
and the speaker. The standard cheap choice for this kind of project is a
**PAM8403** class-D amp module (~$1-2, breadboard-friendly, runs off
5V).

```
ESP32 GPIO25 or GPIO26  ---->  PAM8403 "L" or "R" input (either channel;
                                both DAC pins carry the identical mixed
                                signal, so it doesn't matter which you use)

ESP32 GND               ---->  PAM8403 GND

5V (from ESP32 5V pin,   ---->  PAM8403 VCC
 or a separate 5V supply
 sharing ground with ESP32)

PAM8403 speaker output (+/-)  ---->  Speaker
```

Notes:

- Use **one** DAC pin (GPIO25 *or* GPIO26) into **one** amp input channel.
  You don't need to wire both — they're identical, so wiring both into
  the amp's L and R inputs just gives you the same mono signal twice.
- The PAM8403 is a stereo amp with two independent channels; if you only
  feed one input, only that channel's speaker output will have sound —
  make sure you're wired to the matching output side.
- Keep the ESP32 and amp sharing a common ground, same as every other
  link in this project.
- If a PAM8403 isn't available, any small class-D or op-amp based audio
  amp module that accepts a ~0-3.3V analog input and runs off 5V works
  the same way — the ESP32 side of the wiring doesn't change.

## What's unchanged

- ATmega32 firmware (beam sensing) — identical to `main`, not touched by
  this branch.
- ATmega32 <-> ESP32 UART link (PD1 -> ESP32 GPIO16 through the existing
  voltage divider, PD0 <- ESP32 GPIO17 direct) — identical to `main`.
- Laser/LDR wiring, indicator LEDs on the ATmega32 — identical to `main`.

## Known limitation (by design, for now)

Holding a beam blocked does **not** sustain its note past the note's own
recorded length yet, and releasing a beam early does not cut its note
off early. That's a separate feature (discussed but not built on this
branch yet) — every other beam still mixes in normally on top of
whatever's already playing.
