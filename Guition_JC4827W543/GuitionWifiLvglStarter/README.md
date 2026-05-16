# GuitionWifiLvglStarter

Grundgeruest fuer das Guition JC4827W543 mit ESP32-S3, WLAN und LVGL v9.

Basis wurde aus diesen lokalen Referenzen abgeleitet:

- `../KlipperCryptoWeatherPanel/`
- `../DisplayPixelRaster/`
- `../BOARD_CODING_NOTES.md`

## Enthalten

- LovyanGFX-Konfiguration fuer NV3041A ueber QSPI/SPI
- LVGL v9 mit Fullscreen-Buffer in PSRAM
- stabile Flush-Route mit `LV_COLOR_FORMAT_RGB565_SWAPPED`
- WLAN-Start und automatischer Reconnect
- NTP-Zeitstart ueber `configTzTime`
- einfache Status-UI mit WLAN, IP, Uhrzeit und freiem Heap

## Lokale Config

`config_private.example.h` nach `config_private.h` kopieren und WLAN-Daten eintragen.
Wenn `config_private.h` fehlt, nutzt der Starter nur die Platzhalterwerte aus
`config_private.example.h`; echte WLAN-Verbindung gibt es damit natuerlich nicht.
`config_private.h` ist absichtlich in `.gitignore`.

## Arduino IDE Startwerte

- Board: `ESP32S3 Dev Module`
- CPU Frequency: 240 MHz
- Flash Size: 4 MB
- Partition Scheme: `Huge APP (3MB No OTA/1MB SPIFFS)`
- PSRAM: `OPI PSRAM` / `Octal PSRAM` aktivieren
- USB CDC On Boot: Enabled

Nicht aus dem RGB-Panel-Projekt `ESP32-8048S043C` uebernehmen: dieses Board nutzt hier NV3041A ueber QSPI/SPI.
