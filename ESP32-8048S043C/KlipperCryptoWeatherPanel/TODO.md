# TODO - ESP32-8048S043C KlipperCryptoWeatherPanel

Stand: erster Merge/Port vom Guition_JC4827W543-Projekt auf ESP32-8048S043C.

## Aktueller Stand

- Projekt liegt im Zielordner `ESP32-S3_Project/ESP32-8048S043C/KlipperCryptoWeatherPanel`.
- Quellfunktionen fuer Uhrzeit, Wetter, Crypto, Chart und Klipper wurden uebernommen.
- Display-Treiber wurde von Guition/NV3041A/LovyanGFX auf ESP32-8048S043C `esp_lcd` RGB 800x480 umgestellt.
- Das UI nutzt jetzt native 800x480-Koordinaten statt das alte 480x272-Layout mittig anzuzeigen.
- Backlight laeuft auf GPIO2 mit echter Arduino-LEDC-PWM, 1000 Hz / 8 bit. Nicht-null Helligkeit bekommt eine Mindest-PWM, damit der Nachtmodus nicht komplett ausgeht.
- Bildausgabe wird um 180 Grad im LVGL-Flush gedreht, weil Panel-Mirror im Test zwar OK meldete, aber kein Bild zeigte.
- Boardtest: Netzwerk/NTP laeuft scheinbar mit der aktuellen `configTzTime()`-Loesung.
- Aktuelle Entscheidung: natives 800x480-Layout verwenden; Fonts und Icon-Groessen nach realem Hardwaretest gezielt nachziehen.
- Chart-Redraw wartet nach Screenwechseln kurz, damit Flush/Screen-Animation nicht direkt zusammenfallen.
- Health-Log zaehlt LVGL-Flushes, Timeouts und Draw-Errors zur Stabilitaetsbeobachtung.

## Naechste Pruefpunkte

- Bildausrichtung pruefen: 180-Grad-Drehung korrekt.
- Farben pruefen: RGB/BGR oder Byte-Swap korrekt. Aktuell ist RGB565 ohne Byte-Swap dokumentiert/logged.
- Stabilitaet der LVGL-Flushing-Ausgabe beobachten, besonders bei Screenwechseln und Chart. Health-Log auf `timeout`/`err` pruefen.
- Helligkeit pruefen: Arduino-LEDC-PWM auf echter Hardware beobachten. Falls Nacht noch zu dunkel ist, `LCD_BL_PWM_MIN_VISIBLE_DUTY` oder `nightBrightness` erhoehen.
- PSRAM-/Speicherverbrauch beobachten, weil RGB-Framebuffer und LVGL-Fullbuffer gross sind.
- README bei weiteren Boardtest-Ergebnissen weiter aktualisieren.
- Netzwerk/NTP auf ESP32 Arduino Core 3.3.8 weiter beobachten: `configTzTime()` wird genutzt, keine zusaetzlichen manuellen LWIP-Locks setzen.

## Moegliche spaetere Anpassungen

- Fonts und Icons fuer 800x480 pruefen und bei Bedarf groessere Varianten erzeugen.
- Falls Arduino-LEDC-PWM auf GPIO2 ebenfalls nicht sauber funktioniert, alternative Backlight-Ansteuerung statt GPIO2/LEDC pruefen.
- LVGL-Bufferstrategie optimieren, falls Fullbuffer zu viel PSRAM braucht oder Flush langsam wirkt.
- Falls Bildfehler auftreten: PCLK 16 MHz gegen 14 MHz testen und `LCD_BOUNCE_LINES` anpassen.
