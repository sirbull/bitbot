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
- ST7789 240×240 seven-pin no-CS driver and optional blinking-face/RGB bars
  bring-up. Uses a small DMA stripe buffer, not a full framebuffer. Waits for
  DMA completion before reusing a buffer. Disabled by default until wiring.
- Research and hardware documentation, including the confirmed display pin list.

## Next work, in order

1. **Physical commissioning test.** Flash via USB; join AP, test valid and wrong
   passwords, reboot, travel between two networks, forget one, and verify
   preferences/keys survive. Check AP closes, BOOT reopens it, and known-network
   connection recovers. Test from Android/iOS captive browsers and ordinary
   Chrome/Safari. Verify configuration is inaccessible from the joined LAN.
2. **Electrical verification.** Identify VCC/BLK circuitry, charger module,
   speaker impedance/power, and battery current limits. Keep battery/charger
   wiring disconnected until verified. A confirmed absent CS frees GPIO6 for
   ADC, but does not verify a power circuit.
3. **Physical display test.** Enable the menuconfig option. Verify full 240×240
   area, RGB bars, inversion, row offset, and blink timing. Then add an original
   face renderer with setup instructions, WPA2 setup password, and readable
   Norwegian UTF-8 captions. Keep its API separate from conversation logic.
4. **Audio/camera/ADC independently.** Verify the proposed shared I2S clocks
   and matching sample formats. Record microphone PCM locally, test amplifier
   at low volume, capture/release one camera frame, and calibrate ADC1 GPIO6.
5. **XiaoZhi integration.** Pin a tested upstream revision; keep BitBot board,
   face and commissioning code isolated. Migrate settings explicitly; do not
   silently overwrite upstream Wi-Fi namespaces or copy the AI Pin binary.
6. **First bilingual conversation.** Push-to-talk first, then STT → model/tool
   → one canonical response → captions and TTS. Add time/battery/preview tools,
   validate provider capabilities and costs. Implement "Hey BitBot" only when
   its actual model is available and tested.
7. **Intentional camera vision and power.** Preview locally; only upload an
   image on a visual request. Add timeouts and camera teardown. Measure current
   in each state before enabling aggressive sleep or estimating battery life.

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
  backlight dimming, NTP/local tools, or battery sampling yet.
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
