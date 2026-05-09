String cryptoProductId() {
  return String(cryptoBaseSymbol) + "-" + cryptoQuoteSymbol;
}

String cryptoPairTitle() {
  return String(cryptoBaseSymbol) + " / " + cryptoQuoteSymbol;
}

String cryptoDayTitle() {
  return String(cryptoBaseSymbol) + " " + cryptoChartTimeframeLabel();
}

String cryptoPricePrefixText() {
  if (cryptoPricePrefix != nullptr && cryptoPricePrefix[0] != '\0') {
    return String(cryptoPricePrefix);
  }

  String quote = cryptoQuoteSymbol;
  if (quote.equalsIgnoreCase("USD")) {
    return "$ ";
  }
  if (quote.equalsIgnoreCase("EUR")) {
    return "\xe2\x82\xac ";
  }
  if (quote.equalsIgnoreCase("GBP")) {
    return "\xc2\xa3 ";
  }
  if (quote.equalsIgnoreCase("JPY")) {
    return "\xc2\xa5 ";
  }
  if (quote.equalsIgnoreCase("BTC")) {
    return "\xe2\x82\xbf ";
  }
  return quote + " ";
}

String cryptoSpotUrl() {
  return String("https://api.coinbase.com/v2/prices/") + cryptoProductId() + "/spot";
}

String cryptoCandlesBaseUrl() {
  return String("https://api.exchange.coinbase.com/products/") + cryptoProductId() + "/candles";
}

String cryptoStatsUrl() {
  return String("https://api.exchange.coinbase.com/products/") + cryptoProductId() + "/stats";
}

String cryptoOkStatus() {
  return String(cryptoServiceName) + " " + cryptoQuoteSymbol;
}

uint32_t cryptoChartGranularitySeconds() {
  if (strcmp(cryptoChartTimeframe, "15M") == 0) {
    return 900;
  }
  if (strcmp(cryptoChartTimeframe, "1H") == 0) {
    return 3600;
  }
  if (strcmp(cryptoChartTimeframe, "6H") == 0) {
    return 21600;
  }
  return BTC_CANDLE_SECONDS;
}

int cryptoChartCandleCount() {
  return BTC_DAY_CANDLE_COUNT;
}

const char* cryptoChartTimeframeLabel() {
  if (strcmp(cryptoChartTimeframe, "15M") == 0 ||
      strcmp(cryptoChartTimeframe, "1H") == 0 ||
      strcmp(cryptoChartTimeframe, "6H") == 0 ||
      strcmp(cryptoChartTimeframe, "1D") == 0) {
    return cryptoChartTimeframe;
  }
  return "1D";
}

String cryptoChartPriceText(const String& fallbackPrice, float livePrice) {
  if (isfinite(livePrice) && livePrice > 0.0f) {
    uint8_t decimals = 2;
    if (livePrice >= 10000.0f) {
      decimals = 0;
    } else if (livePrice < 1.0f) {
      decimals = 4;
    }
    return String(cryptoQuoteSymbol) + " " + String(livePrice, (unsigned int)decimals);
  }

  String price = fallbackPrice;
  price.replace("$ ", "");
  price.replace("\xe2\x82\xac ", "");
  price.replace("\xc2\xa3 ", "");
  price.replace("\xc2\xa5 ", "");
  price.replace("\xe2\x82\xbf ", "");
  price.trim();
  bool hasDigit = false;
  for (int i = 0; i < price.length(); i++) {
    if (price[i] >= '0' && price[i] <= '9') {
      hasDigit = true;
      break;
    }
  }
  if (!hasDigit && price.length() > 0) {
    return price;
  }
  if (price.length() == 0) {
    price = "--";
  }
  return String(cryptoQuoteSymbol) + " " + price;
}

void resetCryptoDataState(const char* statusText) {
  if (dataMutex != NULL) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
  }
  currentBtcPrice = "Laden...";
  currentBtcLivePrice = 0.0f;
  currentBtcPriceDirection = 0;
  currentBtcStatus = statusText != nullptr && statusText[0] != '\0' ? String(statusText) : cryptoOkStatus();
  btcDayChange = String(cryptoChartTimeframeLabel()) + " --";
  btcDayTimeRange = "--";
  btcDayVolume = "VOL --";
  btcCandleStatus = "CANDLE --";
  btcDayChangePercent = 0.0f;
  btcDayChangePositive = true;
  btcDayDataReady = false;
  btc24hOpenPrice = 0.0f;
  btc24hDataReady = false;
  btcCandleCount = 0;
  if (dataMutex != NULL) {
    xSemaphoreGive(dataMutex);
  }
  lastBtcChartDraw = 0;
}

static void formatChartTime(time_t t, bool includeTime, char* buffer, size_t bufferSize) {
  if (t <= 0 || buffer == nullptr || bufferSize == 0) {
    if (buffer != nullptr && bufferSize > 0) {
      buffer[0] = '\0';
    }
    return;
  }
  struct tm tmInfo;
  if (localtime_r(&t, &tmInfo) == nullptr) {
    snprintf(buffer, bufferSize, "--");
    return;
  }
  if (includeTime) {
    strftime(buffer, bufferSize, "%b %d %H:%M", &tmInfo);
  } else {
    strftime(buffer, bufferSize, "%b %d", &tmInfo);
  }
}

void formatQuoteCompact(float value, char* buffer, size_t bufferSize) {
  String prefix = cryptoPricePrefixText();
  if (!isfinite(value) || value <= 0.0f) {
    snprintf(buffer, bufferSize, "%s--", prefix.c_str());
    return;
  }

  if (value >= 100000.0f) {
    snprintf(buffer, bufferSize, "%s%.0fK", prefix.c_str(), value / 1000.0f);
  } else if (value >= 10000.0f) {
    snprintf(buffer, bufferSize, "%s%.1fK", prefix.c_str(), value / 1000.0f);
  } else if (value >= 100.0f) {
    snprintf(buffer, bufferSize, "%s%.0f", prefix.c_str(), value);
  } else if (value >= 1.0f) {
    snprintf(buffer, bufferSize, "%s%.2f", prefix.c_str(), value);
  } else {
    snprintf(buffer, bufferSize, "%s%.4f", prefix.c_str(), value);
  }
}

static BtcCandle* allocateBtcCandleBuffer(const char* name, bool& inPsram) {
  const size_t bufferBytes = sizeof(BtcCandle) * BTC_CANDLE_CAPACITY;
  BtcCandle* buffer = (BtcCandle*)heap_caps_calloc(BTC_CANDLE_CAPACITY, sizeof(BtcCandle), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  inPsram = (buffer != nullptr);

  if (buffer == nullptr) {
    buffer = (BtcCandle*)heap_caps_calloc(BTC_CANDLE_CAPACITY, sizeof(BtcCandle), MALLOC_CAP_8BIT);
    inPsram = false;
  }

  if (buffer == nullptr) {
    Serial.printf("%s Speicher fehlgeschlagen: %u Bytes\n", name, (unsigned)bufferBytes);
    return nullptr;
  }

  Serial.printf("%s Speicher: %u Bytes in %s\n", name, (unsigned)bufferBytes, inPsram ? "PSRAM" : "internem RAM");
  return buffer;
}

bool initBtcStorage() {
  if (btcCandles != nullptr && parsedBtcCandles != nullptr && chartBtcCandles != nullptr) {
    return true;
  }

  btcCandles = allocateBtcCandleBuffer("BTC Candle", btcCandleStorageInPsram);
  parsedBtcCandles = allocateBtcCandleBuffer("BTC Parse", parsedBtcCandleStorageInPsram);
  chartBtcCandles = allocateBtcCandleBuffer("BTC Chart", chartBtcCandleStorageInPsram);

  return btcCandles != nullptr && parsedBtcCandles != nullptr && chartBtcCandles != nullptr;
}

void updateBtcDayStatsLocked() {
  if (btcCandles == nullptr) {
    btcDayChange = String(cryptoChartTimeframeLabel()) + " --";
    btcDayTimeRange = "--";
    btcDayVolume = "VOL --";
    btcCandleStatus = "CANDLE RAM";
    btcDayDataReady = false;
    return;
  }

  if (btcCandleCount < 2) {
    btcDayChange = String(cryptoChartTimeframeLabel()) + " --";
    btcDayTimeRange = "--";
    btcDayVolume = "VOL --";
    btcCandleStatus = "CANDLE --";
    btcDayDataReady = false;
    return;
  }

  int start = btcCandleCount - cryptoChartCandleCount();
  if (start < 0) {
    start = 0;
  }

  const BtcCandle& latestCandle = btcCandles[btcCandleCount - 1];
  float high = btcCandles[start].high;
  float low = btcCandles[start].low;
  float volume = 0.0f;

  for (int i = start; i < btcCandleCount; i++) {
    if (btcCandles[i].high > high) {
      high = btcCandles[i].high;
    }
    if (btcCandles[i].low < low) {
      low = btcCandles[i].low;
    }
    volume += btcCandles[i].volume;
  }

  btcDayChangePercent = latestCandle.open > 0.0f ? ((latestCandle.close - latestCandle.open) / latestCandle.open) * 100.0f : 0.0f;
  btcDayChangePositive = btcDayChangePercent >= 0.0f;
  btcDayChange = String(cryptoChartTimeframeLabel()) + " " + String(btcDayChangePositive ? "+" : "") + String(btcDayChangePercent, 2) + "%";

  uint32_t candleSeconds = cryptoChartGranularitySeconds();
  bool includeTime = candleSeconds < 86400;
  time_t rangeStart = (time_t)btcCandles[start].time;
  time_t rangeEnd = (time_t)btcCandles[btcCandleCount - 1].time + (time_t)candleSeconds;
  char startText[24];
  char endText[24];
  formatChartTime(rangeStart, includeTime, startText, sizeof(startText));
  formatChartTime(rangeEnd, includeTime, endText, sizeof(endText));
  if (startText[0] == '\0' || endText[0] == '\0') {
    btcDayTimeRange = "--";
  } else {
    btcDayTimeRange = String(startText) + " - " + String(endText);
  }
  btcDayVolume = "VOL " + String(volume, 1) + " " + cryptoBaseSymbol;

  btcCandleStatus = formatBtcCandleCountdown(btcCandles[btcCandleCount - 1].time);

  btcDayDataReady = true;
}

bool parseNextNumber(const String& payload, int& index, double& value) {
  const char* data = payload.c_str();
  int length = payload.length();
  while (index < length) {
    char c = data[index];
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t' && c != ',') {
      break;
    }
    index++;
  }

  if (index >= length) {
    return false;
  }

  char* endPtr = nullptr;
  value = strtod(data + index, &endPtr);
  if (endPtr == data + index) {
    return false;
  }

  index = (int)(endPtr - data);
  return true;
}

void sortCandlesAscending(BtcCandle* candles, int count) {
  for (int i = 1; i < count; i++) {
    BtcCandle key = candles[i];
    int j = i - 1;
    while (j >= 0 && candles[j].time > key.time) {
      candles[j + 1] = candles[j];
      j--;
    }
    candles[j + 1] = key;
    if ((i % 32) == 0) {
      yieldFetchTask();
    }
  }
}

int parseCoinbaseCandles(const String& payload, BtcCandle* outCandles, int maxCandles) {
  int count = 0;
  int index = 0;

  while (count < maxCandles) {
    int openBracket = payload.indexOf('[', index);
    if (openBracket < 0) {
      break;
    }

    index = openBracket + 1;
    double values[6];
    bool ok = true;
    for (int i = 0; i < 6; i++) {
      if (!parseNextNumber(payload, index, values[i])) {
        ok = false;
        break;
      }
    }

    if (!ok) {
      yieldFetchTask();
      continue;
    }

    BtcCandle candle;
    candle.time = (uint32_t)values[0];
    candle.low = (float)values[1];
    candle.high = (float)values[2];
    candle.open = (float)values[3];
    candle.close = (float)values[4];
    candle.volume = (float)values[5];

    if (candle.time > 0 && candle.low > 0.0f && candle.high >= candle.low && candle.open > 0.0f && candle.close > 0.0f) {
      outCandles[count++] = candle;
      if ((count % 16) == 0) {
        yieldFetchTask();
      }
    }
  }

  yieldFetchTask();
  sortCandlesAscending(outCandles, count);
  yieldFetchTask();
  return count;
}

void storeBtcCandles(const BtcCandle* candles, int count) {
  if (btcCandles == nullptr || candles == nullptr) {
    return;
  }

  if (count > BTC_CANDLE_CAPACITY) {
    count = BTC_CANDLE_CAPACITY;
  }

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  btcCandleCount = count;
  for (int i = 0; i < count; i++) {
    btcCandles[i] = candles[i];
  }
  updateBtcDayStatsLocked();
  xSemaphoreGive(dataMutex);
}

void updateLiveCandleFromPrice(float price) {
  if (btcCandles == nullptr) {
    return;
  }

  if (!isfinite(price) || price <= 0.0f) {
    return;
  }

  time_t nowTime = time(nullptr);
  if (nowTime < 100000) {
    return;
  }

  uint32_t candleSeconds = cryptoChartGranularitySeconds();
  uint32_t bucketTime = ((uint32_t)nowTime / candleSeconds) * candleSeconds;
  xSemaphoreTake(dataMutex, portMAX_DELAY);

  if (btcCandleCount > 0 && btcCandles[btcCandleCount - 1].time == bucketTime) {
    BtcCandle& candle = btcCandles[btcCandleCount - 1];
    candle.close = price;
    if (price > candle.high) {
      candle.high = price;
    }
    if (price < candle.low) {
      candle.low = price;
    }
  } else if (btcCandleCount > 0 && bucketTime > btcCandles[btcCandleCount - 1].time) {
    if (btcCandleCount >= BTC_CANDLE_CAPACITY) {
      for (int i = 1; i < btcCandleCount; i++) {
        btcCandles[i - 1] = btcCandles[i];
      }
      btcCandleCount--;
    }

    float previousClose = btcCandles[btcCandleCount - 1].close;
    BtcCandle candle;
    candle.time = bucketTime;
    candle.open = previousClose;
    candle.high = max(previousClose, price);
    candle.low = min(previousClose, price);
    candle.close = price;
    candle.volume = 0.0f;
    btcCandles[btcCandleCount++] = candle;
  }

  updateBtcDayStatsLocked();
  xSemaphoreGive(dataMutex);
}
