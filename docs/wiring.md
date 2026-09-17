# Wiring — soldering guide (prototype)

Derived from the pin allocation in [hardware.md](hardware.md). Nothing here is
verified on a built unit yet. Check every breakout's printed labels before
soldering; they vary between sellers.

## XIAO ESP32-S3 Sense header (top view, USB-C up)

```
                         ┌───────[USB-C]───────┐
 AMP DIN ── D0 / GPIO1  ●│                     │●  5V ← module 5V OUT+
 TFT RES ── D1 / GPIO2  ●│                     │●  GND
  (free) ── D2 / GPIO3  ●│   XIAO ESP32-S3     │●  3V3
  (free) ── D3 / GPIO4  ●│      Sense          │●  D10 / GPIO9  ── TFT SDA
 TFT DC ─── D4 / GPIO5  ●│                     │●  D9  / GPIO8  ── MIC SD
 BAT ADC ── D5 / GPIO6  ●│                     │●  D8  / GPIO7  ── TFT SCK
 I2S BCLK ─ D6 / GPIO43 ●│                     │●  D7  / GPIO44 ── I2S WS
                         └─────────────────────┘
                           underside: BAT+  BAT-  (leave both unconnected)
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
 └──────────┘              └──── display GND, mic GND, amp GND, divider bottom

 Battery sense (cell voltage, not the 5 V rail)

 cell + ──┬── module BAT+
          │
        [100k]
          ├──────┬───── D5 / GPIO6   ≈ Vcell / 2
        [100k] [100nF]
          │      │
 cell − ──┴──────┴───── module BAT− (same node as OUT−, = GND)

## Notes

- **One ground.** All GND points join: module OUT−/BAT−, XIAO GND, display,
  mic, amp, divider bottom.
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
- **Divider** taps the cell, not the 5 V rail — a boosted rail says nothing
  about state of charge. It draws about 21 µA continuously, including while the
  switch is off, and then holds ~2 V on GPIO6 of an unpowered chip. Acceptable
  for a prototype; the module's own 4-LED gauge is the cross-check.
- **BLK**: most modules pull it up internally (backlight always on). If the
  backlight stays dark, tie it to 3V3. Never drive it from a GPIO without
  knowing its current.
- **Shared I2S clocks**: BCLK and WS go to both mic and amp — solder a short
  jumper from the mic's pin to the amp's pin, then one wire to the XIAO.
- **SD card slot** must stay empty (it shares GPIO7/8/9).
- Keep I2S and SPI wires short (< 10 cm).

## Bring-up order

Solder and test one block at a time, powered from the XIAO's own USB-C with
the switch off and the module disconnected:
display → mic → amp + speaker → battery divider.

Then, separately, bring up the power module on its own (cell + USB-C, no XIAO
attached): confirm charge current and termination, confirm the 5 V output under
a dummy load, and confirm what happens to that output at ~30 mA. Only after
that connect OUT+/OUT− to the XIAO, with the XIAO's USB-C unplugged.
