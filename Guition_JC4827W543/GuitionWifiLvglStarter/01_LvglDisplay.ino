static uint32_t lvTickMillis() {
  return millis();
}

void lvFlush(lv_display_t* disp, const lv_area_t* area, uint8_t* pxMap) {
  int32_t x1 = area->x1;
  int32_t y1 = area->y1;
  int32_t x2 = area->x2;
  int32_t y2 = area->y2;

  if (x2 < 0 || y2 < 0 || x1 >= display.width() || y1 >= display.height()) {
    lv_display_flush_ready(disp);
    return;
  }

  const int32_t srcWidth = lv_area_get_width(area);
  if (x1 < 0) x1 = 0;
  if (y1 < 0) y1 = 0;
  if (x2 >= display.width()) x2 = display.width() - 1;
  if (y2 >= display.height()) y2 = display.height() - 1;

  const uint32_t w = x2 - x1 + 1;
  const uint32_t h = y2 - y1 + 1;
  const lgfx::swap565_t* pixels = reinterpret_cast<const lgfx::swap565_t*>(pxMap)
    + ((y1 - area->y1) * srcWidth)
    + (x1 - area->x1);
  const bool contiguous = (srcWidth == (int32_t)w);
  const bool startedWrite = (display.getStartCount() == 0);

  display.waitDMA();

  if (startedWrite) {
    display.startWrite();
  }

  if (contiguous) {
    display.pushImage(x1, y1, w, h, pixels);
  } else {
    for (uint32_t row = 0; row < h; row++) {
      display.pushImage(x1, y1 + row, w, 1, pixels + (row * srcWidth));
    }
  }

  if (startedWrite) {
    display.endWrite();
  }

  display.waitDMA();
  lv_display_flush_ready(disp);
}

void initLvglDisplay() {
  lv_init();
  lv_tick_set_cb(lvTickMillis);

  // The Guition/NV3041A setup is most stable with one full buffer in PSRAM.
  const uint32_t screenWidth = display.width();
  const uint32_t screenHeight = display.height();
  const uint32_t bufferBytes = screenWidth * screenHeight * sizeof(lv_color_t);
  lvDrawBuf = (lv_color_t*)heap_caps_malloc(bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (lvDrawBuf == nullptr) {
    Serial.println("LVGL Fullscreen-Buffer in PSRAM fehlgeschlagen!");
    while (true) {
      delay(1000);
    }
  }

  Serial.printf("LVGL Buffer: %u Bytes, FULL, PSRAM\n", (unsigned)bufferBytes);

  lvDisplay = lv_display_create(screenWidth, screenHeight);
  lv_display_set_flush_cb(lvDisplay, lvFlush);
  lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565_SWAPPED);
  lv_display_set_buffers(
    lvDisplay,
    lvDrawBuf,
    nullptr,
    bufferBytes,
    LV_DISPLAY_RENDER_MODE_FULL
  );
}
