void initLvglDisplay() {
  lv_init();
  lv_tick_set_cb(lvTickMillis);

  const uint32_t screenWidth = display.width();
  const uint32_t screenHeight = display.height();
  const uint32_t fullBufferPixels = screenWidth * screenHeight;
  const uint32_t partialBufferRows = 24;
  const uint32_t partialBufferPixels = screenWidth * partialBufferRows;
  uint32_t bufferBytes = fullBufferPixels * sizeof(lv_color_t);
  lv_display_render_mode_t renderMode = LV_DISPLAY_RENDER_MODE_FULL;

  lvDrawBuf = (lv_color_t*)heap_caps_malloc(bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  lvDrawBuf2 = nullptr;
  lvDrawBufDma = false;

  if (lvDrawBuf == nullptr) {
    bufferBytes = partialBufferPixels * sizeof(lv_color_t);
    renderMode = LV_DISPLAY_RENDER_MODE_PARTIAL;
    lvDrawBuf = (lv_color_t*)heap_caps_malloc(bufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lvDrawBuf2 = (lv_color_t*)heap_caps_malloc(bufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  if (lvDrawBuf == nullptr) {
    if (lvDrawBuf2 != nullptr) {
      heap_caps_free(lvDrawBuf2);
      lvDrawBuf2 = nullptr;
    }

    bufferBytes = partialBufferPixels * sizeof(lv_color_t);
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
    "LVGL Buffer: %u Bytes, %s, %s%s\n",
    (unsigned)bufferBytes,
    renderMode == LV_DISPLAY_RENDER_MODE_PARTIAL ? "PARTIAL" : "FULL",
    lvDrawBufDma ? "DMA intern" : "nicht-DMA",
    lvDrawBuf2 != nullptr ? ", doppelt" : ", einfach"
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
}
