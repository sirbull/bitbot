# Tonetest

Det minste programmet som kan lage lyd: én I2S-sender, én sinus, ingenting annet.
Ingen skjerm, kamera, mikrofon, PSRAM eller Wi-Fi, og ingen andre tasker enn den
som skriver logg. Er ikke tonen ren her, hjelper det ikke å endre firmwaren.

```sh
. ~/esp/esp-idf-v6.0.2/export.sh
cd tonetest
idf.py -p /dev/cu.usbmodem11201 flash monitor
```

Tilbake til vanlig firmware: `cd ../firmware && idf.py -p ... flash`.

## Bryter

Koble **D2 / GPIO3 til GND** for å dempe, la den stå åpen for å spille. Pinnen har
intern opptrekk, så en enkel ledning eller bryter holder.

**Ikke D6.** D6 er GPIO43, altså I2S-bitklokka til både mikrofon og forsterker
([docs/wiring.md](../docs/wiring.md)). Trekker du den til GND stopper hele bussen.

Dempingen skriver nuller i stedet for å slutte å skrive, så DMA-en holdes fylt og
forsterkeren beholder låsen. Det gjør bryteren til en test i seg selv: **støy mens
den er dempet kan umulig komme fra samplene, for de er beviselig bare nuller.** Da
er det klokking, ledninger eller forsterkeren — ikke programvare.

## Logg

Hver andre sekund, fra en egen task så den aldri kan stoppe skrivingen:

```
TONE | 4310 writes, 0 short, 0 errors | worst gap 6012 us (DMA holds 43537 us)
```

`worst gap` er lengste tid ett skriv tok. Større enn `DMA holds` betyr at
høyttaleren gikk tom, og det hullet *er* skrapet. Små tall her, men fortsatt
skraping, betyr at feilen er analog.

## Knotter

Øverst i [main/main.cpp](main/main.cpp): `kRate` og `kBits`. Standard er 44,1 kHz
16-bit stereo, som gir 32 BCLK per ramme — forholdet en MAX98357A er minst kresen
på, og det unngår den fraksjonelle klokkedeleren 16 kHz krever på ESP32-S3. Bytt
til `16000` og `32` for å sammenligne med det hovedfirmwaren bruker.
