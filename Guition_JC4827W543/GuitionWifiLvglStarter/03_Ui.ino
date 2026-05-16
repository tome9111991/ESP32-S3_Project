static lv_obj_t* createLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t color, lv_text_align_t align) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_remove_style_all(label);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(label, align, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
  return label;
}

void setStatusText(const char* text) {
  if (statusLabel != nullptr) {
    lv_label_set_text(statusLabel, text);
  }
}

void setWifiText(const char* text) {
  if (wifiLabel != nullptr) {
    lv_label_set_text(wifiLabel, text);
  }
}

void createUi() {
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  lv_obj_set_size(screen, display.width(), display.height());
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x0c1016), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  lv_obj_t* accent = lv_obj_create(screen);
  lv_obj_remove_style_all(accent);
  lv_obj_set_pos(accent, 0, 0);
  lv_obj_set_size(accent, display.width(), 5);
  lv_obj_set_style_bg_color(accent, lv_color_hex(0x3bd6a3), 0);
  lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);

  titleLabel = createLabel(screen, &lv_font_montserrat_32, 0xf4f7fb, LV_TEXT_ALIGN_LEFT);
  lv_label_set_text(titleLabel, "Guition WiFi LVGL Starter");
  lv_obj_set_pos(titleLabel, 24, 24);
  lv_obj_set_size(titleLabel, 430, 42);

  wifiLabel = createLabel(screen, &lv_font_montserrat_24, 0x3bd6a3, LV_TEXT_ALIGN_LEFT);
  lv_label_set_text(wifiLabel, "WLAN wartet");
  lv_obj_set_pos(wifiLabel, 24, 88);
  lv_obj_set_size(wifiLabel, 260, 32);

  ipLabel = createLabel(screen, &lv_font_montserrat_18, 0xaeb8c5, LV_TEXT_ALIGN_LEFT);
  lv_label_set_text(ipLabel, "IP: --");
  lv_obj_set_pos(ipLabel, 24, 126);
  lv_obj_set_size(ipLabel, 300, 26);

  timeLabel = createLabel(screen, &lv_font_montserrat_24, 0xf4f7fb, LV_TEXT_ALIGN_RIGHT);
  lv_label_set_text(timeLabel, "--:--:--");
  lv_obj_set_pos(timeLabel, 300, 88);
  lv_obj_set_size(timeLabel, 156, 32);

  heapLabel = createLabel(screen, &lv_font_montserrat_18, 0xaeb8c5, LV_TEXT_ALIGN_RIGHT);
  lv_label_set_text(heapLabel, "Heap: --");
  lv_obj_set_pos(heapLabel, 290, 126);
  lv_obj_set_size(heapLabel, 166, 26);

  statusLabel = createLabel(screen, &lv_font_montserrat_18, 0x7f8a99, LV_TEXT_ALIGN_LEFT);
  lv_label_set_text(statusLabel, "Startbereit");
  lv_obj_set_pos(statusLabel, 24, 212);
  lv_obj_set_size(statusLabel, 432, 26);

  lv_screen_load(screen);
  lv_obj_invalidate(screen);
}

void refreshUi() {
  if (wifiConnected) {
    setWifiText("WLAN verbunden");

    String ipText = "IP: " + WiFi.localIP().toString();
    lv_label_set_text(ipLabel, ipText.c_str());
  } else {
    setWifiText("WLAN getrennt");
    lv_label_set_text(ipLabel, "IP: --");
  }

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 5)) {
    char timeText[16];
    strftime(timeText, sizeof(timeText), "%H:%M:%S", &timeinfo);
    lv_label_set_text(timeLabel, timeText);
  } else {
    lv_label_set_text(timeLabel, "--:--:--");
  }

  char heapText[32];
  snprintf(heapText, sizeof(heapText), "Heap: %u KB", (unsigned)(ESP.getFreeHeap() / 1024));
  lv_label_set_text(heapLabel, heapText);
}
