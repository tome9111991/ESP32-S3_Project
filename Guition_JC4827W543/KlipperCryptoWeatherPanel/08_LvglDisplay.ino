#if LVGL_ALIGN_FLUSH_AREAS
// Invalidierte Bereiche auf gerade Start-/ungerade End-Koordinaten erweitern,
// falls der NV3041A bei ungeraden Fenstergrenzen Pixelfehler zeigt.
static void lvAlignInvalidAreaCb(lv_event_t* e) {
  lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
  if (area == nullptr) {
    return;
  }
  area->x1 &= ~1;
  area->y1 &= ~1;
  area->x2 |= 1;
  area->y2 |= 1;
}
#endif

void initLvglDisplay() {
  lv_init();
  lv_tick_set_cb(lvTickMillis);

  const uint32_t screenWidth = display.width();
  const uint32_t screenHeight = display.height();
  const uint32_t fullBufferPixels = screenWidth * screenHeight;
  const uint32_t fallbackPartialRows = 24;
  // RGB565 = 2 Byte/Pixel. Nicht sizeof(lv_color_t) nehmen: das ist in LVGL 9 RGB888 (3 Byte).
  const uint32_t bytesPerPixel = LV_COLOR_DEPTH / 8;
  uint32_t bufferBytes = 0;
  lv_display_render_mode_t renderMode = LV_DISPLAY_RENDER_MODE_PARTIAL;

  lvDrawBuf = nullptr;
  lvDrawBuf2 = nullptr; // kein Double-Buffer: NV3041A-Treiber schreibt ohnehin blockierend
  lvDrawBufDma = false;

#if LVGL_RENDER_PARTIAL
  // PARTIAL: LVGL rendert/uebertraegt nur die geaenderten Bereiche
  // (z.B. Sekundenbalken 256x4 statt 480x272 Vollbild jede Sekunde).
  // Puffer intern + DMA-faehig (schneller als PSRAM fuer den Render-Puffer).
  const uint32_t partialRowsOptions[] = { 40, 20 };
  for (uint32_t rows : partialRowsOptions) {
    bufferBytes = screenWidth * rows * bytesPerPixel;
    lvDrawBuf = (lv_color_t*)heap_caps_malloc(bufferBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (lvDrawBuf != nullptr) {
      lvDrawBufDma = true;
      break;
    }
  }
#endif

  if (lvDrawBuf == nullptr) {
    // FULL (bisheriges Verhalten) bzw. Fallback, falls kein interner DMA-Puffer frei ist.
    bufferBytes = fullBufferPixels * bytesPerPixel;
    renderMode = LV_DISPLAY_RENDER_MODE_FULL;
    lvDrawBuf = (lv_color_t*)heap_caps_malloc(bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lvDrawBufDma = false;
  }

  if (lvDrawBuf == nullptr) {
    // Letzter Fallback: kleiner Partial-Puffer in beliebigem RAM.
    bufferBytes = screenWidth * fallbackPartialRows * bytesPerPixel;
    renderMode = LV_DISPLAY_RENDER_MODE_PARTIAL;
    lvDrawBuf = (lv_color_t*)heap_caps_malloc(bufferBytes, MALLOC_CAP_8BIT);
    lvDrawBufDma = false;
  }

  if (lvDrawBuf == nullptr) {
    Serial.println("LVGL Buffer Init fehlgeschlagen!");
    while (true) {
      delay(1000);
    }
  }

  Serial.printf(
    "LVGL Buffer: %u Bytes (%u Zeilen), %s, %s%s, internalLargest danach=%u\n",
    (unsigned)bufferBytes,
    (unsigned)(bufferBytes / (screenWidth * bytesPerPixel)),
    renderMode == LV_DISPLAY_RENDER_MODE_PARTIAL ? "PARTIAL" : "FULL",
    lvDrawBufDma ? "DMA intern" : "nicht-DMA",
    lvDrawBuf2 != nullptr ? ", doppelt" : ", einfach",
    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
  );

  lvDisplay = lv_display_create(screenWidth, screenHeight);
  lv_display_set_flush_cb(lvDisplay, lvFlush);
  lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565_SWAPPED);
  lv_display_set_buffers(
    lvDisplay,
    lvDrawBuf,
    lvDrawBuf2,
    bufferBytes,
    renderMode
  );

#if LVGL_ALIGN_FLUSH_AREAS
  // Wird nur im PARTIAL-Mode ausgeloest (FULL zeichnet ohnehin immer alles).
  lv_display_add_event_cb(lvDisplay, lvAlignInvalidAreaCb, LV_EVENT_INVALIDATE_AREA, NULL);
  Serial.println("LVGL: Flush-Bereiche auf gerade Koordinaten ausgerichtet");
#endif
}
