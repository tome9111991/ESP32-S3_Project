# TODO - KlipperCryptoWeatherPanel ESP-IDF Port

Arbeitsordner: `ESP32-S3_Project/ESP32-8048S043C/esp-idf-test/KlipperCryptoWeatherPanel_port`

## Vor der ersten Hardware-Runde

- `main/config_private.h` lokal aus `main/config_private.example.h` anlegen.
- WiFi, Standort, Crypto-Paar und `KLIPPER_BASE_URL` in `main/config_private.h` setzen.
- Nach dem ersten eigenen Build fehlende IDF/LVGL API-Abweichungen korrigieren.
- Auf dem Board pruefen:
  - Display startet mit Dashboard statt Demo.
  - Touch links/rechts schaltet Screens.
  - Auto-Rotation laeuft ohne Touch.
  - Uhrzeit wird nach WLAN/NTP korrekt lokal angezeigt.
  - HTTPS zu Bright Sky und Coinbase funktioniert mit Zertifikatsbundle.
  - Moonraker HTTP funktioniert mit dem gesetzten Hostnamen/IP.

## Aktueller Parity-Stand

- Bereits im Port vorhanden:
  - Vier Hauptscreens: Uhr/Wetter, Crypto-Preis, Crypto-Chart, Klipper.
  - GT911 Touch mit links/rechts Screenwechsel.
  - Auto-Rotation.
  - Bright Sky Wetter, Coinbase Spot/Candles und Moonraker Basisstatus.
  - Wetter-/Status-Assets und grosse Preis-/Zeit-Fonts sind uebernommen.
  - Crypto-Chart Preisfarbe bleibt bei unveraendertem Spotpreis auf der letzten Up/Down-Farbe.
  - Crypto Live-Candle wird aus dem Spotpreis fortgeschrieben.
  - Separater Coinbase Stats Request liefert 24h Open fuer die Preis-Change-Anzeige.
  - Wettercode-Priorisierung und Bright-Sky-Station-Fallback sind an den Arduino-Sketch angeglichen.
  - Long-Press oeffnet Popup-Menue (Settings/Reboot/Factory-Reset) inkl. Confirm-Dialog und wachsendem Feedback-Arc.
  - Factory-Reset macht `nvs_flash_erase()` + `esp_restart()`.
  - LCD-Backlight via LEDC PWM (250 Hz, 8 bit) statt fester GPIO-High; Min-Visible-Duty wie Arduino.
  - Tag/Nacht-Helligkeit anhand Sonnenstand (alle 30 s, hell ab Sunrise+90 min, Nacht ab Sunset, Werte 1:1 zum Arduino).
  - Display-Settings-Screen ueber Popup -> Settings: Slider 32..255 mit Live-Preview, NVS-Persistenz (`display/day_bright`).
  - Rotate-180-Toggle im Display-Settings: NVS-Persistenz (`display/rotate180`), Reboot beim Zurueckgehen wenn geaendert (LVGL-Buffer-Layout + Touch-Map werden beim Boot anhand des NVS-Werts gesetzt).
  - Sonnenstand-Berechnung dedupliziert (`calculate_sun_times` einmalig in `display_brightness.c`, geteilt mit Sun-Icon).
  - Touch-Pipeline: `process_coords` gibt Panel-Pixel-Raum zurueck, LVGL macht die 180-Rotation intern via `lv_display_rotate_point`. Lokaler `touch_poll_timer_cb` invertiert manuell fuer Screen-Switch + Feedback-Arc.
  - Settings-Menue (Port von `12_SettingsMenu.ino`) mit Liste WLAN / Screens / Crypto / Display / Touch kalibrieren. Long-Press -> Popup -> Settings oeffnet jetzt das Menue (statt direkt DisplaySettings). Display ist real (Brightness + Rotate), die anderen vier oeffnen einen Dummy-Sub-Screen "Coming soon" mit Back. Back-Pfeil-Glyphen sind in der 52x52-Box mittig (Label auf `LV_SIZE_CONTENT` + `lv_obj_center`).
- Bewusst IDF-spezifisch anders:
  - Display/LVGL laufen mit IDF RGB-Panel, Double-Framebuffer und Bounce-Buffer statt Arduino-Flush.
  - Konfiguration kommt aktuell aus `main/config_private.h`, nicht aus Laufzeit-Settings.
  - Touch-Kalibrierung ist fest im GT911-Driver-Callback, noch ohne UI zum Nachkalibrieren.
  - Popup-Buttons sind echte LVGL-Buttons mit `LV_EVENT_CLICKED` statt manuellem `pointInRect`.
- Noch nicht gleich zum Arduino-Sketch:
  - Settings-Untermenues: Geruest steht (Menue + 4 Dummy-Sub-Screens), aber WLAN/Screens/Crypto/Touch-Kalibrierung haben noch keine echten Inhalte. Display ist komplett.
  - Persistente Settings nur fuer Tag-Helligkeit (NVS); restliche Settings noch fluechtig.
  - Display-Rotation ist jetzt Runtime via NVS, `DISPLAY_ROTATE_180_DEFAULT` dient nur noch als Initial-Default.
  - Klipper-Details, MMU-Anzeige und ETA/Metadata fehlen.
  - Crypto-Timeframes 15M/1H/6H/1D muessen auf Hardware noch komplett getestet werden.
  - Wetterdarstellung muss auf Hardware noch gegen echte Bright-Sky-Antworten geprueft werden.

## Dashboard-Parity zum Arduino-Sketch

- Runtime-Settings wieder einbauen:
  - WiFi-Setup direkt am Display.
  - Screen enable/disable.
  - Crypto base/quote/timeframe Auswahl.
  - Display-Helligkeit + Rotation (beide erledigt via NVS + Display-Settings-Screen).
  - Touch-Kalibrierung (CAL-Werte aus `touch_process_coords` in Runtime-Struct verschieben).
- Persistenz ersetzen:
  - Arduino `LittleFS` JSON-Settings nach ESP-IDF `NVS` oder LittleFS-Komponente portieren.
  - Factory-Reset erweitert sich von "nur NVS erase" auf gezieltes Loeschen der Settings-Keys, wenn NVS-Settings da sind.
- Popup-/Settings-Overlay portieren:
  - Long-press Menu. (erledigt)
  - Reboot. (erledigt)
  - Factory reset mit Bestaetigung. (erledigt, noch ohne Settings-Loeschen)
  - Settings-Untermenues: Menue-Geruest + Routing fertig, Display-Inhalt fertig. Dummies durch echte Implementierungen ersetzen: WLAN-Setup, Screen-Toggles, Crypto-Auswahl, Touch-Kalibrierung.
- Klipper-Parity ergaenzen:
  - `/server/info` und `/printer/info` fuer bessere Offline-/Klippy-Zustaende.
  - `/server/files/metadata` fuer ETA/Restzeit.
  - Display-message Formatierung wie im Arduino-Sketch.
  - MMU Objekt/Gates/Farben/Status wieder darstellen.
- BTC/Crypto-Parity ergaenzen:
  - Chart-Timeframes 15M/1H/6H/1D komplett testen.
  - Fehlertexte pro API genauer anzeigen.
- Wetter-Parity ergaenzen:
  - Bright Sky Station/Fallback-Logik mit mehr Details.
  - Wettercode-Priorisierung auf Hardware gegen echte Niederschlag/Sonne-Meldungen pruefen.

## IDF-native Refactor

- `net_fetcher.c` weiter zerlegen:
  - `wifi_service.c`
  - `weather_service.c`
  - `crypto_service.c`
  - `klipper_service.c`
- App-State API kapseln:
  - direkte `g_app` Writes durch kleine Setter ersetzen.
  - String-Laengen und Statuswerte zentralisieren.
- Screen-Rotation konfigurierbar machen statt feste Intervalle in `main.c`.
- HTTP-Helfer robuster machen:
  - einheitliche Statuscodes.
  - Retry/backoff pro Service.
  - optionale lokale IP statt mDNS-Hostname fuer Moonraker.
- LVGL-Zugriff strikt im LVGL-Kontext halten:
  - Fetch-Task schreibt nur State.
  - UI-Refresh bleibt im LVGL-Timer.
- Speicher pruefen:
  - Chart-Buffer PSRAM vs intern dokumentieren.
  - grosse Font-/Asset-Segmente und Partitiongroesse beobachten.

## Dokumentation

- README nach erstem erfolgreichen Hardware-Test aktualisieren.
- Bekannte Abweichungen zum Arduino-Sketch dokumentieren.
- Pin-/Timing-Entscheidungen aus dem Displaytest kurz im Port-README behalten.
- Konfigurationsbeispiele fuer `mainsail`, lokale IP und Crypto-Paare ergaenzen.

## Spaeter optional

- OTA-/Web-Config pruefen.
- Eigene Partitionstabelle fuer groessere App/Assets.
- API-Status-/Diagnose-Screen.
- Screenshots vom IDF-Port aufnehmen und in README verlinken.
