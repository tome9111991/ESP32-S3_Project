void updateWifiState() {
  if (!wifiCredentialsConfigured() || isWifiSetupScreenActive()) {
    wifiConnected = false;
    wifiConnectedSince = 0;
    return;
  }

  bool connected = (WiFi.status() == WL_CONNECTED);
  bool wasConnected = wifiConnected;
  wifiConnected = connected;

  if (connected) {
    if (!wasConnected) {
      wifiConnectedSince = millis();
      Serial.print("WLAN verbunden, IP: ");
      Serial.println(WiFi.localIP());
      Serial.printf("RSSI: %d dBm, Kanal: %d\n", WiFi.RSSI(), WiFi.channel());
    } else if (wifiConnectedSince == 0) {
      wifiConnectedSince = millis();
    }
    if (!timeConfigured) {
      configureTimeOnce();
    }
    return;
  }

  if (wasConnected) {
    wifiConnectedSince = 0;
    internetAvailable = false;
    lastInternetProbe = 0;
  }

  unsigned long now = millis();
  if (lastWifiReconnectAttempt != 0 && now - lastWifiReconnectAttempt < wifiConnectAttemptTimeout) {
    return;
  }

  if (now - lastWifiReconnectAttempt >= wifiReconnectInterval) {
    if (networkMutex != NULL && xSemaphoreTake(networkMutex, 0) != pdTRUE) {
      return;
    }

    lastWifiReconnectAttempt = now;
    Serial.printf("WLAN getrennt, neuer Verbindungsversuch. Status: %d\n", WiFi.status());
    WiFi.disconnect(false, false);
    delay(100);
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

    if (networkMutex != NULL) {
      xSemaphoreGive(networkMutex);
    }
  }
}

void beginWiFi() {
  if (!wifiCredentialsConfigured()) {
    Serial.println("WLAN-Verbindung nicht gestartet: keine SSID konfiguriert");
    wifiConnected = false;
    wifiConnectedSince = 0;
    return;
  }

  if (networkMutex != NULL) {
    xSemaphoreTake(networkMutex, portMAX_DELAY);
  }

  WiFi.disconnect(false, false);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  lastWifiReconnectAttempt = millis();
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  wifiConnectedSince = wifiConnected ? millis() : 0;
  internetAvailable = false;
  lastInternetProbe = 0;
  Serial.printf("WLAN-Verbindung gestartet: '%s', Status: %d\n", wifiSsid.c_str(), WiFi.status());

  if (networkMutex != NULL) {
    xSemaphoreGive(networkMutex);
  }
}

bool checkInternetConnectivity() {
  if (WiFi.status() != WL_CONNECTED) {
    internetAvailable = false;
    return false;
  }

  const char* hosts[] = {
    "api.coinbase.com",
    "api.brightsky.dev"
  };

  for (size_t i = 0; i < sizeof(hosts) / sizeof(hosts[0]); i++) {
    IPAddress resolvedIp;
    int dnsOk = WiFi.hostByName(hosts[i], resolvedIp);
    if (dnsOk != 1 || resolvedIp == IPAddress(0, 0, 0, 0)) {
      Serial.printf("Internet Probe: %s DNS fehlgeschlagen\n", hosts[i]);
      continue;
    }

    WiFiClient probeClient;
    probeClient.setTimeout(3000);
    bool tcpOk = probeClient.connect(hosts[i], 443);
    probeClient.stop();
    Serial.printf(
      "Internet Probe: %s %s (%s), dns=%s, gateway=%s\n",
      hosts[i],
      tcpOk ? "OK" : "TCP 443 fehlgeschlagen",
      resolvedIp.toString().c_str(),
      WiFi.dnsIP().toString().c_str(),
      WiFi.gatewayIP().toString().c_str()
    );

    if (!tcpOk) {
      yieldFetchTask();
      continue;
    }

    internetAvailable = true;
    return true;
  }

  internetAvailable = false;
  return false;
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

String weatherStationNameForSource(JsonArrayConst sources, int sourceId) {
  if (sourceId < 0) {
    return "";
  }

  for (JsonObjectConst source : sources) {
    int id = source["id"] | -1;
    if (id != sourceId) {
      continue;
    }

    const char* name = source["station_name"] | "";
    String stationName = String(name);
    stationName.trim();
    return stationName;
  }

  return "";
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
