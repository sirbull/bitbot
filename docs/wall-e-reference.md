# Additional reference: Huy Vector's Wall-E AI

Inspected after the owner supplied the links. These are observations from
publicly served files, not a recovered source tree or a licence to reuse its
character artwork/firmware.

- [Project page](https://www.huyvector.org/robots-kinetic/wall-e-ai) lists XIAO
  ESP32-S3 Sense with OV3660, a 1.54-inch display, amplifier and motors. A wiring
  diagram is linked, but its Google-hosted image returned HTTP 403 during this
  inspection. It has not been used as verified pin evidence.
- [Web installer](https://huyvector.github.io/wall-e-ai-installer/) uses
  `esp-web-tools@10`, browser USB/serial, and version 2.4.0. Its wording confirms
  that installation clears saved Wi-Fi. Browser installation and Wi-Fi setup
  are different operations.
- [Public manifest](https://huyvector.github.io/wall-e-ai-installer/manifest.json)
  targets `ESP32-S3`, flashes one factory image at offset 0, requests an erase
  prompt, and sets `improv: false`. It does not describe display pins.
- Read-only inspection of strings in the publicly downloadable factory image
  found `esp_lcd_new_panel_st7789`, `lcd_panel.st7789`, `xiaozhi`,
  `v5.5.2-dirty`, OV3660 driver names and `wn9_hiwalle_tts2`. These support the
  inference that it uses an ESP-IDF/XiaoZhi-based ST7789 build with a custom
  WakeNet model. Binary strings alone do not establish the source revision,
  complete GPIO map, schematic, licence compliance, or successful operation.

The [installer repository](https://github.com/huyvector/wall-e-ai-installer)
contains the site, images, manifest and binary. No firmware source or explicit
licence was present in the inspected file tree. Nothing from those files is
redistributed in BitBot. The useful architectural lessons are browser-assisted
firmware installation, ST7789 compatibility, and a real trained wake word.
BitBot has no motors and uses its own face design.

The owner subsequently confirmed the actual display as **ST7789 240×240** with
**GND, VCC, SCK, SDA, RES, DC, BLK**, in that order. This owner-provided pin list,
together with Seeed's MCU pin map and Espressif's LCD driver, is the basis of
BitBot's no-CS display implementation; the opaque factory binary is not.
