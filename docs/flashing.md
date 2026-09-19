# Laste opp firmware til ESP32-S3 fra Mac

Enkleste vei: GitHub Actions bygger firmware ved hver push. Last ned ferdig
build og flash den med `esptool`. Du trenger ikke installere ESP-IDF.

## Engangsoppsett

```sh
brew install esptool gh
gh auth login
```

## 1. Finn porten

Koble til med USB-C (en kabel som kan overføre data, ikke bare lade).

```sh
ls /dev/cu.usbmodem*
```

Du får noe som `/dev/cu.usbmodem11201`. Finner du ingen: hold **BOOT**, trykk
**RESET**, slipp **BOOT**, og prøv igjen.

## 2. Hent siste build fra CI

Sjekk at siste kjøring på `main` er grønn og at du har pushet det du vil teste:

```sh
cd bitbot
gh run list -L 3
rm -rf /tmp/bitbot-fw
gh run download -n bitbot-commissioning-esp32s3 -D /tmp/bitbot-fw
```

Uten run-ID laster `gh run download` ned fra siste kjøring. Vil du ha en
bestemt build, legg til ID-en: `gh run download <id> -n ...`.

## 3. Flash

```sh
cd /tmp/bitbot-fw
esptool --chip esp32s3 -p /dev/cu.usbmodem11201 -b 460800 write-flash @flash_args
```

Den skal avslutte med `Hash of data verified.` og `Hard resetting`.

**Ikke kjør `erase-flash`** ved vanlige oppdateringer. Det sletter lagrede
innstillinger (Wi-Fi, nøkler).

## 4. Se loggen og oppsettpassordet

```sh
screen /dev/cu.usbmodem11201 115200
```

Trykk **RESET** på kortet for å se oppstarten. Nederst står:

```
BITBOT PHYSICAL SETUP
Network: BitBot-XXXX
Setup password: ...
Open http://192.168.4.1
```

Avslutt `screen` med `Ctrl-A`, så `K`, så `y`.

Koble deg til `BitBot-XXXX` med passordet og åpne http://192.168.4.1.

## Feilsøking

| Problem | Løsning |
| --- | --- |
| `Failed to connect` / porten er opptatt | Lukk `screen` eller andre seriemonitorer, prøv igjen |
| Ingen `usbmodem`-port | Bytt kabel, eller BOOT + RESET for nedlastingsmodus |
| Sjekk hvilken brikke som er koblet til | `esptool -p /dev/cu.usbmodem11201 chip-id` (skal si ESP32-S3) |

## Bygge lokalt i stedet

Bare nødvendig hvis du vil teste endringer uten å pushe. Installer
[ESP-IDF 6.0.2](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/),
og så:

```sh
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem11201 flash monitor
```
