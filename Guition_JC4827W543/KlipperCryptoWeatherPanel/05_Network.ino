static int wifiReconnectFailures = 0;
static const int wifiHardResetThreshold = 3;

#if DEBUG_ENABLED
static WebServer healthServer(80);
static bool healthServerStarted = false;

static void sendHealthFile(const char* path, const char* notFoundMessage) {
  if (!LittleFS.exists(path)) {
    healthServer.send(404, "text/plain", notFoundMessage);
    return;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    healthServer.send(500, "text/plain", "open failed");
    return;
  }
  healthServer.streamFile(f, "text/csv");
  f.close();
}

static void handleHealthCsv() {
  sendHealthFile("/health.csv", "no log yet");
}

static void handleHealthOldCsv() {
  sendHealthFile("/health.old.csv", "no rotated log");
}

static size_t fileSizeOrZero(const char* path) {
  if (!LittleFS.exists(path)) {
    return 0;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    return 0;
  }
  size_t size = f.size();
  f.close();
  return size;
}

static void handleHealthIndex() {
  String html;
  html.reserve(512);
  html += "<!doctype html><meta charset=utf-8><title>Health Log</title>";
  html += "<h1>ESP32 Health Log</h1><ul>";
  html += "<li><a href='/health.csv'>health.csv</a> (";
  html += String((unsigned long)fileSizeOrZero("/health.csv"));
  html += " Bytes)</li>";
  html += "<li><a href='/health.old.csv'>health.old.csv</a> (";
  html += String((unsigned long)fileSizeOrZero("/health.old.csv"));
  html += " Bytes)</li></ul>";
  html += "<p>Spalten: uptime_s,heap,min,maxAlloc,internalLargest,psramFree,fetchStackHWM,wifi,rssi</p>";
  healthServer.send(200, "text/html", html);
}

void startHealthServerIfNeeded() {
  if (healthServerStarted) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  healthServer.on("/", handleHealthIndex);
  healthServer.on("/health.csv", handleHealthCsv);
  healthServer.on("/health.old.csv", handleHealthOldCsv);
  healthServer.begin();
  healthServerStarted = true;
  Serial.print("Health-Server bereit: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");
}

void serviceHealthServer() {
  if (!healthServerStarted) {
    return;
  }
  healthServer.handleClient();
}
#endif // DEBUG_ENABLED

void updateWifiState() {
  bool connected = (WiFi.status() == WL_CONNECTED);
  bool wasConnected = wifiConnected;
  wifiConnected = connected;

  if (connected) {
    if (!wasConnected) {
      Serial.print("WLAN verbunden, IP: ");
      Serial.println(WiFi.localIP());
      Serial.printf("RSSI: %d dBm, Kanal: %d\n", WiFi.RSSI(), WiFi.channel());
      wifiReconnectFailures = 0;
    }
    if (!timeConfigured) {
      configureTimeOnce();
    }
#if DEBUG_ENABLED
    startHealthServerIfNeeded();
#endif
    return;
  }

  unsigned long now = millis();
  if (now - lastWifiReconnectAttempt >= wifiReconnectInterval) {
    if (networkMutex != NULL && xSemaphoreTake(networkMutex, 0) != pdTRUE) {
      return;
    }

    lastWifiReconnectAttempt = now;
    wifiReconnectFailures++;
    Serial.printf("WLAN getrennt, Versuch %d. Status: %d\n", wifiReconnectFailures, WiFi.status());

    if (wifiReconnectFailures >= wifiHardResetThreshold) {
      // Stack-Reset: nach mehreren Fehlversuchen kompletten Mode-Toggle erzwingen.
      Serial.println("WLAN Hard-Reset: WIFI_OFF -> WIFI_STA");
      WiFi.disconnect(true, true);
      WiFi.mode(WIFI_OFF);
      delay(200);
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(false);
      WiFi.begin(ssid, password);
      wifiReconnectFailures = 0;
    } else {
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }

    if (networkMutex != NULL) {
      xSemaphoreGive(networkMutex);
    }
  }
}

void beginWiFi() {
  if (networkMutex != NULL) {
    xSemaphoreTake(networkMutex, portMAX_DELAY);
  }

  // Kein NVS-Write pro Verbindung -> Flash-Wear und Latenz vermeiden.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  lastWifiReconnectAttempt = millis();
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  wifiReconnectFailures = 0;
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

// --- UTF-8 -> ASCII fuer Texte aus APIs (Ortsname, Klipper-Dateiname usw.) ---
// Die eingebauten Montserrat-Fonts enthalten nur ASCII + Grad-Zeichen + Symbole.
// Ohne Umwandlung fehlen bei "Muenchen", "Krakow" oder "Sao Paulo" Buchstaben.
// Deutsche Umlaute -> ae/oe/ue/ss, sonstige Akzente -> Grundbuchstabe,
// nicht darstellbare Zeichen -> '?'.

// Latin-1 U+00C0..U+00FF auf ASCII (Sonderfaelle wie Umlaute werden vorher behandelt).
static const char ASCII_FOLD_LATIN1[] =
  "AAAAAAACEEEEIIII" // C0..CF
  "DNOOOOOxOUUUUYTs" // D0..DF
  "aaaaaaaceeeeiiii" // E0..EF
  "dnooooo/ouuuuyty"; // F0..FF
static_assert(sizeof(ASCII_FOLD_LATIN1) == 64 + 1, "Latin-1 Fold-Tabelle muss 64 Eintraege haben");

// Latin Extended-A U+0100..U+017F (Polnisch, Tschechisch, Tuerkisch, Baltisch ...).
static const char ASCII_FOLD_LATIN_EXT_A[] =
  "AaAaAa" "CcCcCcCc" "DdDd" "EeEeEeEeEe" "GgGgGgGg" "HhHh" "IiIiIiIiIi" "Ii" "Jj" "Kkk"
  "LlLlLlLlLl" "NnNnNnnNn" "OoOoOoOo" "RrRrRr" "SsSsSsSs" "TtTtTt" "UuUuUuUuUuUu" "Ww" "YyY"
  "ZzZzZz" "s";
static_assert(sizeof(ASCII_FOLD_LATIN_EXT_A) == 128 + 1, "Latin-Ext-A Fold-Tabelle muss 128 Eintraege haben");

// Ein Unicode-Codepoint als ASCII-Ersatz anhaengen.
static void appendAsciiFold(String& out, uint32_t cp) {
  switch (cp) {
    case 0x00C4: out += "Ae"; return; // Ä
    case 0x00D6: out += "Oe"; return; // Ö
    case 0x00DC: out += "Ue"; return; // Ü
    case 0x00E4: out += "ae"; return; // ä
    case 0x00F6: out += "oe"; return; // ö
    case 0x00FC: out += "ue"; return; // ü
    case 0x00DF: out += "ss"; return; // ß
    case 0x00C6: out += "AE"; return; // Æ
    case 0x00E6: out += "ae"; return; // æ
    case 0x00DE: out += "Th"; return; // Þ
    case 0x00FE: out += "th"; return; // þ
    case 0x0152: out += "OE"; return; // Œ
    case 0x0153: out += "oe"; return; // œ
    case 0x00A0: out += ' '; return;  // geschuetztes Leerzeichen
    case 0x00B7: out += '.'; return;  // Mittelpunkt
    case 0x0218: out += 'S'; return;  // Ș (Rumaenisch)
    case 0x0219: out += 's'; return;  // ș
    case 0x021A: out += 'T'; return;  // Ț
    case 0x021B: out += 't'; return;  // ț
    case 0x2018:
    case 0x2019: out += '\''; return; // typografische Apostrophe
    case 0x201C:
    case 0x201D: out += '"'; return;  // typografische Anfuehrungszeichen
    case 0x2013:
    case 0x2014: out += '-'; return;  // Halbgeviert-/Geviertstrich
    case 0x2026: out += "..."; return;
    case 0x00B0: out += "\xc2\xb0"; return;     // Grad-Zeichen ist im Font enthalten
    case 0x2022: out += "\xe2\x80\xa2"; return; // Bullet ist im Font enthalten
    default: break;
  }

  if (cp >= 0x00C0 && cp <= 0x00FF) {
    out += ASCII_FOLD_LATIN1[cp - 0x00C0];
    return;
  }
  if (cp >= 0x0100 && cp <= 0x017F) {
    out += ASCII_FOLD_LATIN_EXT_A[cp - 0x0100];
    return;
  }
  out += '?';
}

String asciiFoldUtf8(const String& text) {
  String out;
  out.reserve(text.length() + 8);

  const size_t len = text.length();
  size_t i = 0;
  while (i < len) {
    const uint8_t c = (uint8_t)text[i];
    if (c < 0x80) {
      out += (char)c; // ASCII unveraendert
      i++;
      continue;
    }

    // Laenge der UTF-8-Sequenz aus dem Startbyte bestimmen.
    size_t seqLen = 0;
    uint32_t cp = 0;
    if ((c & 0xE0) == 0xC0) {
      seqLen = 2;
      cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      seqLen = 3;
      cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      seqLen = 4;
      cp = c & 0x07;
    }

    // Folgebytes pruefen; kaputtes UTF-8 (z.B. Latin-1-Bytes) -> '?'.
    bool valid = seqLen > 0 && i + seqLen <= len;
    for (size_t k = 1; valid && k < seqLen; k++) {
      const uint8_t cont = (uint8_t)text[i + k];
      if ((cont & 0xC0) != 0x80) {
        valid = false;
      } else {
        cp = (cp << 6) | (cont & 0x3F);
      }
    }

    if (!valid) {
      out += '?';
      i++;
      continue;
    }

    appendAsciiFold(out, cp);
    i += seqLen;
  }
  return out;
}
