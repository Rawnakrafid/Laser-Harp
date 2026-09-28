# Laser Harp — Optical Laser Controller (ATmega32 #2) Wiring Reference

This controller operates as an **optical beam sequencer**: it plays notes by turning specific laser beams **OFF** (which makes the LDR sensors go dark, triggering notes on ATmega #1 exactly like a human hand).

---

## 1. Core Hardware & Clock Setup

> [!NOTE]
> **No External Crystal Oscillator is needed for this chip.**
> This ATmega32 runs on its **internal 1.0 MHz RC oscillator** (verified: `lfuse = 0xe1`, `hfuse = 0x99`).

| Pin | Label | Connection |
|:---:|:---:|:---|
| **10** | VCC | +5V Rail |
| **30** | AVCC | +5V Rail |
| **11, 31** | GND | Common Ground |
| **32** | AREF | 100nF capacitor to GND |
| **9** | RESET | 10kΩ pull-up resistor to +5V (also connects to USBasp RESET) |
| **6, 7, 8**| MOSI, MISO, SCK | USBasp programming header (pins PB5, PB6, PB7) |
| **12, 13** | XTAL2, XTAL1 | **LEFT UNCONNECTED** (No crystal needed) |

---

## 2. Pushbuttons Wiring

Both pushbuttons use the ATmega32's **internal pull-up resistors** (no external resistors needed):

* **Mode Button (PD2 / pin 16)**:
  * Connect one terminal to **pin 16 (PD2)**.
  * Connect the other terminal to **GND**.
  * *Pressing toggles between **MANUAL MODE** and **AUTO MODE**.*
* **Song Button (PD3 / pin 17)**:
  * Connect one terminal to **pin 17 (PD3)**.
  * Connect the other terminal to **GND**.
  * *Pressing in Auto Mode advances to the next track in the playlist.*

---

## 3. NPN Transistor & Laser Module Wiring (CRITICAL!)

For each laser beam (Pins 22 to 28):

```
+5V Breadboard Rail --------------------------> Laser Module "S" / VCC Pin
                                                Laser Module "-" Pin
                                                         |
                                                         v
ATmega32 Pin (22 to 28) ---> [ 420Ω Resistor ] -------> BASE (NPN Transistor)
                                                         |
                                                    COLLECTOR (Connected to Laser "-" !)
                                                         |
                                                      EMITTER
                                                         |
                                                         v
Common GND ---------------------------------------------> GND Rail
```

> [!IMPORTANT]
> **Collector must connect to the Laser Module `-` pin!**
> Do NOT connect the Collector to Ground.
> When the ATmega pin goes HIGH, the transistor turns on and connects the Laser's `-` pin to the Emitter (GND), turning the laser ON.

---

## 4. Laser Beams Output Mapping (Pins 1 to 7 = PB0 to PB6)

* **Signal Logic**:
  * **HIGH (5V)** = Laser **ON** (emitting light $\rightarrow$ beam unblocked / silence).
  * **LOW (0V)**  = Laser **OFF** (beam dark $\rightarrow$ triggers note on ATmega #1!).

| Beam | Musical Note | ATmega32 #2 Pin | Function |
|:---:|:---:|:---|:---|
| **Beam 0** | **C4** | **Pin 1 (PB0)** | Connects to Laser 0 `S` pin |
| **Beam 1** | **D4** | **Pin 2 (PB1)** | Connects to Laser 1 `S` pin |
| **Beam 2** | **E4** | **Pin 3 (PB2)** | Connects to Laser 2 `S` pin |
| **Beam 3** | **F4** | **Pin 4 (PB3)** | Connects to Laser 3 `S` pin |
| **Beam 4** | **G4** | **Pin 5 (PB4)** | Connects to Laser 4 `S` pin |
| **Beam 5** | **A4** | **Pin 6 (PB5)** | Connects to Laser 5 `S` pin |
| **Beam 6** | **B4** | **Pin 7 (PB6)** | Connects to Laser 6 `S` pin |

*(Note: Pins 1 to 7 are located consecutively in a clean row down the top-left of the chip, starting from the notch).*
