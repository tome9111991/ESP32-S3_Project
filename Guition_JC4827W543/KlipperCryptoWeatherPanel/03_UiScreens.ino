void createTimeScreen() {
  timeScreen = createScreen();
  timeAccent = createAccent(timeScreen, COLOR_CYAN);
  timeSunIcon = createSunStatusIcon(timeScreen);

  timeLocationLabel = createLabel(timeScreen, &lv_font_montserrat_18, COLOR_DIM, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(timeLocationLabel, 220, 24);
  lv_obj_set_pos(timeLocationLabel, 60, 20);
  lv_label_set_text(timeLocationLabel, "Leipzig");

  timeLabel = createLabel(timeScreen, &lv_font_montserrat_48, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(timeLabel, 220, 64);
  lv_obj_align(timeLabel, LV_ALIGN_TOP_MID, 0, 65);

  weatherIconRoot = createWeatherImage(timeScreen);
  updateWeatherImage(weatherCode);

  timeDivider = createDivider(timeScreen, 112, 164, TIME_SECOND_BAR_W, COLOR_DIM);
  timeSecondFill = createDivider(timeDivider, 0, 0, 0, COLOR_CYAN);
  setHidden(timeSecondFill, true);

  weekdayLabel = createLabel(timeScreen, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(weekdayLabel, 440, 34);
  lv_obj_align(weekdayLabel, LV_ALIGN_TOP_MID, 0, 176);

  dateLabel = createLabel(timeScreen, &lv_font_montserrat_18, COLOR_DIM, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(dateLabel, 440, 26);
  lv_obj_align(dateLabel, LV_ALIGN_TOP_MID, 0, 207);

  tempLabel = createLabel(timeScreen, &lv_font_montserrat_32, COLOR_CYAN, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(tempLabel, 440, 42);
  lv_obj_align(tempLabel, LV_ALIGN_TOP_MID, 0, 228);

  timeStatusTitle = createLabel(timeScreen, &lv_font_montserrat_32, COLOR_ORANGE, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(timeStatusTitle, 440, 44);
  lv_obj_align(timeStatusTitle, LV_ALIGN_CENTER, 0, -18);

  timeStatusDetail = createLabel(timeScreen, &lv_font_montserrat_18, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(timeStatusDetail, 440, 28);
  lv_obj_align(timeStatusDetail, LV_ALIGN_CENTER, 0, 28);
}

void createCryptoScreen() {
  cryptoScreen = createScreen();
  createAccent(cryptoScreen, COLOR_BTC);

  lv_obj_t* title = createLabel(cryptoScreen, &lv_font_montserrat_18, COLOR_DIM, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(title, 220, 24);
  lv_obj_set_pos(title, 60, 20);
  String titleText = cryptoPairTitle();
  lv_label_set_text(title, titleText.c_str());

  cryptoPriceLabel = createLabel(cryptoScreen, &lv_font_montserrat_40, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(cryptoPriceLabel, 440, 58);
  lv_obj_align(cryptoPriceLabel, LV_ALIGN_TOP_MID, 0, 96);

  createDivider(cryptoScreen, 88, 174, 304, COLOR_BTC);

  cryptoStatusLabel = createLabel(cryptoScreen, &lv_font_montserrat_18, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(cryptoStatusLabel, 440, 28);
  lv_obj_align(cryptoStatusLabel, LV_ALIGN_TOP_MID, 0, 210);
}

void createBtcDayScreen() {
  btcDayScreen = createScreen();
  createAccent(btcDayScreen, COLOR_BTC);

  lv_obj_t* title = createLabel(btcDayScreen, &lv_font_montserrat_18, COLOR_DIM, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(title, 180, 24);
  lv_obj_set_pos(title, 60, 20);
  String titleText = cryptoDayTitle();
  lv_label_set_text(title, titleText.c_str());

  btcDayChangeLabel = createLabel(btcDayScreen, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_size(btcDayChangeLabel, 180, 32);
  lv_obj_set_pos(btcDayChangeLabel, 278, 16);

  btcDayPriceLabel = createLabel(btcDayScreen, &lv_font_montserrat_32, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(btcDayPriceLabel, 440, 42);
  lv_obj_align(btcDayPriceLabel, LV_ALIGN_TOP_MID, 0, 38);

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
    lv_obj_set_pos(btcDayChartCanvas, 34, 84);
    lv_obj_clear_flag(btcDayChartCanvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_canvas_fill_bg(btcDayChartCanvas, lv_color_hex(COLOR_BG), LV_OPA_COVER);
  }

  btcDayRangeLabel = createLabel(btcDayScreen, &lv_font_montserrat_16, COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(btcDayRangeLabel, 220, 24);
  lv_obj_set_pos(btcDayRangeLabel, 34, 214);

  btcDayCandleLabel = createLabel(btcDayScreen, &lv_font_montserrat_16, COLOR_BTC, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_size(btcDayCandleLabel, 180, 24);
  lv_obj_set_pos(btcDayCandleLabel, 266, 214);

  btcDayVolumeLabel = createLabel(btcDayScreen, &lv_font_montserrat_16, COLOR_DIM, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(btcDayVolumeLabel, 440, 24);
  lv_obj_align(btcDayVolumeLabel, LV_ALIGN_TOP_MID, 0, 240);
}

void createKlipperScreen() {
  klipperScreen = createScreen();
  klipperAccent = createAccent(klipperScreen, COLOR_GREEN);

  klipperTitleLabel = createLabel(klipperScreen, &lv_font_montserrat_18, COLOR_DIM, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(klipperTitleLabel, 220, 24);
  lv_obj_set_pos(klipperTitleLabel, 60, 20);
  lv_label_set_text(klipperTitleLabel, "KLIPPER");

  klipperStateLabel = createLabel(klipperScreen, &lv_font_montserrat_32, COLOR_TEXT, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_size(klipperStateLabel, 190, 40);
  lv_obj_set_pos(klipperStateLabel, 268, 14);

  klipperProgressArc = lv_arc_create(klipperScreen);
  lv_obj_set_size(klipperProgressArc, 102, 102);
  lv_obj_align(klipperProgressArc, LV_ALIGN_TOP_MID, 0, 36);
  lv_arc_set_rotation(klipperProgressArc, 270);
  lv_arc_set_bg_angles(klipperProgressArc, 0, 360);
  lv_arc_set_range(klipperProgressArc, 0, 100);
  lv_arc_set_value(klipperProgressArc, 0);
  lv_obj_remove_style(klipperProgressArc, NULL, LV_PART_KNOB);
  lv_obj_remove_flag(klipperProgressArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(klipperProgressArc, LV_OPA_TRANSP, 0);
  lv_obj_set_style_arc_width(klipperProgressArc, 7, LV_PART_MAIN);
  lv_obj_set_style_arc_width(klipperProgressArc, 7, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(klipperProgressArc, lv_color_hex(COLOR_DIM), LV_PART_MAIN);
  lv_obj_set_style_arc_color(klipperProgressArc, lv_color_hex(COLOR_GREEN), LV_PART_INDICATOR);

  klipperProgressLabel = createLabel(klipperScreen, &lv_font_montserrat_40, COLOR_GREEN, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(klipperProgressLabel, 180, 54);
  lv_obj_align(klipperProgressLabel, LV_ALIGN_TOP_MID, 0, 61);

  klipperDivider = createDivider(klipperScreen, 112, 132, 256, COLOR_GREEN);
  setHidden(klipperDivider, true);

  klipperOfflineRing = lv_obj_create(klipperScreen);
  lv_obj_remove_style_all(klipperOfflineRing);
  lv_obj_set_size(klipperOfflineRing, 88, 88);
  lv_obj_set_pos(klipperOfflineRing, 62, 72);
  lv_obj_set_style_bg_opa(klipperOfflineRing, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(klipperOfflineRing, 6, 0);
  lv_obj_set_style_border_color(klipperOfflineRing, lv_color_hex(COLOR_DIM), 0);
  lv_obj_set_style_radius(klipperOfflineRing, 44, 0);
  lv_obj_clear_flag(klipperOfflineRing, LV_OBJ_FLAG_SCROLLABLE);
  setHidden(klipperOfflineRing, true);

  klipperOfflineStem = lv_obj_create(klipperScreen);
  styleFilledRect(klipperOfflineStem, COLOR_DIM, 3);
  lv_obj_set_size(klipperOfflineStem, 12, 42);
  lv_obj_set_pos(klipperOfflineStem, 100, 58);
  setHidden(klipperOfflineStem, true);

  klipperOfflineLine = createDivider(klipperScreen, 178, 142, 230, COLOR_DIM);
  setHidden(klipperOfflineLine, true);

  klipperFileLabel = createLabel(klipperScreen, &lv_font_montserrat_18, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(klipperFileLabel, 420, 28);
  lv_obj_align(klipperFileLabel, LV_ALIGN_TOP_MID, 0, 146);

  klipperNozzleTitleLabel = createLabel(klipperScreen, &lv_font_montserrat_18, COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(klipperNozzleTitleLabel, 74, 30);
  lv_obj_set_pos(klipperNozzleTitleLabel, 60, 178);
  lv_label_set_text(klipperNozzleTitleLabel, "Nozzle");

  klipperNozzleLabel = createLabel(klipperScreen, &lv_font_montserrat_18, COLOR_CYAN, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(klipperNozzleLabel, 128, 30);
  lv_obj_set_pos(klipperNozzleLabel, 134, 178);

  klipperBedTitleLabel = createLabel(klipperScreen, &lv_font_montserrat_18, COLOR_MUTED, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_size(klipperBedTitleLabel, 58, 30);
  lv_obj_set_pos(klipperBedTitleLabel, 258, 178);
  lv_label_set_text(klipperBedTitleLabel, "Bett");

  klipperBedLabel = createLabel(klipperScreen, &lv_font_montserrat_18, COLOR_CYAN, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_size(klipperBedLabel, 104, 30);
  lv_obj_set_pos(klipperBedLabel, 316, 178);

  klipperDurationLabel = createLabel(klipperScreen, &lv_font_montserrat_16, COLOR_DIM, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(klipperDurationLabel, 210, 28);
  lv_obj_set_pos(klipperDurationLabel, 60, 204);

  klipperStatusLabel = createLabel(klipperScreen, &lv_font_montserrat_16, COLOR_DIM, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_size(klipperStatusLabel, 210, 28);
  lv_obj_set_pos(klipperStatusLabel, 210, 204);

  klipperMmuLabel = createLabel(klipperScreen, &lv_font_montserrat_16, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(klipperMmuLabel, 420, 22);
  lv_obj_align(klipperMmuLabel, LV_ALIGN_TOP_MID, 0, 226);

  const int gateW = 40;
  const int gateH = 18;
  const int gateGap = 8;
  const int gateStartX = 52;
  for (int i = 0; i < MMU_GATE_MAX; i++) {
    klipperMmuGateBox[i] = lv_obj_create(klipperScreen);
    styleFilledRect(klipperMmuGateBox[i], COLOR_DIM, 3);
    lv_obj_set_size(klipperMmuGateBox[i], gateW, gateH);
    lv_obj_set_pos(klipperMmuGateBox[i], gateStartX + (i * (gateW + gateGap)), 249);
    lv_obj_set_style_border_width(klipperMmuGateBox[i], 0, 0);
    lv_obj_set_style_border_color(klipperMmuGateBox[i], lv_color_hex(COLOR_TEXT), 0);

    klipperMmuGateLabel[i] = createLabel(klipperMmuGateBox[i], &lv_font_montserrat_16, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(klipperMmuGateLabel[i], gateW, gateH);
    lv_obj_set_pos(klipperMmuGateLabel[i], 0, 0);
    String gateLabel = "T" + String(i);
    lv_label_set_text(klipperMmuGateLabel[i], gateLabel.c_str());
  }
}

void createBootScreen() {
  bootScreen = createScreen();
  lv_obj_t* bootLabel = createLabel(bootScreen, &lv_font_montserrat_32, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(bootLabel, 440, 44);
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
  int code = weatherCode;
  xSemaphoreGive(dataMutex);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    setTimeNormalVisible(true);
    setHidden(timeSunIcon.root, true);
    updateTimeSecondProgress(-1);
    updateWeatherImage(code);
    String tempText = temp + TEMP_UNIT;
    setLabelTextIfChanged(timeLabel, "--:--");
    setLabelTextIfChanged(weekdayLabel, "Zeit wird synchronisiert");
    setLabelTextIfChanged(dateLabel, status.c_str());
    setLabelTextIfChanged(tempLabel, tempText.c_str());
    return;
  }

  char timeStringBuff[10];
  char dateStringBuff[20];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M", &timeinfo);
  strftime(dateStringBuff, sizeof(dateStringBuff), "%d.%m.%Y", &timeinfo);

  setTimeNormalVisible(true);
  updateSunStatusIcon(timeinfo);
  updateTimeSecondProgress(timeinfo.tm_sec);
  updateWeatherImage(code);
  String tempText = temp + TEMP_UNIT;
  setLabelTextIfChanged(timeLabel, timeStringBuff);
  setLabelTextIfChanged(weekdayLabel, WEEKDAYS_DE[timeinfo.tm_wday]);
  setLabelTextIfChanged(dateLabel, dateStringBuff);
  setLabelTextIfChanged(tempLabel, tempText.c_str());
}

void refreshCryptoUi() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  String price = currentBtcPrice;
  String status = currentBtcStatus;
  xSemaphoreGive(dataMutex);

  setLabelTextIfChanged(cryptoPriceLabel, price.c_str());
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
  uint32_t candleTime = (btcCandles != nullptr && btcCandleCount > 0) ? btcCandles[btcCandleCount - 1].time : 0;
  xSemaphoreGive(dataMutex);

  if (ready) {
    candle = formatBtcCandleCountdown(candleTime);
  }

  setLabelTextIfChanged(btcDayPriceLabel, price.c_str());
  setLabelTextIfChanged(btcDayChangeLabel, change.c_str());
  setLabelTextIfChanged(btcDayRangeLabel, range.c_str());
  setLabelTextIfChanged(btcDayVolumeLabel, volume.c_str());
  setLabelTextIfChanged(btcDayCandleLabel, candle.c_str());
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
  setHidden(klipperOfflineRing, false);
  setHidden(klipperOfflineStem, false);
  setHidden(klipperOfflineLine, false);
  lv_obj_set_style_border_color(klipperOfflineRing, lv_color_hex(stateColor), 0);
  lv_obj_set_style_bg_color(klipperOfflineStem, lv_color_hex(stateColor), 0);
  lv_obj_set_style_bg_color(klipperOfflineLine, lv_color_hex(stateColor), 0);

  lv_obj_set_size(klipperProgressLabel, 260, 62);
  lv_obj_set_pos(klipperProgressLabel, 172, 70);
  lv_obj_set_size(klipperFileLabel, 390, 62);
  lv_obj_set_pos(klipperFileLabel, 45, 154);
  lv_label_set_long_mode(klipperFileLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_size(klipperDurationLabel, 190, 28);
  lv_obj_set_pos(klipperDurationLabel, 60, 218);
  lv_obj_set_size(klipperStatusLabel, 210, 28);
  lv_obj_set_pos(klipperStatusLabel, 210, 218);
  lv_obj_set_size(klipperMmuLabel, 420, 22);
  lv_obj_align(klipperMmuLabel, LV_ALIGN_TOP_MID, 0, 246);

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
  lv_obj_set_size(klipperProgressArc, 102, 102);
  lv_obj_align(klipperProgressArc, LV_ALIGN_TOP_MID, 0, 36);
  setHidden(klipperDivider, true);
  setHidden(klipperOfflineRing, true);
  setHidden(klipperOfflineStem, true);
  setHidden(klipperOfflineLine, true);

  lv_obj_set_size(klipperProgressLabel, 180, 54);
  lv_obj_align(klipperProgressLabel, LV_ALIGN_TOP_MID, 0, 61);
  lv_obj_set_size(klipperFileLabel, 420, 28);
  lv_obj_align(klipperFileLabel, LV_ALIGN_TOP_MID, 0, 146);
  lv_label_set_long_mode(klipperFileLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_size(klipperDurationLabel, 210, 28);
  lv_obj_set_pos(klipperDurationLabel, 60, 204);
  lv_obj_set_size(klipperStatusLabel, 210, 28);
  lv_obj_set_pos(klipperStatusLabel, 210, 204);
  lv_obj_set_size(klipperMmuLabel, 420, 22);
  lv_obj_align(klipperMmuLabel, LV_ALIGN_TOP_MID, 0, 226);

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
    220,
    0,
    false
  );
  if (currentScreen == SCREEN_BTC_DAY) {
    lastBtcChartDraw = 0;
  }
}
