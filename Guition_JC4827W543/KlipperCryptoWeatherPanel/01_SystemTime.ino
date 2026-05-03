void yieldFetchTask() {
  vTaskDelay(pdMS_TO_TICKS(1));
}

static uint32_t lvTickMillis() {
  return millis();
}

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default:
      switch ((int)reason) {
        case 11: return "USB";
        case 12: return "JTAG";
        case 13: return "EFUSE";
        case 14: return "PWR_GLITCH";
        case 15: return "CPU_LOCKUP";
        default: return "UNKNOWN";
      }
  }
}

void printBootDiagnostics() {
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("Reset reason: %s (%d)\n", resetReasonName(reason), (int)reason);
  Serial.printf(
    "Heap: free=%u min=%u maxAlloc=%u internalLargest=%u psram=%u/%u\n",
    ESP.getFreeHeap(),
    ESP.getMinFreeHeap(),
    ESP.getMaxAllocHeap(),
    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
    ESP.getFreePsram(),
    ESP.getPsramSize()
  );
}

void printRuntimeHealth() {
  UBaseType_t fetchStackWatermark = fetchTaskHandle != NULL ? uxTaskGetStackHighWaterMark(fetchTaskHandle) : 0;
  Serial.printf(
    "Health: heap=%u min=%u maxAlloc=%u internalLargest=%u psramFree=%u fetchStackWatermark=%u wifi=%d rssi=%d\n",
    ESP.getFreeHeap(),
    ESP.getMinFreeHeap(),
    ESP.getMaxAllocHeap(),
    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
    ESP.getFreePsram(),
    (unsigned)fetchStackWatermark,
    WiFi.status(),
    WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0
  );
}

bool getLocalTimeFast(struct tm& timeinfo) {
  time_t now = time(nullptr);
  if (now < 100000) {
    return false;
  }

  localtime_r(&now, &timeinfo);
  return true;
}

bool configureTimeOnce() {
  if (timeConfigured) {
    return true;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  bool mutexTaken = false;
  if (networkMutex != NULL) {
    if (xSemaphoreTake(networkMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
      return false;
    }
    mutexTaken = true;
  }

  if (timeConfigured || WiFi.status() != WL_CONNECTED) {
    if (mutexTaken) {
      xSemaphoreGive(networkMutex);
    }
    return timeConfigured;
  }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  timeConfigured = true;
  Serial.println("NTP/Zeitkonfiguration gestartet");

  if (mutexTaken) {
    xSemaphoreGive(networkMutex);
  }
  return true;
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

void setDisplayBrightness(uint8_t brightness) {
  display.setBrightness(brightness);
  currentBrightness = brightness;
}

int clampMinuteOfDay(int minute) {
  if (minute < 0) {
    return 0;
  }
  if (minute > 1439) {
    return 1439;
  }
  return minute;
}

int localUtcOffsetMinutes(const struct tm& timeinfo) {
  return timeinfo.tm_isdst > 0 ? 120 : 60;
}

bool calculateSunTimes(const struct tm& timeinfo, int& sunriseMinute, int& sunsetMinute) {
  const double pi = 3.14159265358979323846;
  const double degToRad = pi / 180.0;
  const double radToDeg = 180.0 / pi;
  const int dayOfYear = timeinfo.tm_yday + 1;
  const double gamma = (2.0 * pi / 365.0) * (dayOfYear - 1);

  const double equationOfTime =
    229.18 * (
      0.000075 +
      0.001868 * cos(gamma) -
      0.032077 * sin(gamma) -
      0.014615 * cos(2.0 * gamma) -
      0.040849 * sin(2.0 * gamma)
    );

  const double solarDeclination =
    0.006918 -
    0.399912 * cos(gamma) +
    0.070257 * sin(gamma) -
    0.006758 * cos(2.0 * gamma) +
    0.000907 * sin(2.0 * gamma) -
    0.002697 * cos(3.0 * gamma) +
    0.00148 * sin(3.0 * gamma);

  const double latitudeRad = locationLatitude * degToRad;
  const double zenithRad = 90.833 * degToRad;
  const double hourAngleArg =
    (cos(zenithRad) / (cos(latitudeRad) * cos(solarDeclination))) -
    (tan(latitudeRad) * tan(solarDeclination));

  if (hourAngleArg < -1.0 || hourAngleArg > 1.0) {
    return false;
  }

  const double hourAngleDeg = acos(hourAngleArg) * radToDeg;
  const double solarNoon =
    720.0 -
    (4.0 * locationLongitude) -
    equationOfTime +
    localUtcOffsetMinutes(timeinfo);

  sunriseMinute = clampMinuteOfDay((int)round(solarNoon - (4.0 * hourAngleDeg)));
  sunsetMinute = clampMinuteOfDay((int)round(solarNoon + (4.0 * hourAngleDeg)));
  return sunriseMinute < sunsetMinute;
}

int calculateSunStrength(const struct tm& timeinfo, int sunriseMinute, int sunsetMinute) {
  const int nowMinute = (timeinfo.tm_hour * 60) + timeinfo.tm_min;
  if (nowMinute < sunriseMinute || nowMinute >= sunsetMinute) {
    return 0;
  }

  const double pi = 3.14159265358979323846;
  const double daylightProgress =
    (double)(nowMinute - sunriseMinute) / (double)(sunsetMinute - sunriseMinute);
  return (int)round(sin(daylightProgress * pi) * 100.0);
}

void updateBrightnessBySun() {
  struct tm timeinfo;
  if (!getLocalTimeFast(timeinfo)) {
    return;
  }

  int sunriseMinute = 0;
  int sunsetMinute = 0;
  if (!calculateSunTimes(timeinfo, sunriseMinute, sunsetMinute)) {
    return;
  }

  const int nowMinute = (timeinfo.tm_hour * 60) + timeinfo.tm_min;
  const bool nightMode = (nowMinute < sunriseMinute || nowMinute >= sunsetMinute);
  const uint8_t targetBrightness = nightMode ? nightBrightness : dayBrightness;

  if (currentBrightness != targetBrightness) {
    setDisplayBrightness(targetBrightness);
    Serial.printf(
      "Display-Helligkeit: %s (%u), Sonnenaufgang %02d:%02d, Sonnenuntergang %02d:%02d\n",
      nightMode ? "Nacht" : "Tag",
      targetBrightness,
      sunriseMinute / 60,
      sunriseMinute % 60,
      sunsetMinute / 60,
      sunsetMinute % 60
    );
  }
}

