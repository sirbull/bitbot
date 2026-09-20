# Hardware review — no final external wiring yet

The default commissioning build enables only the onboard BOOT button,
GPIO0, input with pull-up. RESET is CHIP_PU; native USB uses GPIO19/20.
The ST7789 driver is enabled by default since the display was verified on
hardware (2026-09-19).

**Verified on hardware, 2026-09-19:** the fitted module is an 8-pin ST7789,
240×240: **GND, VCC, SCL, SDA, RST, DC, CS, BL** (the 2026-09-10 note of a
7-pin module without CS was wrong). SCL/SDA here mean SPI clock/MOSI, not I2C. CS is on
D3/GPIO4; VCC and BL go to 3V3. The driver uses SPI mode 3. With CS floating
the panel stays dark, and with BL floating the backlight stays off. No SD card.

## Verified target facts

[Seeed getting started and pin tables](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
identify the original Sense board's 8 MB flash/8 MB PSRAM, GPIO0 BOOT,
GPIO21 user LED, and onboard PDM microphone (clock 42, data 41).
Camera allocation is fixed:

| Camera signal | GPIO |
| --- | --- |
| XCLK | 10 |
| SCCB SDA / SCL | 40 / 39 |
| D0, D1, D2, D3 | 15, 17, 18, 16 |
| D4, D5, D6, D7 | 14, 12, 11, 48 |
| VSYNC / HREF / PCLK | 38 / 47 / 13 |
| Sensor RESET / PWDN | Not exposed as separate GPIO in Seeed example |

The fitted sensor on this unit is an **OV3660** (identified on hardware
2026-09-19), not the OV2640 some revisions carry.

## Signal allocation, pending physical validation

[Seeed pin multiplexing](https://wiki.seeedstudio.com/xiao_esp32s3_pin_multiplexing/)
maps D0–D10 to 1,2,3,4,5,6,43,44,7,8,9. GPIO41/42 are not ADC inputs.
The Sense SD socket occupies 3/7/8/9. Keep it empty and disabled for this
prototype; do not share SPI with an active SD card. GPIO3 (also the empty SD slot's CS) takes an optional setup button to GND;
its strapping role only applies with the STRAP_JTAG_SEL eFuse burned. Do not cut microphone jumpers for this proposal.

| Function | Proposed pad / GPIO | Final wiring | Constraint |
| --- | --- | --- | --- |
| TFT SCL (SPI clock) | D8 / 7 | Driver allocation | Shared with unused SD clock |
| TFT SDA (MOSI) | D10 / 9 | Driver allocation | Shared with unused SD MOSI |
| TFT DC | D4 / 5 | Driver allocation | 3.3 V logic |
| TFT CS | D3 / 4 | Verified | |
| TFT RST | D1 / 2 | Driver allocation | Active-low reset; validate on module |
| TFT backlight (BL) | 3V3 | Verified | Always on; GPIO dimming would need a transistor |
| INMP441 + amplifier BCLK | D6 / 43 | TBD | Shared standard I2S clock; UART0 TX unavailable |
| INMP441 + amplifier WS | D7 / 44 | TBD | Shared standard I2S frame clock; UART0 RX unavailable |
| INMP441 SD → ESP RX | D9 / 8 | TBD | SD card absent; 3.3 V mic supply |
| ESP TX → MAX98357A DIN | D0 / 1 | TBD | Separate from microphone data |
| Setup button | Onboard BOOT / 0 | Verified onboard | Hold 3 s for setup, short press to talk |
| External setup button | D2 / 3, to GND | Optional | Internal pull-up; hold 3 s after boot, like BOOT |

The original eight-signal assumption exhausted the non-strapping header pins.
With CS on GPIO4 and BL on 3V3, GPIO6 (D5) is unused. There is no battery
voltage sensing. No
I/O expander, microphone jumper cut, or replacement microphone is assumed.
Microphone and speaker sharing BCLK/WS requires matching slot width and sample
rate; initially 16 kHz, stereo 32-bit slots, resampling TTS as necessary.
Independent 16 kHz receive/24 kHz transmit is incompatible with those shared
clocks. USB serial remains available on GPIO19/20.

## Electrical checks

- **ST7789:** ESP-IDF has an ST7789 SPI panel driver
  ([Espressif LCD documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/lcd/index.html)).
  Controller support does not verify the unknown module's regulator, backlight
  resistor, supply voltage, panel offset, or rotation. Obtain
  its exact product page/schematic before power wiring; all ESP signals are
  3.3 V logic. Do not drive an unknown backlight load directly from a GPIO.
- **INMP441:** [TDK datasheet](https://invensense.tdk.com/wp-content/uploads/2015/02/INMP441.pdf)
  specifies 1.8–3.3 V operation and 24-bit standard I2S data. Wire L/R deliberately
  for the chosen slot; verify breakout pin labels, decoupling, and grounding.
- **MAX98357A:** [Analog Devices](https://www.analog.com/en/products/max98357a.html)
  specifies 2.5–5.5 V supply, standard I2S, no MCLK, and differential speaker
  outputs. Neither speaker terminal is ground. Verify actual speaker impedance,
  rating, gain strap, and SD/shutdown circuit. Do not infer module wiring from
  the IC's voltage range alone.
- **Battery:** not measured by BitBot. The module's 4-LED gauge is the only
  charge indicator. No runtime is established.
- **Charging / 5 V supply (identified 2026-09-17):** the purchased module is a
  Type-C USB boost converter with a 1S Li-ion charger, protection and a 4-LED
  gauge, built on an IC marked **FM5324GA** — an IP5306 clone. Datasheet
  behaviour for the family: 4.2 V CC/CV charge, up to ~2 A charge current,
  5 V boost output rated 2 A, over-charge/over-discharge/short protection, and
  a key input for on/off that this board marks unusable. Pads: USB-C in,
  BAT+, BAT−, 5V OUT+, OUT−, K, and a charge-voltage trim point. BAT− and OUT−
  are one node, so the whole system shares a ground. Topology is boost, not
  load-sharing: the 5 V rail is generated from the cell, and the cell charges
  from USB-C, which is what makes charge-while-off work. Wiring follows from
  that and is drawn in [wiring.md](wiring.md): module BAT pads own the cell,
  OUT+ through the latching switch to the XIAO 5V pad and the amp's VIN, the
  XIAO's own BAT+/BAT− pads left empty.
  Two things must be measured on the bench before the cell is trusted:
  - **Charge current versus cell size.** A 350 mAh 10440 at this family's
    default charge current is roughly 6C — far outside the cell's rating. Meter
    the actual current into a cell first. If it is above ~0.35 A, use a larger
    cell (a 1000 mAh+ 18650 or an 18650-class pouch puts the module in its
    intended range) or a charger whose current can be set. Do not "try it and
    watch". The trim point adjusts charge voltage, not current.
  - **Boost auto-shutdown threshold.** IP5306-family parts drop the 5 V output
    when the load falls below roughly 45–100 mA for about 32 s. BitBot awake
    with the display on should stay above that; BitBot in deep sleep will not.
    With no usable K pad, recovery then depends on the board's auto-load
    detection, which has to be confirmed by experiment. If it does not recover,
    aggressive sleep is off the table for this module and the power budget has
    to be built around a light-sleep floor instead.
  Do not plug the XIAO's USB-C in while the switch is on: its 5V pad is VBUS
  with no blocking diode. Begin bring-up on the XIAO's USB with the module
  disconnected.

## Bring-up sequence

1. USB boot, chip/PSRAM check, BOOT hold, Wi-Fi/portal and recovery tests.
2. Verify module identities and resolve pin conflict; publish final wiring table.
3. ST7789 solid colors, rotation, readable `æ ø å Æ Ø Å`, blink/face demo.
4. Test microphone capture and amplifier output separately at low volume.
5. Camera one-frame capture and release; no network upload.
6. Power module alone: charge current into a cell, termination, 5 V under load,
   and the output's behaviour at ~30 mA. Decide the cell from that result.
7. Verified power path, then combined load/current test.
