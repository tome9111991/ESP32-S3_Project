# DisplayPixelRaster

Ein einfacher Orientierungs-Sketch fuer das Guition JC4827W543 / 4.3" ESP32-S3-HMI.

- Display: 480 x 272 Pixel
- Treiber: NV3041A ueber QSPI/SPI
- Rotation: 180 Grad, passend zum aktuellen Hauptprojekt
- Raster: 10 px Nebenlinien, 50 px Hauptlinien
- Markierungen: Displaykanten, Mittelpunkt `240,136`, Eckkoordinaten

Der Sketch nutzt LVGL mit `LV_DISPLAY_RENDER_MODE_FULL` und einem Fullscreen-Buffer in PSRAM. Das entspricht dem stabilen Renderpfad des Hauptprojekts.
