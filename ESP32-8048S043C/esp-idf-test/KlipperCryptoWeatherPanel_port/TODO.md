# TODO - KlipperCryptoWeatherPanel ESP-IDF Port

Arbeitsordner: `ESP32-S3_Project/ESP32-8048S043C/esp-idf-test/KlipperCryptoWeatherPanel_port`

## Setup-Hinweise

- `main/config_private.h` lokal aus `main/config_private.example.h` anlegen.
- WiFi (als Fallback), Standort, Crypto-Paar und `KLIPPER_BASE_URL` in `main/config_private.h` setzen.
- Runtime-Settings (WLAN, Screens, Crypto, Display, Touch-Cal) ueberschreiben die Compile-Time-Defaults nach dem ersten Setup auf dem Geraet.

## Aktueller Parity-Stand

- Bereits im Port vorhanden:
  - Vier Hauptscreens: Uhr/Wetter, Crypto-Preis, Crypto-Chart, Klipper.
  - GT911 Touch mit links/rechts Screenwechsel.
  - Auto-Rotation, Rotationsdauer pro Screen aus Runtime-Settings.
  - Bright Sky Wetter, Coinbase Spot/Candles, Moonraker Basisstatus + erweiterte Klipper-Daten.
  - Wetter-/Status-Assets und grosse Preis-/Zeit-Fonts uebernommen.
  - Crypto-Chart Preisfarbe bleibt bei unveraendertem Spotpreis auf der letzten Up/Down-Farbe.
  - Crypto Live-Candle wird aus dem Spotpreis fortgeschrieben.
  - Separater Coinbase Stats Request liefert 24h Open fuer die Preis-Change-Anzeige.
  - Wettercode-Priorisierung und Bright-Sky-Station-Fallback an Arduino-Sketch angeglichen.
  - Long-Press oeffnet Popup-Menue (Settings/Reboot/Factory-Reset) inkl. Confirm-Dialog und wachsendem Feedback-Arc.
  - Factory-Reset macht `nvs_flash_erase()` + `esp_restart()`.
  - LCD-Backlight via LEDC PWM (250 Hz, 8 bit); Min-Visible-Duty wie Arduino.
  - Tag/Nacht-Helligkeit anhand Sonnenstand (alle 30 s, hell ab Sunrise+90 min, Nacht ab Sunset).
  - Sonnenstand-Berechnung dedupliziert (`calculate_sun_times` in `display_brightness.c`).
  - Touch-Pipeline: `process_coords` gibt Panel-Pixel-Raum zurueck, LVGL macht die 180-Rotation intern via `lv_display_rotate_point`. Lokaler `touch_poll_timer_cb` invertiert manuell fuer Screen-Switch + Feedback-Arc.
  - Settings-Menue (Port von `12_SettingsMenu.ino`) mit echten Sub-Screens: WLAN, Screens, Crypto, Display, Touch kalibrieren.
  - Display-Settings: Slider 32..255 mit Live-Preview, Rotate-180-Toggle, NVS-Persistenz, Reboot beim Toggle.
  - WLAN-Setup: SSID-Liste + On-Screen-Tastatur (`ui_keyboard.c`), Credentials in NVS, Compile-Time-Werte als Fallback.
  - Screen-Settings: enable/disable pro Screen + Rotationsdauer pro Screen (Defaults: 30 s Uhr, 15 s andere), NVS-Persistenz.
  - Crypto-Settings: base/quote/timeframe Auswahl, NVS-Persistenz, Daten-Refresh nach Aenderung.
  - Touch-Kalibrierung: 5-Punkt-UI, CAL-Werte in NVS, beruecksichtigt 180-Rotation.
  - Klipper-Parity: `/server/info`, `/printer/info`, `/server/files/metadata` (ETA + Retry-After), `display_message`-Formatierung, MMU (Tool/Gate/Colors/Status, bis 8 Gates).
  - Popup-Buttons sind echte LVGL-Buttons mit `LV_EVENT_CLICKED`.

- Bewusst IDF-spezifisch anders:
  - Display/LVGL laufen mit IDF RGB-Panel, Double-Framebuffer und Bounce-Buffer statt Arduino-Flush.
  - Compile-Time `config_private.h` bleibt als Fallback, wenn NVS leer ist (Factory-Reset macht bewusst kompletten `nvs_flash_erase()`, damit nach dem Reset wieder die Build-Defaults greifen - dev-convenience).
  - Eigene Partitionstabelle (`partitions.csv`): 4 MB factory + 2x 4 MB OTA + ~4 MB SPIFFS-Storage.

## Noch offen

### Refactor — nur bei Bedarf
Beides aktuell keine Schuld. Erst angehen, wenn der jeweilige Trigger eintritt:

- `net_fetcher.c` (~1670 Zeilen) in `wifi_service` / `weather_service` / `crypto_service` / `klipper_service` zerlegen.
  - Trigger: ein Service bekommt echte neue Komplexitaet (z. B. Klipper-WebSocket statt Polling, Service-spezifische Retry-Policies, neue API-Anbieter).
  - Sonst: Datei ist gross, aber klar in Bloecke sortiert; Helper waeren beim Split entweder zu duplizieren oder in `net_common.c` zu ziehen.
- `g_app` Setter statt 170 direkter Writes.
  - Trigger: Dirty-Flags, Change-Notifications oder Invarianten-Checks werden gebraucht.
  - Sonst: `app_lock/app_unlock`-Pattern ist mechanisch und sichtbar, Setter waeren reine Boilerplate.
- HTTP Retry/Backoff pro Service.
  - Trigger: konkrete Stabilitaetsprobleme im Feld (z. B. Bright Sky / Coinbase flaky). `http_get` ist schon einheitlich.

### Dokumentation
- README mit aktuellem Stand (Runtime-Settings, NVS-Layout, Sub-Screens).
- Bekannte Abweichungen zum Arduino-Sketch dokumentieren.
- Pin-/Timing-Entscheidungen aus dem Displaytest im Port-README behalten.
- Konfigurationsbeispiele fuer `mainsail`, lokale IP und Crypto-Paare ergaenzen.

### Spaeter optional
- OTA-/Web-Config pruefen (Partitionen liegen schon).
- API-Status-/Diagnose-Screen.
- Screenshots vom IDF-Port aufnehmen und in README verlinken.
