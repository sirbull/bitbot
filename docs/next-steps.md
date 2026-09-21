# Handoff and acceptance checklist

## Implemented

- English offline-capable portal, inspired by the paper/orange/dark visual
  language of the user's design reference. No copied assets or external fonts.
- Desktop simulator with durable local settings, explicit fake networking,
  connection failure simulation, mobile/desktop browser tests.
- ESP-IDF 6.0.2 commissioning target: USB console, BOOT hold, protected AP,
  bounded DNS responder, local HTTP API, session protection, NVS storage,
  five saved networks, test-before-save, reconnect/backoff, and finish/restart.
- Shared generated settings schema and compressed web assets; provider-specific
  key retention/removal. Preferences remain separate from Wi-Fi profiles.
- ST7789 240×240 driver for the fitted 8-pin module (CS on D3, SPI mode 3),
  verified on hardware. A small DMA stripe buffer, not a full framebuffer, and
  it waits for DMA completion before reusing a buffer. Face with twelve
  expressions, blinking and wandering eyes; text area with setup instructions,
  pairing code and captions; dimming in place of an unavailable backlight pin.
- XiaoZhi device activation: the OTA endpoint is asked for a pairing code, the
  code is shown on screen, and the device polls until the user adds it.
- Voice: full-duplex I2S (INMP441 + MAX98357A), Opus in and out, WebSocket to
  XiaoZhi, local wake phrases (esp-sr MultiNet), button push-to-talk, barge-in,
  captions, LLM emotions on the face, and MCP so XiaoZhi can ask for a photo.
- Camera: OV3660 identified on the Sense board; a photo is captured, shown on
  screen and uploaded to XiaoZhi's vision service only when the assistant asks.
- `hwtest/`: a separate bring-up program that reports each part on the screen.
  BOOT steps the speaker test tone through off / 2% / 10% / 40% / 40%-with-the-
  mic-off. Pressing BOOT within 3 s of reset instead enters the audio-only
  diagnostic: display, camera, battery and microphone all stay uninitialised and
  `app_main` does nothing but write a 440 Hz sine into I2S, so a tone that is
  still not clean there cannot be blamed on the rest of the firmware. Each
  further BOOT press steps to the next I2S format (16/32-bit, mono/stereo,
  16/44.1/48 kHz), which separates "the amplifier cannot lock onto this format"
  from "something is stalling the writer". `tools/tone_check.cpp` checks the
  waveform maths on a PC.
- Research and hardware documentation, including the confirmed display pin list.

## Next work, in order

1. **Physical commissioning test.** Flash via USB; join AP, test valid and wrong
   passwords, reboot, travel between two networks, forget one, and verify
   preferences/keys survive. Check AP closes, BOOT reopens it, and known-network
   connection recovers. Test from Android/iOS captive browsers and ordinary
   Chrome/Safari. Verify configuration is inaccessible from the joined LAN.
2. **Electrical verification.** Identify VCC/BL circuitry and speaker
   impedance/power. Bench-test the identified FM5324GA/IP5306-clone power
   module on its own — charge current into a cell, and whether its 5 V output
   survives a ~30 mA load — and pick the cell from that result. Keep
   battery/module wiring off the XIAO until then.
3. **Physical display test.** Enable the menuconfig option. Verify full 240×240
   area, RGB bars, inversion, row offset, and blink timing. Then add an original
   face renderer with setup instructions, WPA2 setup password, and readable
   Norwegian UTF-8 captions. Keep its API separate from conversation logic.
4. **Audio/camera independently.** Verify the proposed shared I2S clocks
   and matching sample formats. Record microphone PCM locally, test amplifier
   at low volume, and capture/release one camera frame.
5. **XiaoZhi integration.** Pin a tested upstream revision; keep BitBot board,
   face and commissioning code isolated. Migrate settings explicitly; do not
   silently overwrite upstream Wi-Fi namespaces or copy the AI Pin binary.
6. **Speaker output on real hardware.** The firmware decodes and writes ~1 s of
   audio per second to I2S during a reply (measured), but nothing has been heard
   yet. Check the MAX98357A supply (it needs the 5 V rail, which is dead when
   the module switch is off and only USB is connected), its SD pin, the shared
   ground, and the speaker across OUT+/OUT−.
7. **"Hey BitBot" as a real wake word.** esp-sr's English MultiNet only matches
   real English words, so today's phrases are "hey robot" / "hey bit robot".
   A trained WakeNet model for the actual phrase is the only way to get it;
   until then the button is the reliable path.
8. **Power.** Measure current in each state before enabling aggressive sleep or
   estimating battery life. The power module's low-load cutoff may put a floor
   under the sleep states. The battery divider on GPIO6 is still unwired; the
   test firmware reports it as floating.

## What automated checks establish

Node tests validate simulator persistence/rollback, input limits, API access
rules, secret redaction, and networking contract. Playwright covers the real
shared portal in Chromium desktop and mobile emulation, with no external
asset requests. ESP-IDF CI compiles and links the target firmware.

These checks do **not** prove physical Wi-Fi timing, AP/STA channel changes,
NVS behavior during power loss, GPIO wiring, actual LCD colors, audio quality,
battery safety/runtime, or real Norwegian/provider performance. Record those
results here when hardware is available; do not label them tested in advance.

## Known first-milestone limitations

- Saved AI/voice/display preferences are mostly contracts for later adapters.
  No AI calls, audio capture/playback, cloud vision, wake-word engine, captions,
  backlight dimming, or NTP/local tools yet. There is no battery sensing.
- The physical setup password is output to the USB console for bring-up; screen
  presentation and a polished no-computer first-boot experience are next.
- Endpoint validation does not prove an endpoint is compatible/reachable. The
  bundled speech model and voice lists are documented fallbacks; provider
  connection testing and live catalogue discovery do not exist yet.
- XiaoZhi account pairing is not integrated yet. Its authenticated console owns
  the live language-filtered voice list and provider audio samples. The portal
  links there and can store a separate speech-provider choice in the meantime.
- The hosted service options are candidates, not measured recommendations.
- Firmware and simulator have separate native storage/network implementations;
  shared schema and UI reduce drift but do not replace physical integration tests.
- AP/STA use one radio channel. Testing a network can briefly interrupt the
  browser's hotspot connection; the saved configuration survives reconnecting.
- The current partition layout intentionally has no OTA slots. Design assets,
  wake-word storage, OTA capacity and migration together before release.
