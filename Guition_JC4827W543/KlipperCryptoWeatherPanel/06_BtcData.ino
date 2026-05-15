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
    return "EUR ";
  }
  if (quote.equalsIgnoreCase("GBP")) {
    return "GBP ";
  }
  return quote + " ";
}

String cryptoSpotUrl() {
  return String("https://api.coinbase.com/v2/prices/") + cryptoProductId() + "/spot";
}

String cryptoStatsUrl() {
  return String("https://api.exchange.coinbase.com/products/") + cryptoProductId() + "/stats";
}

String cryptoCandlesBaseUrl() {
  return String("https://api.exchange.coinbase.com/products/") + cryptoProductId() + "/candles";
}

String cryptoOkStatus() {
  return String(cryptoServiceName) + " " + cryptoQuoteSymbol;
}

bool cryptoChartTimeframeAllowed(const char* value) {
  if (value == nullptr) {
    return false;
  }

  String timeframe = value;
  return timeframe.equalsIgnoreCase("15M") ||
         timeframe.equalsIgnoreCase("1H") ||
         timeframe.equalsIgnoreCase("6H") ||
         timeframe.equalsIgnoreCase("1D");
}

const char* cryptoChartTimeframeLabel() {
  String timeframe = "";
  if (cryptoChartTimeframe != nullptr) {
    timeframe = cryptoChartTimeframe;
  }
  if (timeframe.equalsIgnoreCase("15M")) {
    return "15M";
  }
  if (timeframe.equalsIgnoreCase("1H")) {
    return "1H";
  }
  if (timeframe.equalsIgnoreCase("6H")) {
    return "6H";
  }
  return "1D";
}

uint32_t cryptoChartGranularitySeconds() {
  String timeframe = cryptoChartTimeframeLabel();
  if (timeframe.equalsIgnoreCase("15M")) {
    return 900;
  }
  if (timeframe.equalsIgnoreCase("1H")) {
    return 3600;
  }
  if (timeframe.equalsIgnoreCase("6H")) {
    return 21600;
  }
  return BTC_CANDLE_SECONDS;
}

int cryptoChartCandleCount() {
  return BTC_DAY_CANDLE_COUNT;
}

void formatChartTime(time_t value, bool includeTime, char* buffer, size_t bufferSize) {
  struct tm timeinfo;
  if (value <= 0 || !localtime_r(&value, &timeinfo)) {
    snprintf(buffer, bufferSize, "--");
    return;
  }

  strftime(buffer, bufferSize, includeTime ? "%b %d %H:%M" : "%b %d", &timeinfo);
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
    btcDayRange = "--";
    btcDayVolume = "VOL --";
    btcCandleStatus = "CANDLE RAM";
    btcDayDataReady = false;
    return;
  }

  if (btcCandleCount < 2) {
    btcDayChange = String(cryptoChartTimeframeLabel()) + " --";
    btcDayRange = "--";
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
  float volume = 0.0f;

  for (int i = start; i < btcCandleCount; i++) {
    volume += btcCandles[i].volume;
  }

  btcDayChangePercent = latestCandle.open > 0.0f ? ((latestCandle.close - latestCandle.open) / latestCandle.open) * 100.0f : 0.0f;
  btcDayChangePositive = btcDayChangePercent >= 0.0f;
  btcDayChange = String(cryptoChartTimeframeLabel()) + " " + String(btcDayChangePositive ? "+" : "") + String(btcDayChangePercent, 2) + "%";

  uint32_t seconds = cryptoChartGranularitySeconds();
  char startText[24];
  char endText[24];
  formatChartTime((time_t)btcCandles[start].time, seconds < BTC_CANDLE_SECONDS, startText, sizeof(startText));
  formatChartTime((time_t)latestCandle.time + (time_t)seconds, seconds < BTC_CANDLE_SECONDS, endText, sizeof(endText));
  btcDayRange = String(startText) + " - " + String(endText);
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
  invalidateBtcChartCache();
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
  invalidateBtcChartCache();
}
