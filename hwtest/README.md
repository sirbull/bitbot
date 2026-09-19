# Maskinvaretest

Eget lite ESP-IDF-program som viser på skjermen hvilke deler som svarer.
Pinner som i [docs/wiring.md](../docs/wiring.md). Bruker samme partisjonstabell
som hovedfirmwaren, så lagrede Wi-Fi-innstillinger blir liggende.

```sh
. ~/esp/esp-idf-v6.0.2/export.sh
cd hwtest
idf.py -p /dev/cu.usbmodem11201 flash monitor
```

Tilbake til vanlig firmware: `cd ../firmware && idf.py -p ... flash`.

| Linje | Grønn betyr | Rød / annet |
| --- | --- | --- |
| CPU / PSRAM | 8 MB PSRAM funnet | 0 MB: feil kort eller sdkconfig |
| MIC | INMP441 sender data; stolpen følger lyd | NO SIGNAL: SD-linjen (GPIO8) står fast. Sjekk VDD, SCK, WS, SD og L/R→GND. `MIC R` = L/R står høy |
| SPEAKER | — | Ingen automatisk sjekk. Piper ved oppstart og når du trykker BOOT |
| BATT | — | Viser alltid FLOATING: batterimåling er fjernet, GPIO6 er ikke koblet. Ignorer |
| CAMERA | Sensornavn og bilder/s, bilde nederst | NOT FOUND: kamera-kabel. Trykk RESET etter tilkobling |

Uten kamera viser nederste halvdel tre felt som skal være rød, grønn, blå.
Kalibrering (invertering, radforskyvning) står øverst i
[main/main.cpp](main/main.cpp).
