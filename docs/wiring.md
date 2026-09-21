# Wiring — soldering guide (prototype)

Derived from the pin allocation in [hardware.md](hardware.md). Nothing here is
verified on a built unit yet. Check every breakout's printed labels before
soldering; they vary between sellers.

## XIAO ESP32-S3 Sense header (top view, USB-C up)

```
                         ┌───────[USB-C]───────┐
 AMP DIN ── D0 / GPIO1  ●│                     │●  5V ← module 5V OUT+
 TFT RST ── D1 / GPIO2  ●│                     │●  GND
  SETUP ─── D2 / GPIO3  ●│   XIAO ESP32-S3     │●  3V3
  TFT CS ── D3 / GPIO4  ●│      Sense          │●  D10 / GPIO9  ── TFT SDA
 TFT DC ─── D4 / GPIO5  ●│                     │●  D9  / GPIO8  ── MIC SD
 I2S BCLK ─ D5 / GPIO6  ●│                     │●  D8  / GPIO7  ── TFT SCL
 (dead) ─── D6 / GPIO43 ●│                     │●  D7  / GPIO44 ── I2S WS
                         └─────────────────────┘
                           underside: BAT+  BAT-  (leave both unconnected)
```

**BCLK moved from D6/GPIO43 to D5/GPIO6.** GPIO43 cannot hold up as a fast I2S
clock (256 kHz - 1.4 MHz) on this board - confirmed by moving only the BCLK
wire to a spare pin and getting a clean signal on an otherwise unchanged
circuit (two different amp chips both misbehaved identically on GPIO43, which
rules out the amp). GPIO6 was free because battery sensing was never wired.

D6/GPIO43 is still usable for slow, occasional digital I/O: toggled at 1 Hz as
a plain GPIO output, it swings cleanly between 0 V and 3.3 V. It is the only
spare pin left on the D0-D10 header, so it is worth keeping for something like
a reset line or a button - just not for another fast clock or SPI/PWM bus,
which have not been tested and may hit the same limit.

D2/GPIO3 takes an optional setup button to GND (same as holding BOOT 3 s after
boot). GPIO3 is a strapping pin, but it only selects the JTAG source when the
STRAP_JTAG_SEL eFuse is burned, which it is not by default, so a button is safe.

## Full schematic

```
 ST7789 240×240, 8 pins         XIAO
 ┌─────┐
 │ GND ├──────────────────────── GND
 │ VCC ├──────────────────────── 3V3
 │ SCL ├──────────────────────── D8  / GPIO7   (SPI clock, not I2C)
 │ SDA ├──────────────────────── D10 / GPIO9   (SPI MOSI, not I2C)
 │ RST ├──────────────────────── D1  / GPIO2
 │ DC  ├──────────────────────── D4  / GPIO5
 │ CS  ├──────────────────────── D3  / GPIO4
 │ BL  ├──────────────────────── 3V3           (must not float)
 └─────┘

 INMP441 mic                     XIAO
 ┌─────┐
 │ VDD ├──────────────────────── 3V3
 │ GND ├──────────────────────── GND
 │ L/R ├──────────────────────── GND           (left slot)
 │ SCK ├──────────────────────── D5  / GPIO6   (I2S BCLK - moved off D6/GPIO43,
 │     │                              which is faulty on this board)
 │ WS  ├──────────────────────── D7  / GPIO44  (I2S WS)
 │ SD  ├──────────────────────── D9  / GPIO8   (mic data)
 └─────┘

 MAX98357A amp                   XIAO
 ┌──────┐
 │ BCLK ├──────────────────────── D5  / GPIO6   (same net as mic SCK; D6/GPIO43
 │      │                              is faulty on this board, moved off it)
 │ LRC  ├──────────────────────── D7  / GPIO44  (same net as mic WS)
 │ DIN  ├──────────────────────── D0  / GPIO1   (speaker data)
 │ GAIN ├── open                 (9 dB; floating is a real default per the datasheet)
 │ SD   ├──────────────────────── 3V3  (datasheet Table 5 has no floating state for
 │      │                              SD_MODE, only High/pullup/pullup/Low - leaving
 │      │                              it open drifts on breadboard noise from the
 │      │                              adjacent BCLK line. Tying to 3V3 = Left channel,
 │      │                              which sounds identical to L+R mix here since the
 │      │                              firmware duplicates the same sample into both.)
 │ GND  ├──────────────────────── GND
 │ VIN  ├──────────────────────── 5V rail after switch (see below)
 │ OUT+ ├──────── speaker +
 │ OUT- ├──────── speaker −      (neither side to GND!)
 └──────┘

 Power: FM5324GA / IP5306-clone module (USB-C in, 5 V 2 A boost out)

 ┌──────────┐
 │  USB-C   ├── BitBot's charging port (the only one used in normal operation)
 │  BAT +   ├──── cell +      the module owns the cell, nothing else does
 │  BAT −   ├──── cell −
 │   K      ├──── not connected (pad is labelled unusable on this board)
 │          │
 │  OUT +   ├──[ switch ]──┬──── XIAO 5V pad  (→ onboard 3V3 LDO)
 │          │              └──── MAX98357A VIN
 │          │
 │  OUT −   ├──────────────┬──── XIAO GND
 └──────────┘              └──── display GND, mic GND, amp GND
```

## Notes

- **One ground.** All GND points join: module OUT−/BAT−, XIAO GND, display,
  mic, amp.
- **The module is the only charger.** The XIAO's BAT+/BAT− pads stay empty.
  Two chargers on one cell is how cells vent. The cell goes to the module's
  BAT pads and nowhere else.
- **Switch sits in OUT+**, between the module and everything else. That keeps
  the spec's "charge while switched off" behaviour: USB-C charging works with
  the switch in either position.
- **Never plug the XIAO's USB-C in with the switch on.** The XIAO's 5V pad is
  VBUS with no blocking diode, so USB and the module's boost would fight.
  Switch off first, then flash. Rule of thumb: one USB-C at a time.
- **Amp on 5V, not 3V3 or the cell.** The MAX98357A takes 2.5–5.5 V, and the
  module supplies 2 A, so speaker peaks no longer sag the XIAO's regulator.
- **Boost auto-shutdown.** IP5306-family parts cut the 5 V output when the load
  draws under roughly 45–100 mA for about half a minute, and there is no usable
  K pad here to switch it back on. Measure this before relying on deep sleep;
  see the charging notes in [hardware.md](hardware.md).
- **No battery sensing.** BitBot does not measure the cell; the module's 4-LED
  gauge is the only charge indicator. D5/GPIO6 now carries I2S BCLK instead
  (moved there after GPIO43 was found faulty), so it is no longer free.
- **BL** to 3V3: on the fitted module a floating BL leaves the backlight off.
  Never drive it from a GPIO without knowing its current.
- **Shared I2S clocks**: BCLK and WS go to both mic and amp — solder a short
  jumper from the mic's pin to the amp's pin, then one wire to the XIAO.
- **BCLK is on D5/GPIO6, not D6/GPIO43.** GPIO43 is dead on this board (see
  above); nothing should be wired to D6.
- **SD card slot** must stay empty (it shares GPIO7/8/9).
- Keep I2S and SPI wires short (< 10 cm).

## Bring-up order

Solder and test one block at a time, powered from the XIAO's own USB-C with
the switch off and the module disconnected:
display → mic → amp + speaker.

Then, separately, bring up the power module on its own (cell + USB-C, no XIAO
attached): confirm charge current and termination, confirm the 5 V output under
a dummy load, and confirm what happens to that output at ~30 mA. Only after
that connect OUT+/OUT− to the XIAO, with the XIAO's USB-C unplugged.
