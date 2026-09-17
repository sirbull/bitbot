# Component List — BitBot

Compiled from [BitBot_PROJECT_SPEC.md](../BitBot_PROJECT_SPEC.md) and
[hardware.md](hardware.md). Pins and electrical details are in hardware.md.
BitBot has no motors, servos or wheels.

| # | Component | Model / specification | Qty | Interface | Status |
| --- | --- | --- | --- | --- | --- |
| 1 | Microcontroller | Seeed Studio XIAO ESP32-S3 Sense (8 MB flash, 8 MB PSRAM, Wi-Fi, USB-C) | 1 | — | Confirmed |
| 2 | Camera | Included with XIAO Sense (OV2640 or OV3660, depending on revision) | 1 | DVP/SCCB | Sensor type must be identified |
| 3 | Built-in microphone | PDM microphone on the Sense board (GPIO 42/41) | 1 | PDM | Included, not planned for use |
| 4 | Display | 1.54" ST7789 TFT, 240×240, 7 pins (GND, VCC, SCK, SDA, RES, DC, BLK), no CS | 1 | SPI | Confirmed; power/backlight not verified |
| 5 | Microphone | INMP441 digital MEMS microphone, 1.8–3.3 V | 1 | I2S | Module pinout must be verified |
| 6 | Audio amplifier | MAX98357A I2S class D module, 2.5–5.5 V | 1 | I2S | Gain/SD connection must be verified |
| 7 | Speaker | Rectangular, approx. 15 × 11 × 3.5–4 mm | 1 | Differential output from MAX98357A | Impedance and power unknown |
| 8 | Battery | 1S Li-ion, protected. 10440 (approx. 350 mAh) is planned but likely too small for this module — see hardware.md | 1 | Module BAT+/BAT− | Cell choice under review |
| 9 | Charger/boost module | Type-C USB boost converter, 5 V 2 A step-up + 1S Li-ion charger/protection, 4-LED gauge. IC marked **FM5324GA** (IP5306 clone). Pads: USB-C in, BAT+, BAT−, 5V OUT+, OUT−, K (unusable), charge-voltage trim | 1 | 5 V rail | Purchased and identified; charge current and boost behaviour must be measured |
| 10 | Power switch | Self-locking on/off push button, 12 × 8 mm, rated 30 V / 1 A | 1 | In module 5V OUT+ line | Purchased |
| 11 | Resistors (battery sensing) | 100 kΩ, 1 % | 2 | Voltage divider to ADC1 (GPIO6) | Planned |
| 12 | Capacitor (filter) | approx. 100 nF, ADC node to GND | 1 | — | Optional |
| 13 | Setup button | Built-in BOOT button (GPIO0) | — | GPIO | Confirmed, part of XIAO |

## Not used

- The SD card reader on the Sense board (left empty; shares pins with the display SPI).
- Motors, motor drivers and servos.

## Open items

- Charger module: actual charge current into the cell, and whether the boost
  output starts by itself and stays on at BitBot's idle current. The K pad is
  labelled unusable on this board, so there is no button to restart the boost.
- The display module's voltage regulator and backlight current (possibly a transistor for BLK).
- The speaker's impedance and power rating.
- Which camera sensor is actually fitted.
