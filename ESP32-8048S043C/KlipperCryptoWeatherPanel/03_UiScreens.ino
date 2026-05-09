void createTimeScreen() {
  timeScreen = createScreen();
  timeAccent = createAccent(timeScreen, COLOR_CYAN);
  timeSunIcon = createSunStatusIcon(timeScreen);

  timeLocationLabel = createLabel(timeScreen, &lv_font_montserrat_30, COLOR_DIM, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(timeLocationLabel, 460, 40);
  lv_obj_set_pos(timeLocationLabel, 96, 38);
  lv_label_set_text(timeLocationLabel, weatherLocation.c_str());

  timeLabel = createLabel(timeScreen, &ui_font_time_digits_160, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(timeLabel, 620, 176);
  lv_obj_set_pos(timeLabel, 90, 102);
  lv_obj_set_style_text_outline_stroke_color(timeLabel, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_outline_stroke_width(timeLabel, 1, 0);
  lv_obj_set_style_text_outline_stroke_opa(timeLabel, LV_OPA_70, 0);

  weatherIconRoot = createWeatherImage(timeScreen);
  updateWeatherImage(weatherCode);

  timeDivider = createDivider(timeScreen, 220, 292, TIME_SECOND_BAR_W, COLOR_DIM);
  timeSecondFill = createDivider(timeDivider, 0, 0, 0, COLOR_CYAN);
  setHidden(timeSecondFill, true);

  weekdayLabel = createLabel(timeScreen, &lv_font_montserrat_40, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(weekdayLabel, 720, 52);
  lv_obj_align(weekdayLabel, LV_ALIGN_TOP_MID, 0, 322);

  dateLabel = createLabel(timeScreen, &lv_font_montserrat_30, COLOR_DIM, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(dateLabel, 720, 40);
  lv_obj_align(dateLabel, LV_ALIGN_TOP_MID, 0, 364);

  tempLabel = createLabel(timeScreen, &lv_font_montserrat_48, COLOR_CYAN, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(tempLabel, 720, 64);
  lv_obj_align(tempLabel, LV_ALIGN_TOP_MID, 6, 400);

  timeStatusTitle = createLabel(timeScreen, &lv_font_montserrat_48, COLOR_ORANGE, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(timeStatusTitle, 720, 64);
  lv_obj_align(timeStatusTitle, LV_ALIGN_CENTER, 0, -18);

  timeStatusDetail = createLabel(timeScreen, &lv_font_montserrat_30, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(timeStatusDetail, 720, 42);
  lv_obj_align(timeStatusDetail, LV_ALIGN_CENTER, 0, 38);
}

void createCryptoScreen() {
  cryptoScreen = createScreen();
  createAccent(cryptoScreen, COLOR_BTC);

  lv_obj_t* title = createLabel(cryptoScreen, &lv_font_montserrat_30, COLOR_DIM, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(title, 460, 40);
  lv_obj_set_pos(title, 96, 40);
  String titleText = cryptoPairTitle();
  lv_label_set_text(title, titleText.c_str());

  cryptoPriceLabel = createLabel(cryptoScreen, &lv_font_montserrat_48, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(cryptoPriceLabel, 720, 96);
  lv_obj_align(cryptoPriceLabel, LV_ALIGN_TOP_MID, 0, 138);

  createDivider(cryptoScreen, 210, 260, 380, COLOR_BTC);

  cryptoStatusLabel = createLabel(cryptoScreen, &lv_font_montserrat_30, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(cryptoStatusLabel, 720, 42);
  lv_obj_align(cryptoStatusLabel, LV_ALIGN_TOP_MID, 0, 316);
}

void createBtcDayScreen() {
  btcDayScreen = createScreen();
  createAccent(btcDayScreen, COLOR_BTC);

  lv_obj_t* title = createLabel(btcDayScreen, &lv_font_montserrat_30, COLOR_DIM, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(title, 390, 40);
  lv_obj_set_pos(title, 96, 40);
  String titleText = cryptoDayTitle();
  lv_label_set_text(title, titleText.c_str());

  btcDayChangeLabel = createLabel(btcDayScreen, &lv_font_montserrat_40, COLOR_MUTED, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_size(btcDayChangeLabel, 260, 50);
  lv_obj_set_pos(btcDayChangeLabel, 500, 36);

  btcDayPriceLabel = createLabel(btcDayScreen, &lv_font_montserrat_48, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(btcDayPriceLabel, 720, 64);
  lv_obj_align(btcDayPriceLabel, LV_ALIGN_TOP_MID, 0, 86);

  const size_t chartBufferBytes = LV_CANVAS_BUF_SIZE(BTC_CHART_W, BTC_CHART_CANVAS_H, 16, LV_DRAW_BUF_STRIDE_ALIGN);
  btcDayChartCanvasBuf = (uint8_t*)heap_caps_malloc(chartBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  btcDayChartCanvasBufInPsram = (btcDayChartCanvasBuf != nullptr);
  if (btcDayChartCanvasBuf == nullptr) {
    btcDayChartCanvasBuf = (uint8_t*)heap_caps_malloc(chartBufferBytes, MALLOC_CAP_8BIT);
    btcDayChartCanvasBufInPsram = false;
  }

  if (btcDayChartCanvasBuf != nullptr) {
    Serial.printf("BTC Chart Canvas Buffer: %u Bytes in %s\n", (unsigned)chartBufferBytes, btcDayChartCanvasBufInPsram ? "PSRAM" : "internem RAM");
    btcDayChartCanvas = lv_canvas_create(btcDayScreen);
    lv_canvas_set_buffer(btcDayChartCanvas, btcDayChartCanvasBuf, BTC_CHART_W, BTC_CHART_CANVAS_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(btcDayChartCanvas, 40, 142);
    lv_obj_clear_flag(btcDayChartCanvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_canvas_fill_bg(btcDayChartCanvas, lv_color_hex(COLOR_BG), LV_OPA_COVER);
  }

  btcDayRangeLabel = createLabel(btcDayScreen, &lv_font_montserrat_28, COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(btcDayRangeLabel, 360, 32);
  lv_obj_set_pos(btcDayRangeLabel, 40, 414);

  btcDayCandleLabel = createLabel(btcDayScreen, &lv_font_montserrat_28, COLOR_BTC, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_size(btcDayCandleLabel, 300, 32);
  lv_obj_set_pos(btcDayCandleLabel, 460, 414);

  btcDayVolumeLabel = createLabel(btcDayScreen, &lv_font_montserrat_28, COLOR_DIM, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(btcDayVolumeLabel, 720, 32);
  lv_obj_align(btcDayVolumeLabel, LV_ALIGN_TOP_MID, 0, 446);
}

void createKlipperScreen() {
  klipperScreen = createScreen();
  klipperAccent = createAccent(klipperScreen, COLOR_GREEN);

  klipperTitleLabel = createLabel(klipperScreen, &lv_font_montserrat_30, COLOR_DIM, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(klipperTitleLabel, 420, 40);
  lv_obj_set_pos(klipperTitleLabel, 96, 40);
  lv_label_set_text(klipperTitleLabel, "KLIPPER");

  klipperStateLabel = createLabel(klipperScreen, &lv_font_montserrat_48, COLOR_TEXT, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_size(klipperStateLabel, 260, 62);
  lv_obj_set_pos(klipperStateLabel, 500, 30);

  klipperProgressArc = lv_arc_create(klipperScreen);
  lv_obj_set_size(klipperProgressArc, 154, 154);
  lv_obj_align(klipperProgressArc, LV_ALIGN_TOP_MID, 0, 88);
  lv_arc_set_rotation(klipperProgressArc, 270);
  lv_arc_set_bg_angles(klipperProgressArc, 0, 360);
  lv_arc_set_range(klipperProgressArc, 0, 100);
  lv_arc_set_value(klipperProgressArc, 0);
  lv_obj_remove_style(klipperProgressArc, NULL, LV_PART_KNOB);
  lv_obj_remove_flag(klipperProgressArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(klipperProgressArc, LV_OPA_TRANSP, 0);
  lv_obj_set_style_arc_width(klipperProgressArc, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(klipperProgressArc, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(klipperProgressArc, lv_color_hex(COLOR_DIM), LV_PART_MAIN);
  lv_obj_set_style_arc_color(klipperProgressArc, lv_color_hex(COLOR_GREEN), LV_PART_INDICATOR);

  klipperProgressLabel = createLabel(klipperScreen, &lv_font_montserrat_48, COLOR_GREEN, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(klipperProgressLabel, 260, 66);
  lv_obj_align(klipperProgressLabel, LV_ALIGN_TOP_MID, 0, 130);

  klipperDivider = createDivider(klipperScreen, 210, 258, 380, COLOR_GREEN);
  setHidden(klipperDivider, true);

  klipperOfflineIcon = lv_image_create(klipperScreen);
  lv_image_set_src(klipperOfflineIcon, &icon_status_offline);
  lv_obj_set_size(klipperOfflineIcon, 147, 147);
  lv_obj_set_pos(klipperOfflineIcon, 94, 119);
  lv_obj_clear_flag(klipperOfflineIcon, LV_OBJ_FLAG_SCROLLABLE);
  setHidden(klipperOfflineIcon, true);

  klipperOfflineRing = lv_obj_create(klipperScreen);
  lv_obj_remove_style_all(klipperOfflineRing);
  lv_obj_set_size(klipperOfflineRing, 118, 118);
  lv_obj_set_pos(klipperOfflineRing, 108, 148);
  lv_obj_set_style_bg_opa(klipperOfflineRing, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(klipperOfflineRing, 8, 0);
  lv_obj_set_style_border_color(klipperOfflineRing, lv_color_hex(COLOR_DIM), 0);
  lv_obj_set_style_radius(klipperOfflineRing, 59, 0);
  lv_obj_clear_flag(klipperOfflineRing, LV_OBJ_FLAG_SCROLLABLE);
  setHidden(klipperOfflineRing, true);

  klipperOfflineStem = lv_obj_create(klipperScreen);
  styleFilledRect(klipperOfflineStem, COLOR_DIM, 3);
  lv_obj_set_size(klipperOfflineStem, 16, 58);
  lv_obj_set_pos(klipperOfflineStem, 159, 118);
  setHidden(klipperOfflineStem, true);

  klipperOfflineLine = createDivider(klipperScreen, 286, 204, 360, COLOR_DIM);
  setHidden(klipperOfflineLine, true);

  klipperFileLabel = createLabel(klipperScreen, &lv_font_montserrat_30, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(klipperFileLabel, 700, 42);
  lv_obj_align(klipperFileLabel, LV_ALIGN_TOP_MID, 0, 262);

  klipperNozzleTitleLabel = createLabel(klipperScreen, &lv_font_montserrat_30, COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(klipperNozzleTitleLabel, 120, 40);
  lv_obj_set_pos(klipperNozzleTitleLabel, 130, 320);
  lv_label_set_text(klipperNozzleTitleLabel, "Nozzle");

  klipperNozzleLabel = createLabel(klipperScreen, &lv_font_montserrat_30, COLOR_CYAN, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(klipperNozzleLabel, 180, 40);
  lv_obj_set_pos(klipperNozzleLabel, 250, 320);

  klipperBedTitleLabel = createLabel(klipperScreen, &lv_font_montserrat_30, COLOR_MUTED, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_size(klipperBedTitleLabel, 100, 40);
  lv_obj_set_pos(klipperBedTitleLabel, 440, 320);
  lv_label_set_text(klipperBedTitleLabel, "Bett");

  klipperBedLabel = createLabel(klipperScreen, &lv_font_montserrat_30, COLOR_CYAN, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_size(klipperBedLabel, 150, 40);
  lv_obj_set_pos(klipperBedLabel, 550, 320);

  klipperDurationLabel = createLabel(klipperScreen, &lv_font_montserrat_28, COLOR_DIM, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(klipperDurationLabel, 300, 36);
  lv_obj_set_pos(klipperDurationLabel, 130, 362);

  klipperStatusLabel = createLabel(klipperScreen, &lv_font_montserrat_28, COLOR_DIM, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_size(klipperStatusLabel, 340, 36);
  lv_obj_set_pos(klipperStatusLabel, 330, 362);

  klipperMmuLabel = createLabel(klipperScreen, &lv_font_montserrat_28, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(klipperMmuLabel, 700, 32);
  lv_obj_align(klipperMmuLabel, LV_ALIGN_TOP_MID, 0, 402);

  const int gateW = 70;
  const int gateH = 36;
  const int gateGap = 10;
  const int gateStartX = 85;
  for (int i = 0; i < MMU_GATE_MAX; i++) {
    klipperMmuGateBox[i] = lv_obj_create(klipperScreen);
    styleFilledRect(klipperMmuGateBox[i], COLOR_DIM, 3);
    lv_obj_set_size(klipperMmuGateBox[i], gateW, gateH);
    lv_obj_set_pos(klipperMmuGateBox[i], gateStartX + (i * (gateW + gateGap)), 438);
    lv_obj_set_style_border_width(klipperMmuGateBox[i], 0, 0);
    lv_obj_set_style_border_color(klipperMmuGateBox[i], lv_color_hex(COLOR_TEXT), 0);

    klipperMmuGateLabel[i] = createLabel(klipperMmuGateBox[i], &lv_font_montserrat_28, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(klipperMmuGateLabel[i], gateW, gateH);
    lv_obj_set_pos(klipperMmuGateLabel[i], 0, 0);
    String gateLabel = "T" + String(i);
    lv_label_set_text(klipperMmuGateLabel[i], gateLabel.c_str());
  }
}

void createBootScreen() {
  bootScreen = createScreen();
  lv_obj_t* bootLabel = createLabel(bootScreen, &lv_font_montserrat_48, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(bootLabel, 720, 64);
  lv_obj_align(bootLabel, LV_ALIGN_CENTER, 0, 0);
  lv_label_set_text(bootLabel, "Booting");
}

void createUi() {
  createBootScreen();
  createTimeScreen();
  createCryptoScreen();
  createBtcDayScreen();
  createKlipperScreen();
  lv_screen_load(bootScreen);
}

void refreshTimeUi() {
  if (!wifiConnected) {
    setTimeNormalVisible(false);
    updateTimeSecondProgress(-1);
    setHidden(timeSunIcon.root, true);
    lv_obj_set_style_text_color(timeStatusTitle, lv_color_hex(COLOR_RED), 0);
    setLabelTextIfChanged(timeStatusTitle, "WLAN verbindet");
    char statusText[16];
    snprintf(statusText, sizeof(statusText), "Status: %d", WiFi.status());
    setLabelTextIfChanged(timeStatusDetail, statusText);
    return;
  }

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  String temp = currentTemp;
  String status = weatherStatus;
  String location = weatherLocation;
  int code = weatherCode;
  xSemaphoreGive(dataMutex);

  struct tm timeinfo;
  if (!getLocalTimeFast(timeinfo)) {
    setTimeNormalVisible(true);
    setHidden(timeSunIcon.root, true);
    updateTimeSecondProgress(-1);
    updateWeatherImage(code);
    String tempText = temp + TEMP_UNIT;
    setLabelTextIfChanged(timeLocationLabel, location.c_str());
    setLabelTextIfChanged(timeLabel, "--:--");
    updateWeatherImagePositionForTime("--:--");
    setLabelTextIfChanged(weekdayLabel, "Zeit wird synchronisiert");
    setLabelTextIfChanged(dateLabel, status.c_str());
    setLabelTextIfChanged(tempLabel, tempText.c_str());
    return;
  }

  char timeStringBuff[10];
  char dateStringBuff[20];
  char weekdayDateStringBuff[40];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M", &timeinfo);
  strftime(dateStringBuff, sizeof(dateStringBuff), "%d.%m.%Y", &timeinfo);
  snprintf(
    weekdayDateStringBuff,
    sizeof(weekdayDateStringBuff),
    "%s | %s",
    WEEKDAYS_DE[timeinfo.tm_wday],
    dateStringBuff
  );

  setTimeNormalVisible(true);
  setHidden(dateLabel, true);
  updateSunStatusIcon(timeinfo);
  updateTimeSecondProgress(timeinfo.tm_sec);
  updateWeatherImage(code);
  String tempText = temp + TEMP_UNIT;
  setLabelTextIfChanged(timeLocationLabel, location.c_str());
  setLabelTextIfChanged(timeLabel, timeStringBuff);
  updateWeatherImagePositionForTime(timeStringBuff);
  setLabelTextIfChanged(weekdayLabel, weekdayDateStringBuff);
  setLabelTextIfChanged(tempLabel, tempText.c_str());
}

uint32_t readNextUtf8Codepoint(const char*& cursor) {
  const uint8_t first = (uint8_t)*cursor++;
  if ((first & 0x80) == 0) {
    return first;
  }
  if ((first & 0xe0) == 0xc0 && (*cursor & 0xc0) == 0x80) {
    uint32_t codepoint = ((uint32_t)(first & 0x1f) << 6) | ((uint8_t)*cursor++ & 0x3f);
    return codepoint;
  }
  if ((first & 0xf0) == 0xe0 && (cursor[0] & 0xc0) == 0x80 && (cursor[1] & 0xc0) == 0x80) {
    uint32_t codepoint = ((uint32_t)(first & 0x0f) << 12) |
      (((uint8_t)*cursor++ & 0x3f) << 6) |
      ((uint8_t)*cursor++ & 0x3f);
    return codepoint;
  }
  return first;
}

bool priceFontSupportsCodepoint(uint32_t codepoint) {
  if (codepoint == ' ' || codepoint == '$' || codepoint == '.' ||
      codepoint == ',' || codepoint == '-' || codepoint == '+') {
    return true;
  }
  if (codepoint >= '0' && codepoint <= '9') {
    return true;
  }
  return codepoint == 0x20ac || codepoint == 0x00a3 ||
    codepoint == 0x00a5 || codepoint == 0x20bf;
}

bool cryptoPriceCanUseLargeFont(const String& price) {
  bool hasDigit = false;
  const char* cursor = price.c_str();
  while (*cursor != '\0') {
    uint32_t codepoint = readNextUtf8Codepoint(cursor);
    if (codepoint >= '0' && codepoint <= '9') {
      hasDigit = true;
    }
    if (!priceFontSupportsCodepoint(codepoint)) {
      return false;
    }
  }
  return hasDigit;
}

void setCryptoPriceText(const String& price) {
  const lv_font_t* font = cryptoPriceCanUseLargeFont(price) ?
    &ui_font_price_digits_80 :
    &lv_font_montserrat_48;
  lv_obj_set_style_text_font(cryptoPriceLabel, font, 0);
  setLabelTextIfChanged(cryptoPriceLabel, price.c_str());
}

void refreshCryptoUi() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  String price = currentBtcPrice;
  String status = currentBtcStatus;
  xSemaphoreGive(dataMutex);

  setCryptoPriceText(price);
  setLabelTextIfChanged(cryptoStatusLabel, status.c_str());
  lv_obj_set_style_text_color(
    cryptoStatusLabel,
    lv_color_hex(status.startsWith(cryptoServiceName) ? COLOR_MUTED : COLOR_RED),
    0
  );
}

String formatBtcCandleCountdown(uint32_t candleTime) {
  time_t nowTime = time(nullptr);
  if (nowTime <= 100000 || candleTime == 0) {
    return "LIVE";
  }

  int elapsed = (int)(nowTime - candleTime);
  if (elapsed < 0) {
    elapsed = 0;
  }
  if (elapsed >= (int)BTC_CANDLE_SECONDS) {
    elapsed = (int)BTC_CANDLE_SECONDS - 1;
  }

  int remaining = (int)BTC_CANDLE_SECONDS - elapsed;
  int remainingHours = remaining / 3600;
  int remainingMinutes = (remaining % 3600) / 60;
  if (remainingHours > 0) {
    return "LIVE " + String(remainingHours) + "H " + String(remainingMinutes) + "M";
  }
  return "LIVE " + String(max(1, remainingMinutes)) + "M";
}

void refreshBtcDayUi() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  String price = currentBtcPrice;
  String change = btcDayChange;
  String range = btcDayRange;
  String volume = btcDayVolume;
  String candle = btcCandleStatus;
  bool positive = btcDayChangePositive;
  bool ready = btcDayDataReady;
  int priceDirection = currentBtcPriceDirection;
  uint32_t candleTime = (btcCandles != nullptr && btcCandleCount > 0) ? btcCandles[btcCandleCount - 1].time : 0;
  xSemaphoreGive(dataMutex);

  if (ready) {
    candle = formatBtcCandleCountdown(candleTime);
  }

  String chartPrice = price;
  if (priceDirection > 0) {
    chartPrice = String(LV_SYMBOL_UP " ") + price;
  } else if (priceDirection < 0) {
    chartPrice = String(LV_SYMBOL_DOWN " ") + price;
  }

  setLabelTextIfChanged(btcDayPriceLabel, chartPrice.c_str());
  setLabelTextIfChanged(btcDayChangeLabel, change.c_str());
  setLabelTextIfChanged(btcDayRangeLabel, range.c_str());
  setLabelTextIfChanged(btcDayVolumeLabel, volume.c_str());
  setLabelTextIfChanged(btcDayCandleLabel, candle.c_str());
  lv_obj_set_style_text_color(
    btcDayPriceLabel,
    lv_color_hex(priceDirection > 0 ? COLOR_GREEN : (priceDirection < 0 ? COLOR_LOSS : COLOR_TEXT)),
    0
  );
  lv_obj_set_style_text_color(
    btcDayChangeLabel,
    lv_color_hex(!ready ? COLOR_MUTED : (positive ? COLOR_GREEN : COLOR_LOSS)),
    0
  );
}

void setKlipperOfflineLayout(uint32_t stateColor) {
  lv_obj_set_style_bg_color(klipperAccent, lv_color_hex(stateColor), 0);
  setHidden(klipperProgressArc, true);
  setHidden(klipperDivider, true);
  setHidden(klipperOfflineIcon, false);
  setHidden(klipperOfflineRing, true);
  setHidden(klipperOfflineStem, true);
  setHidden(klipperOfflineLine, false);
  lv_obj_set_style_border_color(klipperOfflineRing, lv_color_hex(stateColor), 0);
  lv_obj_set_style_bg_color(klipperOfflineStem, lv_color_hex(stateColor), 0);
  lv_obj_set_style_bg_color(klipperOfflineLine, lv_color_hex(stateColor), 0);

  lv_obj_set_size(klipperProgressLabel, 360, 72);
  lv_obj_set_pos(klipperProgressLabel, 300, 144);
  lv_obj_set_size(klipperFileLabel, 460, 96);
  lv_obj_set_pos(klipperFileLabel, 260, 226);
  lv_label_set_long_mode(klipperFileLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_size(klipperDurationLabel, 280, 36);
  lv_obj_set_pos(klipperDurationLabel, 160, 342);
  lv_obj_set_size(klipperStatusLabel, 300, 36);
  lv_obj_set_pos(klipperStatusLabel, 360, 342);
  lv_obj_set_size(klipperMmuLabel, 700, 32);
  lv_obj_align(klipperMmuLabel, LV_ALIGN_TOP_MID, 0, 392);

  setHidden(klipperNozzleTitleLabel, true);
  setHidden(klipperNozzleLabel, true);
  setHidden(klipperBedTitleLabel, true);
  setHidden(klipperBedLabel, true);
  setHidden(klipperDurationLabel, false);
  setHidden(klipperStatusLabel, false);
  setHidden(klipperMmuLabel, false);
}

void setKlipperOnlineLayout() {
  lv_obj_set_style_bg_color(klipperAccent, lv_color_hex(COLOR_GREEN), 0);
  setHidden(klipperProgressArc, false);
  lv_obj_set_size(klipperProgressArc, 154, 154);
  lv_obj_align(klipperProgressArc, LV_ALIGN_TOP_MID, 0, 88);
  setHidden(klipperDivider, true);
  setHidden(klipperOfflineIcon, true);
  setHidden(klipperOfflineRing, true);
  setHidden(klipperOfflineStem, true);
  setHidden(klipperOfflineLine, true);

  lv_obj_set_size(klipperProgressLabel, 260, 66);
  lv_obj_align(klipperProgressLabel, LV_ALIGN_TOP_MID, 0, 130);
  lv_obj_set_size(klipperFileLabel, 700, 42);
  lv_obj_align(klipperFileLabel, LV_ALIGN_TOP_MID, 0, 262);
  lv_label_set_long_mode(klipperFileLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_size(klipperDurationLabel, 300, 36);
  lv_obj_set_pos(klipperDurationLabel, 130, 362);
  lv_obj_set_size(klipperStatusLabel, 340, 36);
  lv_obj_set_pos(klipperStatusLabel, 330, 362);
  lv_obj_set_size(klipperMmuLabel, 700, 32);
  lv_obj_align(klipperMmuLabel, LV_ALIGN_TOP_MID, 0, 402);

  setHidden(klipperNozzleTitleLabel, false);
  setHidden(klipperNozzleLabel, false);
  setHidden(klipperBedTitleLabel, false);
  setHidden(klipperBedLabel, false);
  setHidden(klipperDurationLabel, false);
  setHidden(klipperStatusLabel, false);
  setHidden(klipperMmuLabel, false);
}

int progressPercentFromText(const String& progress) {
  String text = progress;
  text.trim();
  if (!text.endsWith("%")) {
    return -1;
  }

  text.remove(text.length() - 1);
  text.trim();
  if (text.length() == 0) {
    return -1;
  }

  int percent = text.toInt();
  return constrain(percent, 0, 100);
}

void refreshKlipperUi() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  bool available = klipperAvailable;
  bool hostAvailable = klipperHostAvailable;
  String connectionState = klipperConnectionState;
  String connectionMessage = klipperConnectionMessage;
  String state = klipperState;
  String file = klipperFile;
  String progress = klipperProgress;
  String nozzle = klipperNozzle;
  String bed = klipperBed;
  String duration = klipperDuration;
  String status = klipperStatus;
  String displayMessage = klipperDisplayMessage;
  String printerName = klipperPrinterName;
  bool mmuAvailable = klipperMmuAvailable;
  String mmuInfo = klipperMmuInfo;
  int mmuGate = klipperMmuGate;
  int mmuGateCount = klipperMmuGateCount;
  uint32_t mmuGateColors[MMU_GATE_MAX];
  int mmuGateStatus[MMU_GATE_MAX];
  for (int i = 0; i < MMU_GATE_MAX; i++) {
    mmuGateColors[i] = klipperMmuGateColors[i];
    mmuGateStatus[i] = klipperMmuGateStatus[i];
  }
  xSemaphoreGive(dataMutex);

  if (!available) {
    String stateText = hostAvailable ? formatKlippyConnectionState(connectionState) : "OFFLINE";
    String detailText = hostAvailable ? klippyOfflineDetail(connectionState, connectionMessage) : "Moonraker nicht erreichbar";
    uint32_t stateColor = COLOR_ORANGE;
    if (!hostAvailable || connectionState.equalsIgnoreCase("error")) {
      stateColor = COLOR_LOSS;
    } else if (connectionState.equalsIgnoreCase("shutdown") || connectionState.equalsIgnoreCase("disconnected")) {
      stateColor = COLOR_DIM;
    }

    setKlipperOfflineLayout(stateColor);
    setLabelTextIfChanged(klipperTitleLabel, hostAvailable ? printerName.c_str() : "KLIPPER");
    setLabelTextIfChanged(klipperStateLabel, stateText.c_str());
    setLabelTextIfChanged(klipperProgressLabel, stateText.c_str());
    setLabelTextIfChanged(klipperFileLabel, detailText.c_str());
    setLabelTextIfChanged(klipperDurationLabel, hostAvailable ? "MAINSAIL OK" : "MAINSAIL --");
    setLabelTextIfChanged(klipperStatusLabel, status.c_str());
    setLabelTextIfChanged(klipperMmuLabel, hostAvailable ? "Drucker einschalten" : "Warte auf Mainsail");

    lv_obj_set_style_text_color(klipperStateLabel, lv_color_hex(stateColor), 0);
    lv_obj_set_style_text_color(klipperProgressLabel, lv_color_hex(stateColor), 0);
    lv_obj_set_style_text_color(klipperStatusLabel, lv_color_hex(hostAvailable ? COLOR_MUTED : COLOR_LOSS), 0);

    for (int i = 0; i < MMU_GATE_MAX; i++) {
      setHidden(klipperMmuGateBox[i], true);
    }
    return;
  }

  setKlipperOnlineLayout();
  setLabelTextIfChanged(klipperNozzleTitleLabel, "Nozzle");
  setLabelTextIfChanged(klipperBedTitleLabel, "Bett");
  setLabelTextIfChanged(klipperStateLabel, state.c_str());
  setLabelTextIfChanged(klipperTitleLabel, printerName.c_str());
  setLabelTextIfChanged(klipperFileLabel, file.c_str());
  setLabelTextIfChanged(klipperProgressLabel, progress.c_str());
  setLabelTextIfChanged(klipperNozzleLabel, nozzle.c_str());
  setLabelTextIfChanged(klipperBedLabel, bed.c_str());
  setLabelTextIfChanged(klipperDurationLabel, duration.c_str());
  setLabelTextIfChanged(klipperStatusLabel, displayMessage.length() > 0 ? displayMessage.c_str() : status.c_str());
  setLabelTextIfChanged(klipperMmuLabel, mmuAvailable ? mmuInfo.c_str() : "MMU nicht aktiv");

  uint32_t stateColor = COLOR_GREEN;
  if (state == "PAUSE") {
    stateColor = COLOR_ORANGE;
  } else if (state == "FEHLER") {
    stateColor = COLOR_LOSS;
  } else if (state == "BEREIT" || state == "STANDBY") {
    stateColor = COLOR_MUTED;
  }

  lv_obj_set_style_text_color(klipperStateLabel, lv_color_hex(stateColor), 0);
  lv_obj_set_style_text_color(klipperProgressLabel, lv_color_hex(stateColor), 0);
  int progressPercent = progressPercentFromText(progress);
  setHidden(klipperProgressArc, progressPercent < 0);
  if (progressPercent >= 0) {
    lv_arc_set_value(klipperProgressArc, progressPercent);
    lv_obj_set_style_arc_color(klipperProgressArc, lv_color_hex(COLOR_DIM), LV_PART_MAIN);
    lv_obj_set_style_arc_color(klipperProgressArc, lv_color_hex(stateColor), LV_PART_INDICATOR);
  }
  lv_obj_set_style_text_color(klipperNozzleLabel, lv_color_hex(COLOR_CYAN), 0);
  lv_obj_set_style_text_color(klipperBedLabel, lv_color_hex(COLOR_CYAN), 0);
  lv_obj_set_style_text_color(klipperStatusLabel, lv_color_hex(displayMessage.length() > 0 ? COLOR_CYAN : COLOR_DIM), 0);

  for (int i = 0; i < MMU_GATE_MAX; i++) {
    bool gateVisible = mmuAvailable && i < mmuGateCount;
    setHidden(klipperMmuGateBox[i], !gateVisible);
    if (!gateVisible) {
      continue;
    }

    uint32_t gateColor = mmuGateStatus[i] == 0 ? COLOR_DIM : mmuGateColors[i];
    lv_obj_set_style_bg_color(klipperMmuGateBox[i], lv_color_hex(gateColor), 0);
    lv_obj_set_style_border_width(klipperMmuGateBox[i], i == mmuGate ? 2 : 0, 0);

    uint8_t r = (gateColor >> 16) & 0xff;
    uint8_t g = (gateColor >> 8) & 0xff;
    uint8_t b = gateColor & 0xff;
    uint32_t labelColor = ((uint16_t)r + (uint16_t)g + (uint16_t)b) > 380 ? COLOR_BG : COLOR_TEXT;
    lv_obj_set_style_text_color(klipperMmuGateLabel[i], lv_color_hex(labelColor), 0);
  }
}

void refreshUi() {
  switch (currentScreen) {
    case SCREEN_CRYPTO:
      refreshCryptoUi();
      break;
    case SCREEN_BTC_DAY:
      refreshBtcDayUi();
      break;
    case SCREEN_KLIPPER:
      refreshKlipperUi();
      break;
    case SCREEN_TIME:
    default:
      refreshTimeUi();
      break;
  }
}

bool isKlipperScreenAvailable() {
  if (!wifiConnected) {
    return false;
  }

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  bool available = klipperAvailable || klipperHostAvailable;
  xSemaphoreGive(dataMutex);
  return available;
}

lv_obj_t* screenForState(ScreenState state) {
  if (state == SCREEN_CRYPTO) {
    return cryptoScreen;
  }
  if (state == SCREEN_BTC_DAY) {
    return btcDayScreen;
  }
  if (state == SCREEN_KLIPPER) {
    return klipperScreen;
  }
  return timeScreen;
}

ScreenState nextScreenState(ScreenState state) {
  if (state == SCREEN_TIME) {
    return SCREEN_CRYPTO;
  }
  if (state == SCREEN_CRYPTO) {
    return SCREEN_BTC_DAY;
  }
  if (state == SCREEN_BTC_DAY) {
    return isKlipperScreenAvailable() ? SCREEN_KLIPPER : SCREEN_TIME;
  }
  return SCREEN_TIME;
}

ScreenState previousScreenState(ScreenState state) {
  if (state == SCREEN_TIME) {
    return isKlipperScreenAvailable() ? SCREEN_KLIPPER : SCREEN_BTC_DAY;
  }
  if (state == SCREEN_KLIPPER) {
    return SCREEN_BTC_DAY;
  }
  if (state == SCREEN_BTC_DAY) {
    return SCREEN_CRYPTO;
  }
  return SCREEN_TIME;
}

void switchScreen(ScreenState nextScreen) {
  if (nextScreen == SCREEN_KLIPPER && !isKlipperScreenAvailable()) {
    nextScreen = SCREEN_TIME;
  }

  lv_obj_t* targetScreen = screenForState(nextScreen);
  if (nextScreen == currentScreen && lv_screen_active() == targetScreen) {
    return;
  }

  currentScreen = nextScreen;
  refreshUi();
  lv_screen_load_anim(
    targetScreen,
    LV_SCREEN_LOAD_ANIM_FADE_IN,
    screenTransitionDuration,
    0,
    false
  );
  if (currentScreen == SCREEN_BTC_DAY) {
    lastBtcChartDraw = millis();
  }
}

void switchScreenFromTouch(ScreenState nextScreen) {
  lastScreenSwitch = millis();
  switchScreen(nextScreen);
}
