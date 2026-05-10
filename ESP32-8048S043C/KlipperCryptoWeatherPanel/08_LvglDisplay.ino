void initLvglDisplay() {
  lv_init();
  lv_tick_set_cb(lvTickMillis);

  const uint32_t screenWidth = LCD_W;
  const uint32_t screenHeight = LCD_H;
  const uint32_t colorBytesPerPixel = 2; // RGB565
  const uint32_t partialBufferRows = 20;
  const uint32_t partialBufferPixels = screenWidth * partialBufferRows;
  uint32_t bufferBytes = partialBufferPixels * colorBytesPerPixel;
  lv_display_render_mode_t renderMode = LV_DISPLAY_RENDER_MODE_PARTIAL;

  bool lvDrawBufInPsram = false;
  lvDrawBuf = (lv_color_t*)heap_caps_malloc(bufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (lvDrawBuf == nullptr) {
    Serial.println("LVGL Buffer DRAM Fallback auf PSRAM");
    lvDrawBuf = (lv_color_t*)heap_caps_malloc(bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lvDrawBufInPsram = (lvDrawBuf != nullptr);
  }

  if (lvDrawBuf == nullptr) {
    Serial.println("LVGL Buffer Init fehlgeschlagen!");
    while (true) {
      delay(1000);
    }
  }

  lvDrawBufBytes = bufferBytes;
  lvDrawBufFullMode = (renderMode == LV_DISPLAY_RENDER_MODE_FULL);

  Serial.printf(
    "LVGL Buffer: %u Bytes, %s, %s, einfach\n",
    (unsigned)bufferBytes,
    renderMode == LV_DISPLAY_RENDER_MODE_PARTIAL ? "PARTIAL" : "FULL",
    lvDrawBufInPsram ? "PSRAM" : "DRAM"
  );

  lvDisplay = lv_display_create(screenWidth, screenHeight);
  lv_display_set_flush_cb(lvDisplay, lvFlush);
  lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(
    lvDisplay,
    lvDrawBuf,
    nullptr,
    bufferBytes,
    renderMode
  );
}
