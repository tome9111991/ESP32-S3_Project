# Display-Spezifikation und Setup-Hinweise Guition JC4827W543

Stand: 2026-05-02

## Board / Display

- Board: Guition JC4827W543 / 4.3" ESP32-S3-HMI
- MCU: ESP32-S3-WROOM-1 N4R8
- Display: 4.3 Zoll, 480 x 272 Pixel
- Display-Treiber: NV3041A
- Display-Anbindung: QSPI/SPI, nicht RGB-DE/VSYNC/HSYNC/PCLK
- Farbtiefe: RGB565 / 16-bit
- Rotation im aktuellen Setup: `display.setRotation(2)` / 180 Grad

## Aktuell stabile Display-Pins

- SCLK: GPIO 47
- IO0: GPIO 21
- IO1: GPIO 48
- IO2: GPIO 40
- IO3: GPIO 39
- CS: GPIO 45
- RST: GPIO 4
- BL: GPIO 1

## Stabile Render-Einstellung

Dieses Board/Display-Setup ist empfindlich bei Teilupdates. Der aktuell stabile Weg ist:

- LVGL verwenden
- Fullscreen-Buffer in PSRAM allozieren:
  `heap_caps_malloc(bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`
- `LV_DISPLAY_RENDER_MODE_FULL` verwenden
- `LV_COLOR_FORMAT_RGB565_SWAPPED` verwenden
- Flush-Pfad wie im Hauptprojekt beibehalten
- SPI/QSPI-Takt konservativ bei `32 MHz` lassen

Der Test mit `DisplayPixelRaster` hat bestaetigt: direkte LovyanGFX-Zeichnung von vielen Linien/Texten kann auf diesem NV3041A/QSPI-Setup sichtbare Artefakte bzw. pixelige Fehler erzeugen. Mit LVGL Fullscreen-Buffer in PSRAM war das Raster sauber.

## Vorsicht bei Optimierungen

Nicht leichtfertig auf LVGL Partial Rendering mit DMA-Teilbuffer umstellen. In diesem Setup traten dabei spaeter sichtbare Pixelartefakte auf, obwohl der Screen zuerst sauber gezeichnet war.

Wahrscheinliche Ursachen-Kombination:

- NV3041A-QSPI-Treiber
- RGB565-Swap
- LVGL-Teilinvalidierungen
- Flush-/Window-Handling

Robuste Optimierungen sind eher auf UI-Ebene sinnvoll:

- nur aktiven Screen refreshen
- Labeltexte nur setzen, wenn sich der Text wirklich geaendert hat
- keine aggressiven Display-/SPI-Aenderungen ohne konkreten Testgrund

## Referenz-Sketch

Der Ordner `DisplayPixelRaster` enthaelt einen Orientierungs-Sketch mit:

- 10px Raster
- 50px Hauptlinien
- Koordinatenmarkierungen
- LVGL Fullscreen-Buffer in PSRAM
- demselben stabilen Display-Setup wie das Hauptprojekt
