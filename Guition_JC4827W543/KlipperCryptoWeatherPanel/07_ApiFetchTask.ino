String readHttpPayloadChunked(HTTPClient& http, size_t reserveBytes) {
  StreamString payload;
  int httpSize = http.getSize();
  size_t neededBytes = reserveBytes;
  if (httpSize > 0 && (size_t)(httpSize + 1) > neededBytes) {
    neededBytes = (size_t)httpSize + 1;
  }

  if (neededBytes > 0 && !payload.reserve(neededBytes)) {
    Serial.printf("HTTP Payload Reserve fehlgeschlagen: %u Bytes\n", (unsigned)neededBytes);
  }

  int bytesWritten = http.writeToStream(&payload);
  yieldFetchTask();
  if (bytesWritten < 0) {
    Serial.printf("HTTP Payload Lesefehler: %d\n", bytesWritten);
    return "";
  }

  return String(payload);
}

String buildBrightSkyUrl() {
  return String("https://api.brightsky.dev/current_weather?lat=") +
    String(locationLatitude, 6) +
    "&lon=" +
    String(locationLongitude, 6);
}

bool fetchWeatherValue() {
  WiFiClientSecure weatherClient;
  weatherClient.setInsecure();

  HTTPClient http;
  String weatherUrl = buildBrightSkyUrl();
  if (!http.begin(weatherClient, weatherUrl)) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    weatherStatus = "DWD: HTTP BEGIN";
    xSemaphoreGive(dataMutex);
    Serial.println("DWD HTTP begin fehlgeschlagen");
    return false;
  }
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);
  http.setTimeout(8000);

  int httpCodeWeather = http.GET();
  Serial.printf("DWD HTTP: %d\n", httpCodeWeather);
  yieldFetchTask();

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  weatherStatus = String("DWD HTTP: ") + String(httpCodeWeather);
  xSemaphoreGive(dataMutex);

  if (httpCodeWeather == HTTP_CODE_OK) {
    String payload = readHttpPayloadChunked(http, 2048);
    yieldFetchTask();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      weatherStatus = "DWD: JSON";
      xSemaphoreGive(dataMutex);
      Serial.print("DWD JSON Parse Fehler: ");
      Serial.println(error.c_str());
      Serial.println(payload.substring(0, 240));
      http.end();
      return false;
    }

    JsonVariantConst weather = doc["weather"];
    JsonVariantConst tempValue = weather["temperature"];
    String temp;
    if (tempValue.is<double>() || tempValue.is<long>() || tempValue.is<unsigned long>()) {
      temp = String(tempValue.as<float>(), 1);
    } else {
      temp = jsonStringValue(tempValue);
    }
    temp.trim();

    String iconText = jsonStringValue(weather["icon"]);
    String conditionText = jsonStringValue(weather["condition"]);

    int stationSourceId = jsonIntValue(weather["fallback_source_ids"]["temperature"], -1);
    if (stationSourceId < 0) {
      stationSourceId = jsonIntValue(weather["source_id"], -1);
    }
    String stationName = weatherStationNameForSource(doc["sources"].as<JsonArrayConst>(), stationSourceId);
    if (stationName.length() == 0) {
      stationName = weatherStationNameForSource(doc["sources"].as<JsonArrayConst>(), jsonIntValue(weather["source_id"], -1));
    }

    if (temp.length() > 0) {
      int parsedWeatherCode = weatherCodeFromText(iconText);
      if (parsedWeatherCode < 0) {
        parsedWeatherCode = weatherCodeFromText(conditionText);
      }

      xSemaphoreTake(dataMutex, portMAX_DELAY);
      currentTemp = temp;
      if (parsedWeatherCode >= 0) {
        weatherCode = parsedWeatherCode;
      }
      if (stationName.length() > 0) {
        weatherLocation = stationName;
      }
      weatherStatus = "WETTER: DWD";
      xSemaphoreGive(dataMutex);
      Serial.println(String("DWD Temperatur: ") + temp + " C, station=" + stationName + ", icon=" + iconText + ", condition=" + conditionText);
      http.end();
      return true;
    }

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    weatherStatus = "DWD: KEIN TEMP";
    xSemaphoreGive(dataMutex);
    Serial.println("DWD Temperatur nicht in Antwort gefunden:");
    Serial.println(payload.substring(0, 240));
  } else {
    Serial.print("DWD Fehler: ");
    Serial.println(http.errorToString(httpCodeWeather));
  }

  http.end();
  return false;
}

bool fetchBtcPrice() {
  WiFiClientSecure btcClient;
  btcClient.setInsecure();

  HTTPClient http;
  String url = cryptoSpotUrl();
  if (!http.begin(btcClient, url)) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    currentBtcStatus = String(cryptoBaseSymbol) + " HTTP BEGIN";
    if (currentBtcPrice == "Laden...") {
      currentBtcPrice = String(cryptoBaseSymbol) + " FEHLER";
    }
    xSemaphoreGive(dataMutex);
    return false;
  }

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);
  http.setTimeout(8000);
  int httpCodeBtc = http.GET();
  Serial.printf("%s HTTP: %d\n", cryptoBaseSymbol, httpCodeBtc);
  yieldFetchTask();

  if (httpCodeBtc == HTTP_CODE_OK) {
    String payload = readHttpPayloadChunked(http, 1024);
    yieldFetchTask();
    String marker = "\"amount\":\"";
    int amountIndex = payload.indexOf(marker);
    if (amountIndex >= 0) {
      int start = amountIndex + marker.length();
      int end = payload.indexOf("\"", start);
      if (end > start) {
        String price = payload.substring(start, end);
        int dotIndex = price.indexOf('.');
        if (dotIndex > 0 && price.length() > dotIndex + 3) {
          price = price.substring(0, dotIndex + 3);
        }
        float livePrice = price.toFloat();

        xSemaphoreTake(dataMutex, portMAX_DELAY);
        if (currentBtcLivePrice > 0.0f && livePrice > 0.0f) {
          if (livePrice > currentBtcLivePrice) {
            currentBtcPriceDirection = 1;
          } else if (livePrice < currentBtcLivePrice) {
            currentBtcPriceDirection = -1;
          }
        } else {
          currentBtcPriceDirection = 0;
        }
        if (livePrice > 0.0f) {
          currentBtcLivePrice = livePrice;
        }
        currentBtcPrice = cryptoPricePrefixText() + price;
        currentBtcStatus = cryptoOkStatus();
        xSemaphoreGive(dataMutex);
        updateLiveCandleFromPrice(livePrice);

        http.end();
        return true;
      }
    }

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    currentBtcStatus = String(cryptoBaseSymbol) + " JSON FEHLER";
    if (currentBtcPrice == "Laden...") {
      currentBtcPrice = String(cryptoBaseSymbol) + " FEHLER";
    }
    xSemaphoreGive(dataMutex);
    Serial.printf("%s Preis nicht in Antwort gefunden:\n", cryptoBaseSymbol);
    Serial.println(payload.substring(0, 240));
  } else {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    currentBtcStatus = String(cryptoBaseSymbol) + " HTTP: " + String(httpCodeBtc);
    if (currentBtcPrice == "Laden...") {
      currentBtcPrice = String(cryptoBaseSymbol) + " FEHLER";
    }
    xSemaphoreGive(dataMutex);
    Serial.printf("%s Fehler: ", cryptoBaseSymbol);
    Serial.println(http.errorToString(httpCodeBtc));
  }

  http.end();
  return false;
}

String buildKlipperQueryUrl() {
  return String(klipperBaseUrl) +
    "/printer/objects/query"
    "?extruder=temperature,target"
    "&heater_bed=temperature,target"
    "&print_stats=state,filename,print_duration,total_duration"
    "&display_status=progress,message"
    "&mmu=enabled,num_gates,is_homed,is_paused,is_in_print,tool,gate,action,operation,filament,gate_status,gate_color,runout,servo,extruder_filament_remaining";
}

String urlEncodeQueryParam(const String& text) {
  const char hex[] = "0123456789ABCDEF";
  String encoded;
  encoded.reserve(text.length() + 8);

  for (int i = 0; i < text.length(); i++) {
    unsigned char c = (unsigned char)text[i];
    bool safe =
      (c >= 'A' && c <= 'Z') ||
      (c >= 'a' && c <= 'z') ||
      (c >= '0' && c <= '9') ||
      c == '-' || c == '_' || c == '.' || c == '~' || c == '/';

    if (safe) {
      encoded += (char)c;
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0f];
      encoded += hex[c & 0x0f];
    }
  }

  return encoded;
}

String formatKlipperTemperature(const String& currentValue, const String& targetValue) {
  if (currentValue.length() == 0) {
    return "--";
  }

  float current = currentValue.toFloat();
  float target = targetValue.length() > 0 ? targetValue.toFloat() : 0.0f;
  if (!isfinite(current)) {
    return "--";
  }

  if (isfinite(target) && target > 0.5f) {
    return String((int)roundf(current)) + "/" + String((int)roundf(target)) + TEMP_UNIT;
  }
  return String((int)roundf(current)) + TEMP_UNIT;
}

String formatKlipperProgress(const String& progressValue) {
  if (progressValue.length() == 0) {
    return "--";
  }

  float progress = progressValue.toFloat();
  if (!isfinite(progress)) {
    return "--";
  }
  if (progress <= 1.0f) {
    progress *= 100.0f;
  }
  progress = constrain(progress, 0.0f, 100.0f);
  return String((int)roundf(progress)) + "%";
}

String formatDurationClock(float seconds, bool forceHours) {
  if (!isfinite(seconds) || seconds < 0.0f) {
    return "--";
  }

  unsigned long minutesTotal = (unsigned long)((seconds + 30.0f) / 60.0f);
  unsigned long hours = minutesTotal / 60UL;
  unsigned long minutes = minutesTotal % 60UL;

  if (!forceHours && hours == 0) {
    return String(minutes) + "M";
  }

  String text = String(hours) + ":";
  if (minutes < 10) {
    text += "0";
  }
  text += String(minutes);
  return text;
}

String formatKlipperDuration(const String& secondsValue) {
  if (secondsValue.length() == 0) {
    return "DAUER --";
  }

  float secondsFloat = secondsValue.toFloat();
  if (!isfinite(secondsFloat) || secondsFloat <= 0.0f) {
    return "DAUER --";
  }

  unsigned long seconds = (unsigned long)secondsFloat;
  unsigned long hours = seconds / 3600UL;
  unsigned long minutes = (seconds % 3600UL) / 60UL;
  if (hours > 0) {
    return "DAUER " + String(hours) + "H " + String(minutes) + "M";
  }
  return "DAUER " + String(minutes) + "M";
}

String formatKlipperDurationProgress(const String& secondsValue, float estimatedSeconds) {
  float secondsFloat = secondsValue.length() > 0 ? secondsValue.toFloat() : 0.0f;
  if (!isfinite(secondsFloat) || secondsFloat < 0.0f) {
    secondsFloat = 0.0f;
  }

  if (isfinite(estimatedSeconds) && estimatedSeconds > 0.5f) {
    return formatDurationClock(secondsFloat, true) + "/" + formatDurationClock(estimatedSeconds, true);
  }

  return formatKlipperDuration(secondsValue);
}

String formatKlipperState(String state) {
  state.toLowerCase();
  if (state == "printing") {
    return "DRUCKT";
  }
  if (state == "paused" || state == "pausing") {
    return "PAUSE";
  }
  if (state == "error") {
    return "FEHLER";
  }
  if (state == "complete") {
    return "FERTIG";
  }
  if (state == "cancelled" || state == "cancelling") {
    return "ABBRUCH";
  }
  if (state == "standby" || state == "ready") {
    return "BEREIT";
  }
  if (state.length() == 0) {
    return "--";
  }
  state.toUpperCase();
  return state;
}

String formatKlippyConnectionState(String state) {
  state.toLowerCase();
  if (state == "ready") {
    return "BEREIT";
  }
  if (state == "startup") {
    return "START";
  }
  if (state == "shutdown" || state == "disconnected") {
    return "AUS";
  }
  if (state == "error") {
    return "FEHLER";
  }
  if (state.length() == 0) {
    return "--";
  }
  state.toUpperCase();
  return state;
}

String formatKlippyMessageForScreen(String message) {
  message.replace("\r", "");
  message.trim();
  if (message.length() == 0) {
    return "";
  }

  message.replace("Once the underlying issue is corrected, use the\n", "");
  message.replace("\"FIRMWARE_RESTART\" command to reset the firmware, reload the\nconfig, and restart the host software.", "FIRMWARE_RESTART ausfuehren");
  message.replace("Klipper reports: ", "");

  const int maxLen = 132;
  if (message.length() > maxLen) {
    message = message.substring(0, maxLen - 3) + "...";
  }
  return message;
}

String formatKlipperDisplayMessage(String message) {
  message.replace("\r", " ");
  message.replace("\n", " ");
  message.trim();
  while (message.indexOf("  ") >= 0) {
    message.replace("  ", " ");
  }

  if (message.length() == 0) {
    return "";
  }

  const int maxLen = 72;
  if (message.length() > maxLen) {
    message = message.substring(0, maxLen - 3) + "...";
  }
  return message;
}

String klippyOfflineDetail(const String& state, const String& message) {
  String detail = formatKlippyMessageForScreen(message);
  if (detail.length() > 0) {
    return detail;
  }

  String normalizedState = state;
  normalizedState.toLowerCase();
  if (normalizedState == "shutdown" || normalizedState == "disconnected") {
    return "Drucker aus oder MCU nicht verbunden";
  }
  if (normalizedState == "startup") {
    return "Klipper startet";
  }
  if (normalizedState == "error") {
    return "Klipper Fehler";
  }
  if (normalizedState == "ready") {
    return "Druckdaten nicht verfuegbar";
  }
  return "Klippy nicht bereit";
}

String formatKlipperFile(String filename) {
  if (filename.length() == 0) {
    return "Kein Job";
  }

  int slashIndex = max(filename.lastIndexOf('/'), filename.lastIndexOf('\\'));
  if (slashIndex >= 0 && slashIndex + 1 < filename.length()) {
    filename = filename.substring(slashIndex + 1);
  }
  return filename;
}

uint32_t parseKlipperColor(String colorText) {
  colorText.trim();
  if (colorText.length() < 6) {
    return COLOR_DIM;
  }

  char rgbText[7];
  for (int i = 0; i < 6; i++) {
    rgbText[i] = colorText[i];
  }
  rgbText[6] = '\0';

  char* endPtr = nullptr;
  uint32_t color = (uint32_t)strtoul(rgbText, &endPtr, 16);
  if (endPtr == rgbText) {
    return COLOR_DIM;
  }

  return color;
}

String formatKlipperMmuInfo(int tool, int gate, const String& action, const String& operation, const String& filament) {
  String activity = action.length() > 0 ? action : operation;
  if (activity.length() == 0) {
    activity = "Idle";
  }

  String info = "MMU";
  if (tool >= 0) {
    info += " T" + String(tool);
  }
  if (gate >= 0) {
    info += " G" + String(gate);
  }
  info += " " + activity;
  if (filament.length() > 0 && filament != "Unloaded") {
    info += "  " + filament;
  }
  return info;
}

String jsonStringValue(JsonVariantConst value) {
  if (value.is<const char*>()) {
    const char* text = value.as<const char*>();
    return text != nullptr ? String(text) : String();
  }

  return "";
}

String jsonNumberText(JsonVariantConst value) {
  if (value.is<double>()) {
    return String(value.as<double>(), 3);
  }

  if (value.is<long>()) {
    return String(value.as<long>());
  }

  if (value.is<unsigned long>()) {
    return String(value.as<unsigned long>());
  }

  if (value.is<const char*>()) {
    return jsonStringValue(value);
  }

  return "";
}

int jsonIntValue(JsonVariantConst value, int fallback) {
  if (value.is<int>()) {
    return value.as<int>();
  }

  if (value.is<const char*>()) {
    String text = jsonStringValue(value);
    text.trim();
    if (text.length() > 0) {
      return text.toInt();
    }
  }

  return fallback;
}

bool fetchKlipperFileMetadata(const String& filename, float& estimatedSeconds) {
  estimatedSeconds = 0.0f;
  if (filename.length() == 0) {
    return false;
  }

  WiFiClient klipperClient;
  HTTPClient http;
  String url = String(klipperBaseUrl) + "/server/files/metadata?filename=" + urlEncodeQueryParam(filename);

  if (!http.begin(klipperClient, url)) {
    Serial.println("Klipper Metadata HTTP begin fehlgeschlagen");
    return false;
  }

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);
  http.setTimeout(3000);
  int httpCodeMetadata = http.GET();
  Serial.printf("Klipper Metadata HTTP: %d\n", httpCodeMetadata);
  yieldFetchTask();

  if (httpCodeMetadata == HTTP_CODE_OK) {
    String payload = readHttpPayloadChunked(http, 4096);
    yieldFetchTask();

    JsonDocument filter;
    filter["result"]["estimated_time"] = true;
    filter["estimated_time"] = true;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    if (!error) {
      String estimatedText = jsonNumberText(doc["result"]["estimated_time"]);
      if (estimatedText.length() == 0) {
        estimatedText = jsonNumberText(doc["estimated_time"]);
      }

      float parsedEstimate = estimatedText.toFloat();
      if (isfinite(parsedEstimate) && parsedEstimate > 0.5f) {
        estimatedSeconds = parsedEstimate;
        Serial.printf("Klipper Metadata Estimate: %.0f Sekunden\n", estimatedSeconds);
        http.end();
        return true;
      }
    } else {
      Serial.print("Klipper Metadata JSON Parse Fehler: ");
      Serial.println(error.c_str());
    }

    Serial.println("Klipper Metadata ohne estimated_time");
    Serial.println(payload.substring(0, 240));
  } else {
    Serial.print("Klipper Metadata Fehler: ");
    Serial.println(http.errorToString(httpCodeMetadata));
  }

  http.end();
  return false;
}

float klipperEstimatedSecondsForFile(const String& filename) {
  if (filename.length() == 0) {
    klipperMetadataFilename = "";
    klipperEstimatedDurationSeconds = 0.0f;
    klipperMetadataRetryAfter = 0;
    return 0.0f;
  }

  if (filename != klipperMetadataFilename) {
    klipperMetadataFilename = filename;
    klipperEstimatedDurationSeconds = 0.0f;
    klipperMetadataRetryAfter = 0;
  }

  if (klipperEstimatedDurationSeconds > 0.5f) {
    return klipperEstimatedDurationSeconds;
  }

  unsigned long now = millis();
  if (klipperMetadataRetryAfter != 0 && (long)(now - klipperMetadataRetryAfter) < 0) {
    return 0.0f;
  }

  float estimatedSeconds = 0.0f;
  if (fetchKlipperFileMetadata(filename, estimatedSeconds)) {
    klipperEstimatedDurationSeconds = estimatedSeconds;
    klipperMetadataRetryAfter = 0;
  } else {
    klipperMetadataRetryAfter = millis() + klipperNameRetryInterval;
  }

  return klipperEstimatedDurationSeconds;
}

bool fetchKlipperPrinterName() {
  WiFiClient klipperClient;
  HTTPClient http;
  String url = String(klipperBaseUrl) + "/server/database/item?namespace=mainsail&key=general.printername";

  if (!http.begin(klipperClient, url)) {
    Serial.println("Klipper Name HTTP begin fehlgeschlagen");
    return false;
  }

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);
  http.setTimeout(3000);
  int httpCodeName = http.GET();
  Serial.printf("Klipper Name HTTP: %d\n", httpCodeName);
  yieldFetchTask();

  if (httpCodeName == HTTP_CODE_OK) {
    String payload = readHttpPayloadChunked(http, 512);
    yieldFetchTask();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    String name;
    if (!error) {
      name = jsonStringValue(doc["result"]["value"]);
      if (name.length() == 0) {
        name = jsonStringValue(doc["value"]);
      }
    } else {
      Serial.print("Klipper Name JSON Parse Fehler: ");
      Serial.println(error.c_str());
    }
    name.trim();

    if (name.length() > 0) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      klipperPrinterName = name;
      xSemaphoreGive(dataMutex);
      http.end();
      return true;
    }
  } else {
    Serial.print("Klipper Name Fehler: ");
    Serial.println(http.errorToString(httpCodeName));
  }

  http.end();
  return false;
}

bool fetchKlipperServerInfo(String& klippyState, String& klippyMessage) {
  klippyState = "";
  klippyMessage = "";

  WiFiClient klipperClient;
  HTTPClient http;
  String url = String(klipperBaseUrl) + "/server/info";

  if (!http.begin(klipperClient, url)) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    klipperHostAvailable = false;
    klipperAvailable = false;
    klipperMmuAvailable = false;
    klipperConnectionState = "--";
    klipperConnectionMessage = "";
    klipperStatus = "MOONRAKER BEGIN";
    klipperDisplayMessage = "";
    xSemaphoreGive(dataMutex);
    Serial.println("Moonraker Server Info HTTP begin fehlgeschlagen");
    return false;
  }

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);
  http.setTimeout(3000);
  int httpCodeInfo = http.GET();
  Serial.printf("Moonraker Info HTTP: %d\n", httpCodeInfo);
  yieldFetchTask();

  if (httpCodeInfo == HTTP_CODE_OK) {
    String payload = readHttpPayloadChunked(http, 1024);
    yieldFetchTask();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      klipperHostAvailable = false;
      klipperAvailable = false;
      klipperMmuAvailable = false;
      klipperConnectionState = "--";
      klipperConnectionMessage = "";
      klipperStatus = "INFO JSON";
      klipperDisplayMessage = "";
      xSemaphoreGive(dataMutex);
      Serial.print("Moonraker Info JSON Parse Fehler: ");
      Serial.println(error.c_str());
      Serial.println(payload.substring(0, 240));
      http.end();
      return false;
    }

    JsonVariantConst stateValue = doc["result"]["klippy_state"];
    if (stateValue.isNull()) {
      stateValue = doc["klippy_state"];
    }
    klippyState = jsonStringValue(stateValue);
    klippyState.trim();

    JsonVariantConst connectedValue = doc["result"]["klippy_connected"];
    if (connectedValue.isNull()) {
      connectedValue = doc["klippy_connected"];
    }
    if (klippyState.length() == 0) {
      klippyState = connectedValue.is<bool>() && connectedValue.as<bool>() ? "ready" : "disconnected";
    }

    JsonVariantConst warningValue = doc["result"]["warnings"][0];
    if (warningValue.isNull()) {
      warningValue = doc["warnings"][0];
    }
    klippyMessage = jsonStringValue(warningValue);
    klippyMessage.trim();

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    klipperHostAvailable = true;
    klipperConnectionState = klippyState;
    klipperConnectionMessage = klippyMessage;
    klipperStatus = "MOONRAKER OK";
    if (!klippyState.equalsIgnoreCase("ready")) {
      klipperAvailable = false;
      klipperMmuAvailable = false;
      klipperState = formatKlippyConnectionState(klippyState);
      klipperFile = klippyOfflineDetail(klippyState, klippyMessage);
      klipperProgress = klipperState;
      klipperNozzle = "OK";
      klipperBed = klipperState;
      klipperDuration = "MAINSAIL OK";
      klipperDisplayMessage = "";
      klipperMmuInfo = "Drucker einschalten";
      klipperMmuGateCount = 0;
      for (int i = 0; i < MMU_GATE_MAX; i++) {
        klipperMmuGateColors[i] = COLOR_DIM;
        klipperMmuGateStatus[i] = -1;
      }
    }
    xSemaphoreGive(dataMutex);

    http.end();
    return true;
  }

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  klipperHostAvailable = false;
  klipperAvailable = false;
  klipperMmuAvailable = false;
  klipperConnectionState = "--";
  klipperConnectionMessage = "";
  klipperStatus = "MOONRAKER " + String(httpCodeInfo);
  klipperDisplayMessage = "";
  xSemaphoreGive(dataMutex);
  Serial.print("Moonraker Info Fehler: ");
  Serial.println(http.errorToString(httpCodeInfo));

  http.end();
  return false;
}

bool fetchKlipperPrinterInfo(String& klippyState, String& klippyMessage) {
  WiFiClient klipperClient;
  HTTPClient http;
  String url = String(klipperBaseUrl) + "/printer/info";

  if (!http.begin(klipperClient, url)) {
    Serial.println("Klipper Printer Info HTTP begin fehlgeschlagen");
    return false;
  }

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);
  http.setTimeout(3000);
  int httpCodeInfo = http.GET();
  Serial.printf("Klipper Printer Info HTTP: %d\n", httpCodeInfo);
  yieldFetchTask();

  if (httpCodeInfo == HTTP_CODE_OK) {
    String payload = readHttpPayloadChunked(http, 2048);
    yieldFetchTask();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      Serial.print("Klipper Printer Info JSON Parse Fehler: ");
      Serial.println(error.c_str());
      Serial.println(payload.substring(0, 240));
      http.end();
      return false;
    }

    String state = jsonStringValue(doc["result"]["state"]);
    if (state.length() == 0) {
      state = jsonStringValue(doc["state"]);
    }
    state.trim();
    if (state.length() > 0) {
      klippyState = state;
    }

    String message = jsonStringValue(doc["result"]["state_message"]);
    if (message.length() == 0) {
      message = jsonStringValue(doc["state_message"]);
    }
    message.trim();
    if (message.length() > 0) {
      klippyMessage = message;
    }

    http.end();
    return true;
  }

  Serial.print("Klipper Printer Info Fehler: ");
  Serial.println(http.errorToString(httpCodeInfo));
  http.end();
  return false;
}

bool fetchKlipperStatus() {
  String klippyState;
  String klippyMessage;
  if (!fetchKlipperServerInfo(klippyState, klippyMessage)) {
    return false;
  }
  if (!klippyState.equalsIgnoreCase("ready")) {
    fetchKlipperPrinterInfo(klippyState, klippyMessage);
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    klipperConnectionState = klippyState;
    klipperConnectionMessage = klippyMessage;
    klipperState = formatKlippyConnectionState(klippyState);
    klipperFile = klippyOfflineDetail(klippyState, klippyMessage);
    klipperProgress = klipperState;
    klipperDisplayMessage = "";
    xSemaphoreGive(dataMutex);
    return true;
  }

  WiFiClient klipperClient;
  HTTPClient http;
  String url = buildKlipperQueryUrl();
  bool statusHttpEnded = false;

  if (!http.begin(klipperClient, url)) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    klipperHostAvailable = true;
    klipperAvailable = false;
    klipperMmuAvailable = false;
    klipperStatus = "HTTP BEGIN";
    klipperDisplayMessage = "";
    xSemaphoreGive(dataMutex);
    Serial.println("Klipper HTTP begin fehlgeschlagen");
    return false;
  }

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);
  http.setTimeout(3000);
  int httpCodeKlipper = http.GET();
  Serial.printf("Klipper HTTP: %d\n", httpCodeKlipper);
  yieldFetchTask();

  if (httpCodeKlipper == HTTP_CODE_OK) {
    String payload = readHttpPayloadChunked(http, 4096);
    yieldFetchTask();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      klipperHostAvailable = true;
      klipperAvailable = false;
      klipperMmuAvailable = false;
      klipperStatus = "JSON PARSE";
      klipperDisplayMessage = "";
      xSemaphoreGive(dataMutex);
      Serial.println("Klipper JSON konnte nicht geparst werden:");
      Serial.println(error.c_str());
      Serial.println(payload.substring(0, 240));
      http.end();
      return false;
    }

    JsonObjectConst status = doc["result"]["status"].as<JsonObjectConst>();
    if (status.isNull()) {
      status = doc["status"].as<JsonObjectConst>();
    }
    if (status.isNull()) {
      status = doc.as<JsonObjectConst>();
    }

    JsonObjectConst extruderObject = status["extruder"].as<JsonObjectConst>();
    JsonObjectConst bedObject = status["heater_bed"].as<JsonObjectConst>();
    JsonObjectConst printStatsObject = status["print_stats"].as<JsonObjectConst>();
    JsonObjectConst displayStatusObject = status["display_status"].as<JsonObjectConst>();
    JsonObjectConst mmuObject = status["mmu"].as<JsonObjectConst>();

    String rawState = jsonStringValue(printStatsObject["state"]);
    String rawFilename = jsonStringValue(printStatsObject["filename"]);
    String rawProgress = jsonNumberText(displayStatusObject["progress"]);
    String rawDisplayMessage = jsonStringValue(displayStatusObject["message"]);
    String rawDuration = jsonNumberText(printStatsObject["print_duration"]);
    if (rawDuration.length() == 0) {
      rawDuration = jsonNumberText(printStatsObject["total_duration"]);
    }

    String nozzleTemp = jsonNumberText(extruderObject["temperature"]);
    String nozzleTarget = jsonNumberText(extruderObject["target"]);
    String bedTemp = jsonNumberText(bedObject["temperature"]);
    String bedTarget = jsonNumberText(bedObject["target"]);
    String rawMmuGateCount = jsonNumberText(mmuObject["num_gates"]);
    String rawMmuAction = jsonStringValue(mmuObject["action"]);
    String rawMmuOperation = jsonStringValue(mmuObject["operation"]);
    String rawMmuFilament = jsonStringValue(mmuObject["filament"]);
    JsonArrayConst mmuGateColorArray = mmuObject["gate_color"].as<JsonArrayConst>();
    JsonArrayConst mmuGateStatusArray = mmuObject["gate_status"].as<JsonArrayConst>();
    bool hasMmuData = !mmuObject.isNull() && rawMmuGateCount.length() > 0;

    bool hasKlipperData =
      rawState.length() > 0 ||
      nozzleTemp.length() > 0 ||
      bedTemp.length() > 0 ||
      rawProgress.length() > 0;

    float estimatedSeconds = 0.0f;
    if (hasKlipperData && rawFilename.length() > 0) {
      http.end();
      statusHttpEnded = true;
      estimatedSeconds = klipperEstimatedSecondsForFile(rawFilename);
    } else if (hasKlipperData) {
      klipperEstimatedSecondsForFile("");
    }

    if (hasKlipperData) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      klipperHostAvailable = true;
      klipperAvailable = true;
      klipperConnectionState = "ready";
      klipperConnectionMessage = "";
      klipperState = formatKlipperState(rawState);
      klipperFile = formatKlipperFile(rawFilename);
      klipperProgress = formatKlipperProgress(rawProgress);
      klipperNozzle = formatKlipperTemperature(nozzleTemp, nozzleTarget);
      klipperBed = formatKlipperTemperature(bedTemp, bedTarget);
      klipperDuration = formatKlipperDurationProgress(rawDuration, estimatedSeconds);
      klipperStatus = "MOONRAKER OK";
      klipperDisplayMessage = formatKlipperDisplayMessage(rawDisplayMessage);
      klipperMmuAvailable = hasMmuData;
      klipperMmuTool = jsonIntValue(mmuObject["tool"], -1);
      klipperMmuGate = jsonIntValue(mmuObject["gate"], -1);
      klipperMmuGateCount = hasMmuData ? constrain(rawMmuGateCount.toInt(), 0, MMU_GATE_MAX) : 0;
      klipperMmuInfo = formatKlipperMmuInfo(klipperMmuTool, klipperMmuGate, rawMmuAction, rawMmuOperation, rawMmuFilament);
      for (int i = 0; i < MMU_GATE_MAX; i++) {
        klipperMmuGateColors[i] = COLOR_DIM;
        klipperMmuGateStatus[i] = -1;
      }
      for (int i = 0; i < klipperMmuGateCount; i++) {
        klipperMmuGateColors[i] = parseKlipperColor(jsonStringValue(mmuGateColorArray[i]));
        String gateStatus = jsonNumberText(mmuGateStatusArray[i]);
        klipperMmuGateStatus[i] = gateStatus.length() > 0 ? gateStatus.toInt() : -1;
      }
      xSemaphoreGive(dataMutex);
      if (!statusHttpEnded) {
        http.end();
      }
      return true;
    }

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    klipperHostAvailable = true;
    klipperAvailable = false;
    klipperMmuAvailable = false;
    klipperStatus = "JSON FEHLER";
    klipperDisplayMessage = "";
    xSemaphoreGive(dataMutex);
    Serial.println("Klipper Daten nicht in Antwort gefunden:");
    Serial.println(payload.substring(0, 240));
    if (!statusHttpEnded) {
      http.end();
    }
    return false;
  } else {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    klipperHostAvailable = true;
    klipperAvailable = false;
    klipperMmuAvailable = false;
    klipperStatus = "HTTP " + String(httpCodeKlipper);
    klipperDisplayMessage = "";
    xSemaphoreGive(dataMutex);
    Serial.print("Klipper Fehler: ");
    Serial.println(http.errorToString(httpCodeKlipper));
  }

  if (!statusHttpEnded) {
    http.end();
  }
  return false;
}

String formatUtcIsoTime(time_t value) {
  struct tm utcTime;
  gmtime_r(&value, &utcTime);

  char buffer[24];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
  return String(buffer);
}

String buildBtcCandlesUrl() {
  time_t nowTime = time(nullptr);
  const time_t rangeSeconds = (time_t)(BTC_DAY_CANDLE_COUNT + 5) * (time_t)BTC_CANDLE_SECONDS;
  time_t startTime = nowTime - rangeSeconds;

  return cryptoCandlesBaseUrl() +
    "?granularity=86400&start=" +
    formatUtcIsoTime(startTime) +
    "&end=" +
    formatUtcIsoTime(nowTime);
}

bool fetchBtcCandles() {
  time_t nowTime = time(nullptr);
  if (nowTime < 100000) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    btcCandleStatus = "CANDLE WARTET ZEIT";
    xSemaphoreGive(dataMutex);
    Serial.println("BTC Candles warten auf NTP-Zeit");
    return false;
  }

  WiFiClientSecure candleClient;
  candleClient.setInsecure();

  String candlesUrl = buildBtcCandlesUrl();

  HTTPClient http;
  if (!http.begin(candleClient, candlesUrl)) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    btcCandleStatus = "CANDLE HTTP BEGIN";
    xSemaphoreGive(dataMutex);
    return false;
  }

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);
  http.setTimeout(8000);
  http.addHeader("User-Agent", "ESP32-S3-HMI");
  int httpCodeCandles = http.GET();
  Serial.printf("BTC Candles HTTP: %d\n", httpCodeCandles);
  yieldFetchTask();

  if (httpCodeCandles == HTTP_CODE_OK) {
    String payload = readHttpPayloadChunked(http, 8192);
    yieldFetchTask();
    Serial.printf("BTC Candles Payload: %u Bytes\n", (unsigned)payload.length());
    if (parsedBtcCandles == nullptr && !initBtcStorage()) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      btcCandleStatus = "CANDLE RAM";
      btcDayDataReady = false;
      xSemaphoreGive(dataMutex);
      http.end();
      return false;
    }

    int parsedCount = parseCoinbaseCandles(payload, parsedBtcCandles, BTC_CANDLE_CAPACITY);

    if (parsedCount >= 2) {
      storeBtcCandles(parsedBtcCandles, parsedCount);
      Serial.printf(
        "BTC Candles geladen: %d, erstes=%u, letztes=%u\n",
        parsedCount,
        parsedBtcCandles[0].time,
        parsedBtcCandles[parsedCount - 1].time
      );
      http.end();
      return true;
    }

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    btcCandleStatus = "CANDLE JSON";
    btcDayDataReady = false;
    xSemaphoreGive(dataMutex);
    Serial.println("BTC Candles nicht in Antwort gefunden:");
    Serial.println(payload.substring(0, 240));
  } else {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    btcCandleStatus = "CANDLE HTTP " + String(httpCodeCandles);
    btcDayDataReady = false;
    xSemaphoreGive(dataMutex);
    Serial.print("BTC Candles Fehler: ");
    Serial.println(http.errorToString(httpCodeCandles));
  }

  http.end();
  return false;
}

// --- TASK: API ABFRAGEN ---
void fetchDataTask(void *pvParameters) {
  unsigned long nextBtcFetch = 0;
  unsigned long nextBtcCandleFetch = 20000;
  unsigned long nextWeatherFetch = 10000;
  unsigned long nextKlipperFetch = 5000;
  unsigned long nextKlipperNameFetch = 0;
  unsigned long lastWifiWaitLog = 0;

  for (;;) {
    wifiConnected = (WiFi.status() == WL_CONNECTED);

    if (wifiConnected) {
      unsigned long now = millis();

      if ((long)(now - nextBtcFetch) >= 0) {
        if (networkMutex != NULL) {
          xSemaphoreTake(networkMutex, portMAX_DELAY);
        }
        bool btcOk = fetchBtcPrice();
        if (networkMutex != NULL) {
          xSemaphoreGive(networkMutex);
        }
        nextBtcFetch = millis() + (btcOk ? btcRefreshInterval : btcRetryInterval);
        vTaskDelay(pdMS_TO_TICKS(500));
      }

      if ((long)(now - nextBtcCandleFetch) >= 0) {
        if (networkMutex != NULL) {
          xSemaphoreTake(networkMutex, portMAX_DELAY);
        }
        bool candleOk = fetchBtcCandles();
        if (networkMutex != NULL) {
          xSemaphoreGive(networkMutex);
        }
        nextBtcCandleFetch = millis() + (candleOk ? btcCandleRefreshInterval : btcCandleRetryInterval);
        vTaskDelay(pdMS_TO_TICKS(500));
      }

      if ((long)(now - nextWeatherFetch) >= 0) {
        if (networkMutex != NULL) {
          xSemaphoreTake(networkMutex, portMAX_DELAY);
        }
        bool weatherOk = fetchWeatherValue();
        if (networkMutex != NULL) {
          xSemaphoreGive(networkMutex);
        }
        nextWeatherFetch = millis() + (weatherOk ? weatherRefreshInterval : weatherRetryInterval);
        vTaskDelay(pdMS_TO_TICKS(500));
      }

      if ((long)(now - nextKlipperNameFetch) >= 0) {
        if (networkMutex != NULL) {
          xSemaphoreTake(networkMutex, portMAX_DELAY);
        }
        bool nameOk = fetchKlipperPrinterName();
        if (networkMutex != NULL) {
          xSemaphoreGive(networkMutex);
        }
        nextKlipperNameFetch = millis() + (nameOk ? klipperNameRefreshInterval : klipperNameRetryInterval);
        vTaskDelay(pdMS_TO_TICKS(500));
      }

      if ((long)(now - nextKlipperFetch) >= 0) {
        if (networkMutex != NULL) {
          xSemaphoreTake(networkMutex, portMAX_DELAY);
        }
        bool klipperOk = fetchKlipperStatus();
        if (networkMutex != NULL) {
          xSemaphoreGive(networkMutex);
        }
        nextKlipperFetch = millis() + (klipperOk ? klipperRefreshInterval : klipperRetryInterval);
        vTaskDelay(pdMS_TO_TICKS(500));
      }

      vTaskDelay(pdMS_TO_TICKS(1000));
    } else {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      klipperHostAvailable = false;
      klipperAvailable = false;
      klipperMmuAvailable = false;
      klipperConnectionState = "--";
      klipperConnectionMessage = "";
      klipperStatus = "WLAN WARTET";
      klipperDisplayMessage = "";
      xSemaphoreGive(dataMutex);

      unsigned long now = millis();
      if (now - lastWifiWaitLog >= 15000) {
        lastWifiWaitLog = now;
        Serial.printf("FetchData wartet auf WLAN. Status: %d\n", WiFi.status());
      }
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
  }
}
