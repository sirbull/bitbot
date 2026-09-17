# Wiring — soldering guide (prototype)

Derived from the pin allocation in [hardware.md](hardware.md). Nothing here is
verified on a built unit yet. Check every breakout's printed labels before
soldering; they vary between sellers.

## XIAO ESP32-S3 Sense header (top view, USB-C up)

```
                         ┌───────[USB-C]───────┐
 AMP DIN ── D0 / GPIO1  ●│                     │●  5V (VBUS, USB only)
 TFT RES ── D1 / GPIO2  ●│                     │●  GND
  (free) ── D2 / GPIO3  ●│   XIAO ESP32-S3     │●  3V3
  (free) ── D3 / GPIO4  ●│      Sense          │●  D10 / GPIO9  ── TFT SDA
 TFT DC ─── D4 / GPIO5  ●│                     │●  D9  / GPIO8  ── MIC SD
 BAT ADC ── D5 / GPIO6  ●│                     │●  D8  / GPIO7  ── TFT SCK
 I2S BCLK ─ D6 / GPIO43 ●│                     │●  D7  / GPIO44 ── I2S WS
                         └─────────────────────┘
                           underside: BAT+  BAT-
```

D2/GPIO3 is a strapping pin: leave it unconnected. D3/GPIO4 is free
(reserved for future backlight control).

## Full schematic

```
 ST7789 240×240 (no CS)          XIAO
 ┌─────┐
 │ GND ├──────────────────────── GND
 │ VCC ├──────────────────────── 3V3
 │ SCK ├──────────────────────── D8  / GPIO7
 │ SDA ├──────────────────────── D10 / GPIO9   (SPI MOSI, not I2C)
 │ RES ├──────────────────────── D1  / GPIO2
 │ DC  ├──────────────────────── D4  / GPIO5
 │ BLK ├── open, or to 3V3
 └─────┘

 INMP441 mic                     XIAO
 ┌─────┐
 │ VDD ├──────────────────────── 3V3
 │ GND ├──────────────────────── GND
 │ L/R ├──────────────────────── GND           (left slot)
 │ SCK ├──────────────────────── D6  / GPIO43  (I2S BCLK)
 │ WS  ├──────────────────────── D7  / GPIO44  (I2S WS)
 │ SD  ├──────────────────────── D9  / GPIO8   (mic data)
 └─────┘

 MAX98357A amp                   XIAO
 ┌──────┐
 │ BCLK ├──────────────────────── D6  / GPIO43  (same net as mic SCK)
 │ LRC  ├──────────────────────── D7  / GPIO44  (same net as mic WS)
 │ DIN  ├──────────────────────── D0  / GPIO1   (speaker data)
 │ GAIN ├── open                 (9 dB)
 │ SD   ├── open                 (on, L+R mix)
 │ GND  ├──────────────────────── GND
 │ VIN  ├──────────────────────── BAT+ after switch (see below)
 │ OUT+ ├──────── speaker +
 │ OUT- ├──────── speaker −      (neither side to GND!)
 └──────┘

 Battery (1S Li-ion, protected cell)

 cell + ──[ switch ]──┬──────────── XIAO BAT+ pad
                      ├──────────── MAX98357A VIN
                      │
                     [100k]
                      ├──────┬───── D5 (GPIO6)  ≈ Vbat / 2
                     [100k] [100nF]
                      │      │
 cell − ──────────────┴──────┴───── XIAO BAT− pad  (= GND)
```

## Notes

- **One ground.** All GND points join: XIAO GND, display, mic, amp, BAT−, divider.
- **Amp on battery, not 3V3.** Speaker peaks can exceed what the XIAO's 3V3
  regulator delivers. 5V is only live on USB, so battery voltage (3.0–4.2 V)
  is the simplest supply that works both ways.
- **Charging** uses the XIAO's onboard charger via the BAT pads. The switch
  sits before the pads, so USB charging only works with the switch on. Don't
  add a second charger module without reading [hardware.md](hardware.md).
- **Divider** draws about 21 µA constantly. Acceptable for a prototype.
- **BLK**: most modules pull it up internally (backlight always on). If the
  backlight stays dark, tie it to 3V3. Never drive it from a GPIO without
  knowing its current.
- **Shared I2S clocks**: BCLK and WS go to both mic and amp — solder a short
  jumper from the mic's pin to the amp's pin, then one wire to the XIAO.
- **SD card slot** must stay empty (it shares GPIO7/8/9).
- Keep I2S and SPI wires short (< 10 cm).

## Bring-up order

Solder and test one block at a time, USB-powered, battery disconnected:
display → mic → amp + speaker → battery divider → battery.
