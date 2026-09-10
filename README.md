# BitBot

A small portable AI companion for the **Seeed XIAO ESP32-S3 Sense**, with a
**7-pin ST7789 240×240** display, camera, microphone, and speaker. Norwegian
and English are the default pair, with an international BCP-47 language
preference for open-source provider and speech adapters.

**Current milestone: commissioning and hardware bring-up.** The English setup
portal, Wi-Fi profiles, settings storage, and optional ST7789 test renderer
are implemented. Voice conversation, wake word, camera, battery measurement,
and real power optimization are still to come. No paid AI calls are made.

## Continue development at home

Install Node.js 22 or later, then in this repository:

```sh
git pull --ff-only
npm ci
npm run dev
```

Open **http://127.0.0.1:4173**. In Windows PowerShell, use `npm.cmd` if script
execution policy blocks `npm.ps1`.

The desktop simulator saves its settings in ignored `.local/simulator.json`.
Use **dummy credentials**. It does not connect your computer to Wi-Fi or call
an AI service. Use the password `wrong-password` to exercise a failed network
test; other valid demo passwords simulate success. A new network is saved only
after success. Restarting the server preserves saved preferences.

## Build the portal and run checks

```sh
npm ci
npx playwright install chromium
npm run check
```

The build generates compressed C++ assets under `firmware/main/generated/`.
Commit these generated files when changing `web/` or the settings schema.
The portal is approximately 14 KiB compressed and uses no external scripts,
images, fonts, or CDN. Node is a development tool, not a requirement on the ESP.

Checks cover secret redaction, provider-specific keys, storage failures,
invalid inputs, network limits, failed network changes, concurrent saves,
HTTP access controls, and browser setup on mobile and desktop. GitHub Actions
also compiles the ESP32-S3 firmware and uploads build artifacts.

## Build and flash the ESP32-S3

Use an **ESP-IDF 6.0.2** development shell. The local workstation used for
development did not have ESP-IDF installed; firmware compilation runs in CI.

```sh
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

Replace `COM5` with the board's serial port (for example `/dev/ttyACM0` on Linux).
Only flash the actual XIAO ESP32-S3 Sense. Do not use `erase-flash` for ordinary
updates: it removes saved settings. This commissioning partition layout is new;
flashing over another project's firmware can require a deliberate migration.

**Start with USB power and no unverified external power wiring.** On first boot,
the firmware opens `BitBot-XXXX`. Its generated setup password is shown through
the physical USB serial console in this bring-up version. Join that hotspot
and open **http://192.168.4.1**. Stay connected if your phone says "no internet".
Add a 2.4 GHz personal/open network, save preferences, and choose **Finish setup**.

At a new location, hold onboard **BOOT for three seconds after startup**. This
reopens setup without erasing saved Wi-Fi, personality, or keys. RESET just
reboots; BOOT held during reset enters the firmware download mode. After initial
setup, losing Wi-Fi does not automatically open the hotspot. Reconfiguration
closes after ten minutes; first-boot setup stays available until a network is
saved. Up to five profiles are remembered.

The portal only checks Wi-Fi association and DHCP, **not internet or provider
availability**. Enterprise authentication and hotel web-login networks are not
supported. A 2.4 GHz phone hotspot is useful for portable demos.

## The confirmed display

Owner-confirmed pins, left to right with the pins at the top:

| Display | XIAO signal allocation |
| --- | --- |
| GND | GND |
| VCC | Verify module supply requirements before connection |
| SCK | D8 / GPIO7 |
| SDA (SPI MOSI) | D10 / GPIO9 |
| RES | D1 / GPIO2 |
| DC | D4 / GPIO5 |
| BLK | Verify backlight circuit/current; not driven directly by firmware |

There is **no CS pin**. The included driver uses ESP-IDF's `esp_lcd` ST7789
driver with CS disabled, RGB565, and conservative 10 MHz SPI. Do not insert or
initialize an SD card on the shared Sense SPI wiring. GPIO6 is left available
for future battery ADC use. See [hardware review](docs/hardware.md).

After verifying and connecting the display power/backlight correctly:

```sh
idf.py menuconfig
```

Enable **BitBot hardware bring-up → Enable verified 7-pin ST7789 240x240 display
wiring**. The default build leaves external pins inactive. The test shows
blinking eyes, a status stripe, and red/green/blue test bars. Color inversion,
RGB/BGR order, and row offset are configurable. Physical panel validation is
still required. Setup text, UTF-8 captions, and backlight dimming are not yet
implemented; the password currently remains on the serial console.

## Project map

| Path | Purpose |
| --- | --- |
| [BitBot_PROJECT_SPEC.md](BitBot_PROJECT_SPEC.md) | Original project brief; read before changing architecture |
| [docs/architecture.md](docs/architecture.md) | Upstream comparison, scope, provisioning and security decisions |
| [docs/hardware.md](docs/hardware.md) | Camera reservations, display wiring and power questions |
| [docs/providers.md](docs/providers.md) | Free/paid/self-hosted options and bilingual evaluation plan |
| [docs/wall-e-reference.md](docs/wall-e-reference.md) | Findings from the additional project/installer |
| [docs/next-steps.md](docs/next-steps.md) | Handoff and physical acceptance checklist |
| `web/` | Shared English portal; no frontend framework needed |
| `config/settings-schema.json` | Settings defaults and limits used to generate firmware schema |
| `firmware/main/` | ESP-IDF commissioning, NVS, network, HTTP, DNS and display modules |
| `lib/`, `tools/`, `tests/` | Desktop simulator, packaging and automated checks |

Developer firmware stores credentials in NVS **without flash encryption**.
The setup AP is WPA2 protected and its API is restricted to that interface;
physical flash access can still reveal secrets. Do not commit real keys or
passwords. Secure boot, encrypted storage, and scoped service tokens belong in
the preparation for distributing finished devices.
