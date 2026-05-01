void updateWifiState() {
  bool connected = (WiFi.status() == WL_CONNECTED);
  bool wasConnected = wifiConnected;
  wifiConnected = connected;

  if (connected) {
    if (!wasConnected) {
      Serial.print("WLAN verbunden, IP: ");
      Serial.println(WiFi.localIP());
      Serial.printf("RSSI: %d dBm, Kanal: %d\n", WiFi.RSSI(), WiFi.channel());
      configureTimeOnce();
    }
    return;
  }

  unsigned long now = millis();
  if (now - lastWifiReconnectAttempt >= wifiReconnectInterval) {
    if (networkMutex != NULL && xSemaphoreTake(networkMutex, 0) != pdTRUE) {
      return;
    }

    lastWifiReconnectAttempt = now;
    Serial.printf("WLAN getrennt, neuer Verbindungsversuch. Status: %d\n", WiFi.status());
    WiFi.disconnect();
    WiFi.begin(ssid, password);

    if (networkMutex != NULL) {
      xSemaphoreGive(networkMutex);
    }
  }
}

void beginWiFi() {
  if (networkMutex != NULL) {
    xSemaphoreTake(networkMutex, portMAX_DELAY);
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);
  lastWifiReconnectAttempt = millis();
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  Serial.printf("WLAN-Verbindung gestartet: '%s', Status: %d\n", ssid, WiFi.status());

  if (networkMutex != NULL) {
    xSemaphoreGive(networkMutex);
  }
}

bool connectWiFi(unsigned long timeoutMs) {
  if (networkMutex != NULL) {
    xSemaphoreTake(networkMutex, portMAX_DELAY);
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  Serial.printf("Verbinde mit WLAN '%s'", ssid);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    Serial.print(".");
    lv_timer_handler();
    delay(500);
  }
  Serial.println();

  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) {
    Serial.print("WLAN verbunden, IP: ");
    Serial.println(WiFi.localIP());
    Serial.printf("RSSI: %d dBm, Kanal: %d\n", WiFi.RSSI(), WiFi.channel());
  } else {
    Serial.printf("WLAN nicht verbunden. Status: %d\n", WiFi.status());
  }

  if (networkMutex != NULL) {
    xSemaphoreGive(networkMutex);
  }

  return wifiConnected;
}

String extractJsonNumber(const String& payload, const String& key) {
  String marker = "\"" + key + "\":";
  int searchFrom = 0;

  while (true) {
    int keyIndex = payload.indexOf(marker, searchFrom);
    if (keyIndex < 0) {
      return "";
    }

    int start = keyIndex + marker.length();
    while (start < payload.length() && payload[start] == ' ') {
      start++;
    }

    if (start >= payload.length()) {
      return "";
    }

    char first = payload[start];
    bool startsWithNumber = (first >= '0' && first <= '9') || first == '-' || first == '.';
    if (!startsWithNumber) {
      searchFrom = start + 1;
      continue;
    }

    int end = start;
    while (end < payload.length()) {
      char c = payload[end];
      if (c == ',' || c == '}' || c == ']') {
        break;
      }
      end++;
    }

    String value = payload.substring(start, end);
    value.trim();
    return value;
  }
}

String extractJsonString(const String& payload, const String& key) {
  String marker = "\"" + key + "\"";
  int keyIndex = payload.indexOf(marker);
  if (keyIndex < 0) {
    return "";
  }

  int start = keyIndex + marker.length();
  while (start < payload.length() && payload[start] == ' ') {
    start++;
  }
  if (start >= payload.length() || payload[start] != ':') {
    return "";
  }
  start++;
  while (start < payload.length() && payload[start] == ' ') {
    start++;
  }
  if (start >= payload.length() || payload[start] != '"') {
    return "";
  }
  start++;

  int end = payload.indexOf("\"", start);
  if (end <= start) {
    return "";
  }

  return payload.substring(start, end);
}

int weatherCodeFromText(String text) {
  text.toLowerCase();

  if (text.indexOf("thunder") >= 0 || text.indexOf("storm") >= 0 || text.indexOf("gewitter") >= 0) {
    return 95;
  }
  if (text.indexOf("snow") >= 0 || text.indexOf("sleet") >= 0 || text.indexOf("hail") >= 0 || text.indexOf("schnee") >= 0) {
    return 71;
  }
  if (text.indexOf("rain") >= 0 || text.indexOf("drizzle") >= 0 || text.indexOf("regen") >= 0) {
    return 61;
  }
  if (text.indexOf("fog") >= 0 || text.indexOf("mist") >= 0 || text.indexOf("nebel") >= 0) {
    return 45;
  }
  if (text.indexOf("partly") >= 0) {
    return 2;
  }
  if (text.indexOf("cloud") >= 0 || text.indexOf("overcast") >= 0 || text.indexOf("wolke") >= 0) {
    return 3;
  }
  if (text.indexOf("clear") >= 0 || text.indexOf("sun") >= 0 || text.indexOf("dry") >= 0 || text.indexOf("sonne") >= 0) {
    return 0;
  }

  return -1;
}
