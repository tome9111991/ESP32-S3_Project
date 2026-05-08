# TODO - ESP32-8048S043C KlipperCryptoWeatherPanel

Stand: erster Merge/Port vom Guition_JC4827W543-Projekt auf ESP32-8048S043C.

## Aktueller Stand

- Projekt liegt im Zielordner `ESP32-S3_Project/ESP32-8048S043C/KlipperCryptoWeatherPanel`.
- Quellfunktionen fuer Uhrzeit, Wetter, Crypto, Chart und Klipper wurden uebernommen.
- Display-Treiber wurde von Guition/NV3041A/LovyanGFX auf ESP32-8048S043C `esp_lcd` RGB 800x480 umgestellt.
- Das urspruengliche 480x272-UI-Layout wird aktuell mittig auf dem 800x480-Display angezeigt.
- Backlight ist auf GPIO2 umgestellt.

## Naechste Pruefpunkte

- Bildausrichtung pruefen: gedreht, gespiegelt oder korrekt.
- Farben pruefen: RGB/BGR oder Byte-Swap korrekt.
- Stabilitaet der LVGL-Flushing-Ausgabe beobachten, besonders bei Screenwechseln und Chart.
- Entscheiden, ob das UI nur zentriert bleiben soll oder spaeter auf echte 800x480 ausgebaut wird.
- Helligkeit pruefen: aktuell nur GPIO2 an/aus, kein PWM-Dimmen.
- PSRAM-/Speicherverbrauch beobachten, weil RGB-Framebuffer und LVGL-Fullbuffer gross sind.
- README nach dem ersten echten Boardtest mit den beobachteten Ergebnissen aktualisieren.
- Netzwerk/NTP auf ESP32 Arduino Core 3.3.8 beobachten: `configTzTime()` wird genutzt, keine zusaetzlichen manuellen LWIP-Locks setzen.

## Moegliche spaetere Anpassungen

- 800x480-Layout bauen statt 480x272 zentriert.
- Optional PWM-Helligkeit fuer GPIO2 testen, falls das Board es sauber unterstuetzt.
- LVGL-Bufferstrategie optimieren, falls Fullbuffer zu viel PSRAM braucht oder Flush langsam wirkt.
- Falls Bildfehler auftreten: PCLK 16 MHz gegen 14 MHz testen und `LCD_BOUNCE_LINES` anpassen.
