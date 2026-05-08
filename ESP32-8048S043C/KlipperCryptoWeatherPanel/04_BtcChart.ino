int priceToChartY(float price, float low, float high, int y, int h) {
  if (high <= low) {
    return y + (h / 2);
  }

  float normalized = (price - low) / (high - low);
  if (normalized < 0.0f) {
    normalized = 0.0f;
  }
  if (normalized > 1.0f) {
    normalized = 1.0f;
  }
  return y + h - 1 - (int)roundf(normalized * (float)(h - 1));
}

void canvasSetPixel(int x, int y, uint32_t color) {
  if (btcDayChartCanvas == nullptr || x < 0 || y < 0 || x >= BTC_CHART_W || y >= BTC_CHART_CANVAS_H) {
    return;
  }
  lv_canvas_set_px(btcDayChartCanvas, x, y, lv_color_hex(color), LV_OPA_COVER);
}

void canvasFillRect(int x, int y, int w, int h, uint32_t color) {
  if (w <= 0 || h <= 0) {
    return;
  }

  int x2 = x + w - 1;
  int y2 = y + h - 1;
  if (x2 < 0 || y2 < 0 || x >= BTC_CHART_W || y >= BTC_CHART_CANVAS_H) {
    return;
  }

  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x2 >= BTC_CHART_W) x2 = BTC_CHART_W - 1;
  if (y2 >= BTC_CHART_CANVAS_H) y2 = BTC_CHART_CANVAS_H - 1;

  lv_color_t lvColor = lv_color_hex(color);
  for (int yy = y; yy <= y2; yy++) {
    for (int xx = x; xx <= x2; xx++) {
      lv_canvas_set_px(btcDayChartCanvas, xx, yy, lvColor, LV_OPA_COVER);
    }
  }
}

void canvasHLine(int x, int y, int w, uint32_t color) {
  canvasFillRect(x, y, w, 1, color);
}

void canvasVLine(int x, int y, int h, uint32_t color) {
  canvasFillRect(x, y, 1, h, color);
}

void canvasDrawRect(int x, int y, int w, int h, uint32_t color) {
  canvasHLine(x, y, w, color);
  canvasHLine(x, y + h - 1, w, color);
  canvasVLine(x, y, h, color);
  canvasVLine(x + w - 1, y, h, color);
}

void drawBtcDayChart() {
  if (btcDayChartCanvas == nullptr || btcCandles == nullptr || chartBtcCandles == nullptr) {
    return;
  }

  int sourceCount = 0;
  bool ready = false;

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  ready = btcDayDataReady;
  sourceCount = btcCandleCount;
  if (sourceCount > BTC_CANDLE_CAPACITY) {
    sourceCount = BTC_CANDLE_CAPACITY;
  }
  for (int i = 0; i < sourceCount; i++) {
    chartBtcCandles[i] = btcCandles[i];
  }
  xSemaphoreGive(dataMutex);

  lv_display_enable_invalidation(lvDisplay, false);
  lv_canvas_fill_bg(btcDayChartCanvas, lv_color_hex(COLOR_BG), LV_OPA_COVER);
  canvasDrawRect(0, 0, BTC_CHART_W, BTC_CHART_H, COLOR_DIM);

  if (!ready || sourceCount < 2) {
    lv_display_enable_invalidation(lvDisplay, true);
    lv_obj_invalidate(btcDayChartCanvas);
    return;
  }

  int start = sourceCount - BTC_DAY_CANDLE_COUNT;
  if (start < 0) {
    start = 0;
  }
  int count = sourceCount - start;

  float low = chartBtcCandles[start].low;
  float high = chartBtcCandles[start].high;
  for (int i = start; i < sourceCount; i++) {
    if (chartBtcCandles[i].low < low) {
      low = chartBtcCandles[i].low;
    }
    if (chartBtcCandles[i].high > high) {
      high = chartBtcCandles[i].high;
    }
  }

  float padding = (high - low) * 0.08f;
  if (padding < 1.0f) {
    padding = 1.0f;
  }
  low -= padding;
  high += padding;

  for (int i = 1; i < 4; i++) {
    int gy = BTC_CHART_H * i / 4;
    canvasHLine(1, gy, BTC_CHART_W - 2, 0x1d2530);
  }

  for (int i = 1; i < 4; i++) {
    int gx = BTC_CHART_W * i / 4;
    canvasVLine(gx, 1, BTC_CHART_H - 2, 0x1d2530);
  }

  const int candleStep = max(1, (BTC_CHART_W - 5) / max(1, count - 1));
  const int bodyWidth = candleStep >= 6 ? 5 : (candleStep >= 4 ? 3 : 1);

  for (int i = start; i < sourceCount; i++) {
    const BtcCandle& candle = chartBtcCandles[i];
    int relative = i - start;
    int x = 2 + ((relative * (BTC_CHART_W - 5)) / max(1, count - 1));
    int yHigh = priceToChartY(candle.high, low, high, 3, BTC_CHART_H - 6);
    int yLow = priceToChartY(candle.low, low, high, 3, BTC_CHART_H - 6);
    int yOpen = priceToChartY(candle.open, low, high, 3, BTC_CHART_H - 6);
    int yClose = priceToChartY(candle.close, low, high, 3, BTC_CHART_H - 6);
    int bodyTop = min(yOpen, yClose);
    int bodyHeight = max(1, abs(yClose - yOpen) + 1);
    int bodyX = max(1, x - (bodyWidth / 2));
    if (bodyX + bodyWidth >= BTC_CHART_W) {
      bodyX = BTC_CHART_W - bodyWidth - 1;
    }
    uint32_t candleColor = candle.close >= candle.open ? COLOR_GREEN : COLOR_LOSS;

    canvasVLine(x, yHigh, max(1, yLow - yHigh + 1), candleColor);
    canvasFillRect(bodyX, bodyTop, bodyWidth, bodyHeight, candleColor);
    if (i == sourceCount - 1) {
      canvasDrawRect(max(1, bodyX - 1), max(1, bodyTop - 1), bodyWidth + 2, bodyHeight + 2, COLOR_BTC);
    }
  }

  canvasFillRect(0, BTC_CHART_PROGRESS_Y, BTC_CHART_W, BTC_CHART_PROGRESS_H, 0x1d2530);
  time_t nowTime = time(nullptr);
  uint32_t lastCandleTime = chartBtcCandles[sourceCount - 1].time;
  int progressWidth = 0;
  if (nowTime > 100000 && lastCandleTime > 0) {
    int elapsed = (int)(nowTime - lastCandleTime);
    if (elapsed < 0) {
      elapsed = 0;
    }
    if (elapsed > (int)BTC_CANDLE_SECONDS) {
      elapsed = (int)BTC_CANDLE_SECONDS;
    }
    progressWidth = (BTC_CHART_W * elapsed) / (int)BTC_CANDLE_SECONDS;
  }
  canvasFillRect(0, BTC_CHART_PROGRESS_Y, progressWidth, BTC_CHART_PROGRESS_H, COLOR_BTC);

  lv_display_enable_invalidation(lvDisplay, true);
  lv_obj_invalidate(btcDayChartCanvas);
}
