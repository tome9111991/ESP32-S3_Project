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
  setWifiText("WLAN startet");
  setStatusText("Verbinde mit WLAN");

  if (networkMutex != NULL) {
    xSemaphoreGive(networkMutex);
  }
}

void updateWifiState() {
  const bool connected = (WiFi.status() == WL_CONNECTED);
  const bool wasConnected = wifiConnected;
  wifiConnected = connected;

  if (connected) {
    if (!wasConnected) {
      Serial.print("WLAN verbunden, IP: ");
      Serial.println(WiFi.localIP());
      Serial.printf("RSSI: %d dBm, Kanal: %d\n", WiFi.RSSI(), WiFi.channel());
      setStatusText("WLAN verbunden");
    }

    if (!timeConfigured) {
      configureTimeOnce();
    }
    return;
  }

  setWifiText("WLAN getrennt");

  const unsigned long now = millis();
  if (now - lastWifiReconnectAttempt < wifiReconnectInterval) {
    return;
  }

  if (networkMutex != NULL && xSemaphoreTake(networkMutex, 0) != pdTRUE) {
    return;
  }

  lastWifiReconnectAttempt = now;
  Serial.printf("WLAN getrennt, neuer Verbindungsversuch. Status: %d\n", WiFi.status());
  setStatusText("WLAN reconnect");
  WiFi.disconnect();
  WiFi.begin(ssid, password);

  if (networkMutex != NULL) {
    xSemaphoreGive(networkMutex);
  }
}

bool configureTimeOnce() {
  if (timeConfigured || WiFi.status() != WL_CONNECTED) {
    return timeConfigured;
  }

  // Non-blocking NTP start; the UI checks later whether local time is ready.
  configTzTime(timezonePosix, "pool.ntp.org", "time.nist.gov");
  timeConfigured = true;
  Serial.println("NTP Zeitkonfiguration gestartet.");
  return true;
}
