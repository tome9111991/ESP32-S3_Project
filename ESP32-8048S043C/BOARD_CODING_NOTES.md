# ESP32-8048S043C Coding-Notizen

Stand: 2026-05-08, aufgeraeumt nach lokalen Display-/Touchtests

Diese Notizen gelten fuer das Board `ESP32-8048S043C` mit
`ESP32-S3-WROOM-1` und 4.3-Zoll-Display `800x480`.

## Kurzfazit

- Board: ESP32-8048S043C, 4.3 Zoll HMI/TFT.
- MCU/Modul: ESP32-S3-WROOM-1, Markierung `MCN16R8`.
- Lokal bestaetigt: 16 MB Flash, 8 MB PSRAM, 40 MHz Crystal.
- Display: 800 x 480 RGB565 ueber paralleles RGB-Interface.
- Touch: GT911 per I2C, lokal Adresse `0x5D`.
- Backlight: GPIO2, `HIGH` schaltet ein.
- SD-Karte: SPI, CS GPIO10.

Empfohlene Displaybasis fuer fluessige Touch-GUI:

- `esp_lcd` RGB direkt mit Doublebuffer.
- Testsketch: `displaytest_esp_lcd_doublefb/displaytest_esp_lcd_doublefb.ino`
- Lokal deutlich stabiler beim Fingerziehen als Arduino_GFX single framebuffer.

Arduino_GFX bleibt brauchbar fuer einfache/statische Tests:

- Testsketch: `displaytest/displaytest.ino`
- Display und Touch funktionieren.
- Nicht ideal fuer schnelle Touch-Spuren, Animationen oder viele Updates,
  weil die lokale Arduino_GFX-RGB-Klasse nur einen PSRAM-Framebuffer nutzt.

Nicht als Basis fuer dieses Setup empfohlen:

- direkter LovyanGFX-RGB-Treiber mit Arduino-ESP32 `3.3.8`
- lokal Problem mit `LCD_CAM interrupt`; Display lief nicht sauber.

Wichtig: Dieses Board ist kein Guition/JC4827W543 und kein NV3041A-QSPI-Panel.
Keine QSPI-/SPI-Display-Pinbelegung uebernehmen.

## Arduino-IDE-Startwerte

- Board: `ESP32S3 Dev Module`
- CPU Frequency: 240 MHz
- Flash Size: 16 MB
- Flash Mode: QIO 80 MHz, bei Boot-/Uploadproblemen DIO testen
- PSRAM: `OPI PSRAM` / `Octal PSRAM` aktivieren
- Upload Speed: 921600
- USB CDC On Boot: Disabled, wenn Serial/Upload ueber CH340/UART laeuft
- JTAG Adapter: Disabled, wenn nicht genutzt
- Events Run On: Core 1
- Arduino Runs On: Core 1
- Partition Scheme: fuer Tests `16M Flash (3MB APP/9.9MB FATFS)`

PSRAM muss fuer `displaytest_esp_lcd_doublefb` aktiv sein. Zwei
RGB565-Framebuffer plus Zeichenpuffer brauchen rund 2.3 MB PSRAM:
`800 * 480 * 2 = 768000 Byte` pro Vollbildpuffer.

## Display-Pins

Kontrollsignale:

| Funktion | GPIO |
| --- | ---: |
| LCD_DE | 40 |
| LCD_VSYNC | 41 |
| LCD_HSYNC | 39 |
| LCD_PCLK | 42 |
| TFT_BL | 2 |

RGB-Datenleitungen:

| Farbe | Bits | GPIOs |
| --- | --- | --- |
| Rot | R0 R1 R2 R3 R4 | 45, 48, 47, 21, 14 |
| Gruen | G0 G1 G2 G3 G4 G5 | 5, 6, 7, 15, 16, 4 |
| Blau | B0 B1 B2 B3 B4 | 8, 3, 46, 9, 1 |

Getestete Timing-Startwerte:

- Aufloesung: `800x480`
- RGB: 16 Bit / RGB565
- PCLK: `16000000`, bei Instabilitaet `14000000` testen
- HSYNC: polarity `0`, front `8`, pulse `4`, back `16`
- VSYNC: polarity `0`, front `4`, pulse `4`, back `4`
- PCLK active negative: `1`
- PCLK idle high: `1`
- DE idle high: `0`

## Empfohlener Displayweg

Fuer neue GUI-Arbeit zuerst auf diesem Sketch aufbauen:

```text
displaytest_esp_lcd_doublefb/displaytest_esp_lcd_doublefb.ino
```

Der Sketch nutzt:

- `esp_lcd_new_rgb_panel()`
- `num_fbs = 2`
- `double_fb = true`
- `fb_in_psram = true`
- `esp_lcd_panel_draw_bitmap()`
- `on_color_trans_done`-Callback, bevor der Zeichenpuffer wiederverwendet wird

Lokales Ergebnis: stabileres Bild beim Fingerziehen. Reaktion ist etwas
langsamer als direktes Arduino_GFX-Zeichnen, aber ohne sichtbare Verzerrung.
Wenn mehr Reaktion noetig ist, zuerst im Sketch `TOUCH_FRAME_INTERVAL_MS` von
`50` auf `30` oder `25` senken.

Wenn Speicher/Init Probleme macht:

- pruefen, ob PSRAM aktiv ist
- `LCD_BOUNCE_LINES` testweise von `10` auf `0` setzen
- PCLK von `16000000` auf `14000000` senken

## Arduino_GFX-Nutzung

Arduino_GFX funktioniert lokal fuer Display-/Touch-Grundtests:

```text
displaytest/displaytest.ino
```

Geeignet fuer:

- statische Testbilder
- einfache Statusanzeigen
- seltene Label-/Wertupdates

Nicht ideal fuer:

- Touch-Spuren beim Fingerziehen
- laufende Animationen
- haeufige Vollbild- oder Teilbildupdates

Grund: Die lokale `GFX_Library_for_Arduino` setzt fuer RGB intern
`num_fbs = 1` und `double_fb = false`. Wenn CPU/Cache in denselben
PSRAM-Framebuffer schreibt, den LCD_CAM gerade ausliest, koennen Verzerrungen
auftreten.

## Touch GT911

| Funktion | GPIO / Wert |
| --- | --- |
| I2C SDA | GPIO19 |
| I2C SCL | GPIO20 |
| GT911 Reset | GPIO38 |
| GT911 INT | GPIO18, nicht blind voraussetzen |
| I2C-Adresse | lokal `0x5D`, Fallback `0x14` sinnvoll |

Touchpolling funktioniert. Fuer neue Sketches erstmal ohne Interrupt arbeiten:

```cpp
Wire.begin(19, 20, 400000);
```

Nach dem Lesen des GT911-Statusregisters `0x814E` wieder `0x00` schreiben.
GPIO18/INT ist bei dieser Boardklasse oft nur mit Hardware-Mod sicher nutzbar.

Lokal gemessene Touch-Kalibrierung vom 2026-05-08:

- Display bleibt `800x480`.
- Der GT911 liefert aber Rohwerte nur ungefaehr in diesem Bereich:
  `raw_x` ca. `27..458`, `raw_y` ca. `24..249`.
- Rohwerte deshalb nie direkt als Displaypixel verwenden.
- Nach der Kalibrierung im Sketch
  `displaytest_esp_lcd_doublefb/displaytest_esp_lcd_doublefb.ino`
  waren diese Werte passend:

```cpp
static constexpr bool TOUCH_USE_SAVED_CALIBRATION = true;
static constexpr float TOUCH_CAL_X_RX = 1.65867031f;
static constexpr float TOUCH_CAL_X_RY = -0.02261823f;
static constexpr float TOUCH_CAL_X_C = 2.12817001f;
static constexpr float TOUCH_CAL_Y_RX = 0.02082564f;
static constexpr float TOUCH_CAL_Y_RY = 1.79517055f;
static constexpr float TOUCH_CAL_Y_C = 10.62223816f;
```

Mapping nach dem GT911-Rohwertlesen:

```cpp
screen_x = constrain((int)((TOUCH_CAL_X_RX * raw_x) + (TOUCH_CAL_X_RY * raw_y) + TOUCH_CAL_X_C + 0.5f), 0, LCD_W - 1);
screen_y = constrain((int)((TOUCH_CAL_Y_RX * raw_x) + (TOUCH_CAL_Y_RY * raw_y) + TOUCH_CAL_Y_C + 0.5f), 0, LCD_H - 1);
```

Diese Kalibrierwerte gelten fuer dieses Board mit `800x480` und der aktuell
getesteten Displayausrichtung. Bei gedrehtem/gespiegeltem Display neu
kalibrieren oder Mapping anpassen.

## SD-Karte

| Funktion | GPIO |
| --- | ---: |
| SD_CS | 10 |
| SD_MOSI | 11 |
| SD_SCK | 12 |
| SD_MISO | 13 |

Diese Pins nicht gleichzeitig fuer andere SPI-Geraete verwenden, ausser mit
sauberem CS-Handling.

## Weitere Pins

| GPIO | Nutzung |
| ---: | --- |
| 0 | BOOT Button, Strapping beachten |
| 17 | NC |
| 18 | CTP_INT nur mit/je nach Hardware-Mod |
| 33, 34 | NA laut Pinlisten |
| 35, 36, 37 | NC / NA |
| 43 | U0TXD / CH340 Serial |
| 44 | U0RXD / CH340 Serial |

## Gemessene Boarddaten

Lokal per esptool gemessen:

```text
Chip type: ESP32-S3 (QFN56), revision v0.2
Features: Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
Crystal frequency: 40MHz
Detected flash size: 16MB
Flash type set in eFuse: quad (4 data lines)
Flash voltage set by eFuse: 3.3V
Secure Boot: Disabled
Flash Encryption: Disabled
```

## Praktische Regeln

- Fuer fluessige Touch-GUI: `esp_lcd` Doublebuffer als Basis nehmen.
- Fuer einfache Tests: Arduino_GFX ist okay.
- Keine schnellen Animationen direkt in einen aktiven single framebuffer
  zeichnen.
- Labels/Werte nur aktualisieren, wenn sie sich wirklich geaendert haben.
- Bei Artefakten zuerst pruefen: PSRAM aktiv, PCLK 16/14 MHz, PCLK invertiert,
  Porch-Werte, RGB-Pinreihenfolge.
- LovyanGFX-RGB mit Arduino-ESP32 `3.3.8` hier nicht als Standardweg verwenden.

## Quellen

- ESP3D, Sunton 4.3 ESP32-8048S043C: https://esp3d.io/esp3d-tft/version_1x/hardware/esp32-s3/sunton-43-8048/
- HomeDing Panel ESP32-8048S043C: https://homeding.github.io/boards/esp32s3/panel-8048S043.htm
- ESPHome Devices, Sunton ESP32-8048S043C: https://devices.esphome.io/devices/sunton-esp32-8048s043c/
- Espressif RGB LCD / esp_lcd: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html
- Espressif ESP32-S3-WROOM-1/WROOM-1U Datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
