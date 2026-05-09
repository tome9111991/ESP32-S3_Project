static constexpr const char* WIFI_SETTINGS_FILE = "/wifi_settings.json";
static constexpr uint32_t WIFI_SSID_MAX_LEN = 32;
static constexpr uint32_t WIFI_PASSWORD_MAX_LEN = 64;

// Liste aller persistenten Settings-Dateien fuer den Factory Reset.
// Beim Hinzufuegen neuer gespeicherter Einstellungen den Pfad hier ergaenzen.
static const char* const FACTORY_RESET_FILES[] = {
  "/wifi_settings.json",
  "/touch_cal.json",
  "/display_settings.json",
  "/screen_settings.json",
};
static constexpr size_t FACTORY_RESET_FILE_COUNT =
  sizeof(FACTORY_RESET_FILES) / sizeof(FACTORY_RESET_FILES[0]);
static constexpr unsigned long CLEAN_REBOOT_NETWORK_WAIT_MS = 10000;
static constexpr unsigned long CLEAN_REBOOT_STEP_WAIT_MS = 100;

static void waitForLvglBeforeReboot() {
  if (lvDisplay == nullptr) {
    delay(20);
    return;
  }

  lv_timer_handler();
  delay(20);
}

static void showCleanRebootOverlay() {
  if (lvDisplay == nullptr) {
    return;
  }

  lv_obj_t* parent = lv_screen_active();
  lv_obj_t* backdrop = lv_obj_create(parent);
  lv_obj_remove_style_all(backdrop);
  lv_obj_set_size(backdrop, LCD_W, LCD_H);
  lv_obj_set_pos(backdrop, 0, 0);
  lv_obj_set_style_bg_color(backdrop, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(backdrop, 220, 0);
  lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* panel = lv_obj_create(backdrop);
  styleFilledRect(panel, 0x171d26, 10);
  lv_obj_set_size(panel, 360, 150);
  lv_obj_set_pos(panel, (LCD_W - 360) / 2, (LCD_H - 150) / 2);
  lv_obj_set_style_border_width(panel, 2, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(COLOR_ORANGE), 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* label = createLabel(panel, &lv_font_montserrat_40, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(label, 320, 54);
  lv_obj_set_pos(label, 20, 48);
  lv_label_set_text(label, "Reboot...");

  waitForLvglBeforeReboot();
}

static bool takeNetworkMutexForReboot() {
  if (networkMutex == NULL) {
    return false;
  }

  const unsigned long started = millis();
  while (millis() - started < CLEAN_REBOOT_NETWORK_WAIT_MS) {
    if (xSemaphoreTake(networkMutex, pdMS_TO_TICKS(CLEAN_REBOOT_STEP_WAIT_MS)) == pdTRUE) {
      return true;
    }
    waitForLvglBeforeReboot();
  }
  return false;
}

static void shutdownWifiForReboot() {
  WiFi.disconnect(true, false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  wifiConnected = false;
  wifiConnectedSince = 0;
  internetAvailable = false;
}

void performCleanReboot() {
  if (cleanRebootRequested) {
    return;
  }

  cleanRebootRequested = true;
  Serial.println("Reboot: sauberer Neustart angefordert");
  showCleanRebootOverlay();

  bool networkMutexTaken = takeNetworkMutexForReboot();
  if (networkMutexTaken || networkMutex == NULL) {
    shutdownWifiForReboot();
  } else {
    Serial.println("Reboot: Netzwerk-Mutex Timeout, WLAN wird nicht separat gestoppt");
  }
  if (networkMutexTaken) {
    xSemaphoreGive(networkMutex);
  }

  if (settingsStorageReady) {
    LittleFS.end();
    settingsStorageReady = false;
    Serial.println("Reboot: Settings-Dateisystem geschlossen");
  }

  Serial.println("Reboot: Neustart");
  Serial.flush();
  delay(250);
  ESP.restart();

  while (true) {
    delay(1000);
  }
}

void performFactoryReset() {
  Serial.println("Factory Reset: loesche gespeicherte Einstellungen");
  if (settingsStorageReady) {
    for (size_t i = 0; i < FACTORY_RESET_FILE_COUNT; i++) {
      const char* path = FACTORY_RESET_FILES[i];
      if (!LittleFS.exists(path)) {
        continue;
      }
      if (LittleFS.remove(path)) {
        Serial.printf("Factory Reset: %s entfernt\n", path);
      } else {
        Serial.printf("Factory Reset: %s konnte nicht entfernt werden\n", path);
      }
    }
  } else {
    Serial.println("Factory Reset: Settings-Dateisystem nicht bereit");
  }
  performCleanReboot();
}

static lv_obj_t* wifiSetupScreen = nullptr;
static lv_obj_t* wifiSsidTextarea = nullptr;
static lv_obj_t* wifiPasswordTextarea = nullptr;
static lv_obj_t* wifiPasswordToggleLabel = nullptr;
static lv_obj_t* wifiKeyboardPanel = nullptr;
static lv_obj_t* wifiKeyboard = nullptr;
static lv_obj_t* wifiSetupStatusLabel = nullptr;
static lv_obj_t* wifiSaveButton = nullptr;

static void showWifiKeyboard() {
  if (wifiKeyboard == nullptr) {
    return;
  }

  if (wifiKeyboardPanel != nullptr) {
    lv_obj_remove_flag(wifiKeyboardPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(wifiKeyboardPanel);
  }
  lv_obj_remove_flag(wifiKeyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(wifiKeyboard);
}

static void hideWifiKeyboard() {
  if (wifiKeyboardPanel != nullptr) {
    lv_obj_add_flag(wifiKeyboardPanel, LV_OBJ_FLAG_HIDDEN);
  }
  if (wifiKeyboard != nullptr) {
    lv_obj_add_flag(wifiKeyboard, LV_OBJ_FLAG_HIDDEN);
  }
}

bool wifiCredentialsConfigured() {
  String ssidValue = wifiSsid;
  ssidValue.trim();
  return ssidValue.length() > 0;
}

bool initSettingsStorage() {
  settingsStorageReady = LittleFS.begin(true);
  if (!settingsStorageReady) {
    Serial.println("Settings-Dateisystem konnte nicht gestartet werden");
  }
  return settingsStorageReady;
}

bool loadWifiSettingsFromFile() {
  if (!settingsStorageReady || !LittleFS.exists(WIFI_SETTINGS_FILE)) {
    return false;
  }

  File settingsFile = LittleFS.open(WIFI_SETTINGS_FILE, "r");
  if (!settingsFile) {
    Serial.println("WLAN Settings-Datei konnte nicht gelesen werden");
    return false;
  }

  JsonDocument doc(&psramJsonAllocator);
  DeserializationError error = deserializeJson(doc, settingsFile);
  settingsFile.close();
  if (error) {
    Serial.print("WLAN Settings JSON Fehler: ");
    Serial.println(error.c_str());
    return false;
  }

  const char* storedSsid = doc["wifi"]["ssid"] | "";
  const char* storedPassword = doc["wifi"]["password"] | "";
  String ssidValue = String(storedSsid);
  ssidValue.trim();
  if (ssidValue.length() == 0) {
    return false;
  }

  wifiSsid = ssidValue;
  wifiPassword = String(storedPassword);
  Serial.println("WLAN Settings aus interner Datei geladen");
  return true;
}

bool saveWifiSettingsToFile(const String& ssidValue, const String& passwordValue) {
  if (!settingsStorageReady) {
    return false;
  }

  JsonDocument doc(&psramJsonAllocator);
  doc["wifi"]["ssid"] = ssidValue;
  doc["wifi"]["password"] = passwordValue;

  File settingsFile = LittleFS.open(WIFI_SETTINGS_FILE, "w");
  if (!settingsFile) {
    return false;
  }

  size_t written = serializeJson(doc, settingsFile);
  settingsFile.close();
  return written > 0;
}

bool isWifiSetupScreenActive() {
  return wifiSetupActive;
}

static void setWifiSetupStatus(const char* text, uint32_t color) {
  if (wifiSetupStatusLabel == nullptr) {
    return;
  }
  lv_obj_set_style_text_color(wifiSetupStatusLabel, lv_color_hex(color), 0);
  lv_label_set_text(wifiSetupStatusLabel, text);
}

static void destroyWifiSetupScreen() {
  if (wifiSetupScreen == nullptr) {
    return;
  }

  lv_obj_delete_async(wifiSetupScreen);
  wifiSetupScreen = nullptr;
  wifiSsidTextarea = nullptr;
  wifiPasswordTextarea = nullptr;
  wifiPasswordToggleLabel = nullptr;
  wifiKeyboardPanel = nullptr;
  wifiKeyboard = nullptr;
  wifiSetupStatusLabel = nullptr;
  wifiSaveButton = nullptr;
}

static lv_obj_t* createWifiSetupText(lv_obj_t* parent, const lv_font_t* font, uint32_t color, int x, int y, int w, int h, const char* text) {
  lv_obj_t* label = createLabel(parent, font, color, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(label, w, h);
  lv_obj_set_pos(label, x, y);
  lv_label_set_text(label, text);
  return label;
}

static lv_obj_t* createWifiSetupTextarea(int x, int y, const char* placeholder, bool passwordMode) {
  lv_obj_t* textarea = lv_textarea_create(wifiSetupScreen);
  lv_obj_set_size(textarea, 560, 50);
  lv_obj_set_pos(textarea, x, y);
  lv_obj_set_style_bg_color(textarea, lv_color_hex(0x151b24), 0);
  lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(textarea, 2, 0);
  lv_obj_set_style_border_color(textarea, lv_color_hex(COLOR_DIM), 0);
  lv_obj_set_style_border_color(textarea, lv_color_hex(COLOR_CYAN), LV_STATE_FOCUSED);
  lv_obj_set_style_radius(textarea, 6, 0);
  lv_obj_set_style_pad_left(textarea, 18, 0);
  lv_obj_set_style_pad_right(textarea, 18, 0);
  lv_obj_set_style_text_font(textarea, &lv_font_montserrat_30, 0);
  lv_obj_set_style_text_color(textarea, lv_color_hex(COLOR_TEXT), 0);
  lv_textarea_set_one_line(textarea, true);
  lv_textarea_set_placeholder_text(textarea, placeholder);
  lv_textarea_set_max_length(textarea, passwordMode ? WIFI_PASSWORD_MAX_LEN : WIFI_SSID_MAX_LEN);
  lv_textarea_set_password_mode(textarea, passwordMode);
  return textarea;
}

static void focusWifiTextarea(lv_obj_t* textarea) {
  if (wifiKeyboard == nullptr || textarea == nullptr) {
    return;
  }
  showWifiKeyboard();
  lv_keyboard_set_textarea(wifiKeyboard, textarea);
  lv_obj_add_state(textarea, LV_STATE_FOCUSED);
  if (textarea == wifiSsidTextarea) {
    lv_obj_remove_state(wifiPasswordTextarea, LV_STATE_FOCUSED);
  } else {
    lv_obj_remove_state(wifiSsidTextarea, LV_STATE_FOCUSED);
  }
}

static void wifiTextareaEvent(lv_event_t* event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    focusWifiTextarea(lv_event_get_target_obj(event));
  } else if (code == LV_EVENT_DEFOCUSED) {
    hideWifiKeyboard();
  }
}

static void wifiKeyboardEvent(lv_event_t* event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    if (wifiSsidTextarea != nullptr) {
      lv_obj_remove_state(wifiSsidTextarea, LV_STATE_FOCUSED);
    }
    if (wifiPasswordTextarea != nullptr) {
      lv_obj_remove_state(wifiPasswordTextarea, LV_STATE_FOCUSED);
    }
    hideWifiKeyboard();
  }
}

static void wifiScreenClickEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  if (lv_event_get_target_obj(event) != wifiSetupScreen) {
    return;
  }
  if (wifiSsidTextarea != nullptr) {
    lv_obj_remove_state(wifiSsidTextarea, LV_STATE_FOCUSED);
  }
  if (wifiPasswordTextarea != nullptr) {
    lv_obj_remove_state(wifiPasswordTextarea, LV_STATE_FOCUSED);
  }
  hideWifiKeyboard();
}

static void saveWifiSetupCredentials() {
  String ssidValue = String(lv_textarea_get_text(wifiSsidTextarea));
  String passwordValue = String(lv_textarea_get_text(wifiPasswordTextarea));
  ssidValue.trim();
  if (ssidValue.length() == 0) {
    setWifiSetupStatus("SSID fehlt", COLOR_LOSS);
    focusWifiTextarea(wifiSsidTextarea);
    return;
  }

  setWifiSetupStatus("Speichere WLAN-Daten", COLOR_MUTED);
  if (!saveWifiSettingsToFile(ssidValue, passwordValue)) {
    setWifiSetupStatus("Speichern fehlgeschlagen", COLOR_LOSS);
    return;
  }

  wifiSsid = ssidValue;
  wifiPassword = passwordValue;
  setWifiSetupStatus("Gespeichert, WLAN startet", COLOR_CYAN);
  Serial.println("WLAN Settings gespeichert");
  startNetworkServices();
  destroyWifiSetupScreen();
}

static void wifiSaveEvent(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    saveWifiSetupCredentials();
  }
}

static void wifiPasswordToggleEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  if (wifiPasswordTextarea == nullptr) {
    return;
  }
  bool currentlyHidden = lv_textarea_get_password_mode(wifiPasswordTextarea);
  bool showPlain = currentlyHidden;
  lv_textarea_set_password_mode(wifiPasswordTextarea, !showPlain);
  if (wifiPasswordToggleLabel != nullptr) {
    lv_label_set_text(wifiPasswordToggleLabel, showPlain ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
  }
  focusWifiTextarea(wifiPasswordTextarea);
}

static void wifiBackButtonEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  wifiSetupActive = false;
  destroyWifiSetupScreen();
  openSettingsMenuScreen();
}

static lv_obj_t* createWifiBackButton(lv_obj_t* parent) {
  lv_obj_t* btn = lv_obj_create(parent);
  styleFilledRect(btn, 0x232b38, 8);
  lv_obj_set_size(btn, 52, 52);
  lv_obj_set_pos(btn, LCD_W - 52 - 24, 30);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(COLOR_DIM), 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(btn, wifiBackButtonEvent, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* label = createLabel(btn, &lv_font_montserrat_24, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(label, LV_SYMBOL_LEFT);
  lv_obj_center(label);
  lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
  return btn;
}

static void createWifiSetupScreen() {
  if (wifiSetupScreen != nullptr) {
    return;
  }

  wifiSetupScreen = createScreen();
  createAccent(wifiSetupScreen, COLOR_CYAN);
  lv_obj_add_flag(wifiSetupScreen, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(wifiSetupScreen, wifiScreenClickEvent, LV_EVENT_CLICKED, nullptr);
  createWifiSetupText(wifiSetupScreen, &lv_font_montserrat_40, COLOR_TEXT, 96, 26, 560, 52, "WLAN einrichten");
  createWifiBackButton(wifiSetupScreen);
  createWifiSetupText(wifiSetupScreen, &lv_font_montserrat_24, COLOR_MUTED, 40, 110, 140, 30, "SSID");
  createWifiSetupText(wifiSetupScreen, &lv_font_montserrat_24, COLOR_MUTED, 40, 172, 140, 30, "Passwort");

  wifiSsidTextarea = createWifiSetupTextarea(190, 100, "WLAN-Name", false);
  wifiPasswordTextarea = createWifiSetupTextarea(190, 162, "WLAN-Passwort", true);
  lv_obj_set_style_pad_right(wifiPasswordTextarea, 56, 0);
  lv_obj_add_event_cb(wifiSsidTextarea, wifiTextareaEvent, LV_EVENT_ALL, nullptr);
  lv_obj_add_event_cb(wifiPasswordTextarea, wifiTextareaEvent, LV_EVENT_ALL, nullptr);

  lv_obj_t* passwordToggle = lv_obj_create(wifiSetupScreen);
  lv_obj_set_size(passwordToggle, 50, 50);
  lv_obj_set_pos(passwordToggle, 190 + 560 - 50, 162);
  lv_obj_set_style_bg_opa(passwordToggle, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(passwordToggle, 0, 0);
  lv_obj_set_style_pad_all(passwordToggle, 0, 0);
  lv_obj_set_style_radius(passwordToggle, 0, 0);
  lv_obj_add_flag(passwordToggle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(passwordToggle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(passwordToggle, wifiPasswordToggleEvent, LV_EVENT_CLICKED, nullptr);

  wifiPasswordToggleLabel = createLabel(passwordToggle, &lv_font_montserrat_30, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(wifiPasswordToggleLabel, LV_SYMBOL_EYE_OPEN);
  lv_obj_align(wifiPasswordToggleLabel, LV_ALIGN_CENTER, 0, -6);
  lv_obj_add_flag(wifiPasswordToggleLabel, LV_OBJ_FLAG_EVENT_BUBBLE);

  wifiSetupStatusLabel = createLabel(wifiSetupScreen, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(wifiSetupStatusLabel, 460, 32);
  lv_obj_set_pos(wifiSetupStatusLabel, 40, 232);

  wifiSaveButton = lv_obj_create(wifiSetupScreen);
  styleFilledRect(wifiSaveButton, COLOR_CYAN, 6);
  lv_obj_set_size(wifiSaveButton, 210, 44);
  lv_obj_set_pos(wifiSaveButton, 540, 226);
  lv_obj_add_flag(wifiSaveButton, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(wifiSaveButton, wifiSaveEvent, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* saveLabel = createLabel(wifiSaveButton, &lv_font_montserrat_24, COLOR_BG, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(saveLabel, 210, 40);
  lv_obj_set_pos(saveLabel, 0, 8);
  lv_label_set_text(saveLabel, "Speichern");
  lv_obj_add_flag(saveLabel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(saveLabel, wifiSaveEvent, LV_EVENT_CLICKED, nullptr);

  wifiKeyboardPanel = lv_obj_create(wifiSetupScreen);
  styleFilledRect(wifiKeyboardPanel, 0x101720, 0);
  lv_obj_set_size(wifiKeyboardPanel, 800, 200);
  lv_obj_set_pos(wifiKeyboardPanel, 0, 280);
  lv_obj_set_style_border_width(wifiKeyboardPanel, 0, 0);
  lv_obj_clear_flag(wifiKeyboardPanel, LV_OBJ_FLAG_SCROLLABLE);

  wifiKeyboard = lv_keyboard_create(wifiKeyboardPanel);
  lv_obj_set_size(wifiKeyboard, 776, 184);
  lv_obj_set_pos(wifiKeyboard, 12, 8);
  lv_keyboard_set_mode(wifiKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_set_style_bg_color(wifiKeyboard, lv_color_hex(0x101720), 0);
  lv_obj_set_style_bg_opa(wifiKeyboard, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(wifiKeyboard, 0, 0);
  lv_obj_set_style_text_font(wifiKeyboard, &lv_font_montserrat_24, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(wifiKeyboard, lv_color_hex(0x18202b), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(wifiKeyboard, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_border_width(wifiKeyboard, 1, LV_PART_ITEMS);
  lv_obj_set_style_border_color(wifiKeyboard, lv_color_hex(COLOR_DIM), LV_PART_ITEMS);
  lv_obj_set_style_text_color(wifiKeyboard, lv_color_hex(COLOR_TEXT), LV_PART_ITEMS);
  lv_obj_set_style_text_opa(wifiKeyboard, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_radius(wifiKeyboard, 5, LV_PART_ITEMS);
  lv_obj_set_style_pad_all(wifiKeyboard, 4, 0);
  lv_obj_set_style_pad_row(wifiKeyboard, 5, 0);
  lv_obj_set_style_pad_column(wifiKeyboard, 5, 0);
  lv_obj_add_event_cb(wifiKeyboard, wifiKeyboardEvent, LV_EVENT_ALL, nullptr);

  hideWifiKeyboard();
}

void openWifiSetupScreen(const char* message) {
  createWifiSetupScreen();
  wifiSetupActive = true;
  closePopupMenu();

  lv_textarea_set_text(wifiSsidTextarea, wifiSsid.c_str());
  lv_textarea_set_text(wifiPasswordTextarea, wifiPassword.c_str());
  lv_obj_remove_state(wifiSsidTextarea, LV_STATE_FOCUSED);
  lv_obj_remove_state(wifiPasswordTextarea, LV_STATE_FOCUSED);
  hideWifiKeyboard();
  setWifiSetupStatus(message != nullptr ? message : "WLAN-Daten eingeben", COLOR_MUTED);

  lv_screen_load(wifiSetupScreen);
  lastScreenSwitch = millis();
  lastUiRefresh = millis();
}
