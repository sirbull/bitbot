# Hardware review — no final external wiring yet

The default commissioning build enables only the onboard BOOT button,
GPIO0, input with pull-up. RESET is CHIP_PU; native USB uses GPIO19/20.
An optional ST7789 bring-up driver is included and enabled through menuconfig
after the display's supply/backlight wiring is verified.

**Owner confirmation, 2026-09-10:** ST7789, 240×240, seven pins. With the pins
at the top, left to right: **GND, VCC, SCK, SDA, RES, DC, BLK**. There is **no CS**.
SDA here means SPI MOSI, not I2C. The driver uses `cs_gpio_num = GPIO_NUM_NC`
(-1) and the display must have exclusive use of its SPI bus. No SD card.

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

Do not assume the bundled camera is always OV2640: revisions also use OV3660.
Identify the fitted sensor and use the camera driver's identification.

## Signal allocation, pending physical validation

[Seeed pin multiplexing](https://wiki.seeedstudio.com/xiao_esp32s3_pin_multiplexing/)
maps D0–D10 to 1,2,3,4,5,6,43,44,7,8,9. GPIO41/42 are not ADC inputs.
The Sense SD socket occupies 3/7/8/9. Keep it empty and disabled for this
prototype; do not share SPI with an active SD card. GPIO3 is a strapping
pin and remains reserved. Do not cut microphone jumpers for this proposal.

| Function | Proposed pad / GPIO | Final wiring | Constraint |
| --- | --- | --- | --- |
| TFT SCK | D8 / 7 | Driver allocation | Shared with unused SD clock |
| TFT SDA (MOSI) | D10 / 9 | Driver allocation | Shared with unused SD MOSI |
| TFT DC | D4 / 5 | Driver allocation | 3.3 V logic |
| TFT CS | Not present / -1 | Confirmed absent | Display owns the SPI bus |
| TFT RES | D1 / 2 | Driver allocation | Active-low reset; validate on module |
| TFT backlight control | D3 / 4 | TBD | Transistor/driver if current exceeds GPIO limits |
| INMP441 + amplifier BCLK | D6 / 43 | TBD | Shared standard I2S clock; UART0 TX unavailable |
| INMP441 + amplifier WS | D7 / 44 | TBD | Shared standard I2S frame clock; UART0 RX unavailable |
| INMP441 SD → ESP RX | D9 / 8 | TBD | SD card absent; 3.3 V mic supply |
| ESP TX → MAX98357A DIN | D0 / 1 | TBD | Separate from microphone data |
| Battery divider ADC | D5 / 6, ADC1 channel 5 | TBD | Available because this display has no CS |
| Setup button | Onboard BOOT / 0 | Verified onboard | Press after boot, not during reset |

The original eight-signal assumption exhausted the non-strapping header pins.
The confirmed seven-pin module resolves that allocation conflict by freeing
GPIO6 for ADC1. Battery and backlight wiring still require verification. No
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
- **Battery:** proposed 100k/100k divider gives 2.1 V at the ADC for 4.2 V cell
  voltage; add filtering and calibrate the ADC. Power wiring remains TBD. Use a
  measured Li-ion curve, not a linear percentage. No runtime is established.
- **Charging:** identify the purchased USB-C module, charge current, protection,
  output voltage, boost/load-sharing topology, and cell charge rating. Do not
  combine two chargers or feed unknown output into 3V3/5V/BAT pads. Charging
  while switched off depends on those facts. Begin bring-up on USB with the
  external battery/power module disconnected.

## Bring-up sequence

1. USB boot, chip/PSRAM check, BOOT hold, Wi-Fi/portal and recovery tests.
2. Verify module identities and resolve pin conflict; publish final wiring table.
3. ST7789 solid colors, rotation, readable `æ ø å Æ Ø Å`, blink/face demo.
4. Test microphone capture and amplifier output separately at low volume.
5. Camera one-frame capture and release; no network upload.
6. Battery ADC calibration, verified power path, then combined load/current test.
