static constexpr const char* SCREEN_SETTINGS_FILE = "/screen_settings.json";
static constexpr int SCREEN_SETTINGS_OPTION_COUNT = 4;

static bool screenSettingsActive = false;
static lv_obj_t* screenSettingsScreen = nullptr;
static lv_obj_t* screenSettingsTile[SCREEN_SETTINGS_OPTION_COUNT] = {};
static lv_obj_t* screenSettingsTitleLabel[SCREEN_SETTINGS_OPTION_COUNT] = {};
static lv_obj_t* screenSettingsUnderline[SCREEN_SETTINGS_OPTION_COUNT] = {};
static lv_obj_t* screenSettingsStateLabel[SCREEN_SETTINGS_OPTION_COUNT] = {};
static lv_obj_t* screenSettingsStatusLabel = nullptr;
static uint8_t screenSettingsTileIndex[SCREEN_SETTINGS_OPTION_COUNT] = {0, 1, 2, 3};
static bool screenSettingsLastSavedTime = true;
static bool screenSettingsLastSavedCrypto = true;
static bool screenSettingsLastSavedBtcDay = true;
static bool screenSettingsLastSavedKlipper = true;

bool isScreenSettingsScreenActive() {
  return screenSettingsActive;
}

static uint8_t screenSettingsStateForIndex(uint8_t index) {
  if (index == 1) {
    return (uint8_t)SCREEN_CRYPTO;
  }
  if (index == 2) {
    return (uint8_t)SCREEN_BTC_DAY;
  }
  if (index == 3) {
    return (uint8_t)SCREEN_KLIPPER;
  }
  return (uint8_t)SCREEN_TIME;
}

static const char* screenSettingsTitleForIndex(uint8_t index) {
  if (index == 1) {
    return "PREIS";
  }
  if (index == 2) {
    return "CHART";
  }
  if (index == 3) {
    return "KLIPPER";
  }
  return "UHR";
}

static const char* screenSettingsHintForIndex(uint8_t index) {
  if (index == 1) {
    return "Live-Kurs";
  }
  if (index == 2) {
    return "Kerzenchart";
  }
  if (index == 3) {
    return "Druckerstatus";
  }
  return "Zeit und Wetter";
}

static uint32_t screenSettingsColorForIndex(uint8_t index) {
  if (index == 3) {
    return COLOR_GREEN;
  }
  if (index == 1 || index == 2) {
    return COLOR_BTC;
  }
  return COLOR_CYAN;
}

static void screenSettingsSetEnabled(uint8_t state, bool enabled) {
  if (state == (uint8_t)SCREEN_CRYPTO) {
    screenCryptoEnabled = enabled;
  } else if (state == (uint8_t)SCREEN_BTC_DAY) {
    screenBtcDayEnabled = enabled;
  } else if (state == (uint8_t)SCREEN_KLIPPER) {
    screenKlipperEnabled = enabled;
  } else {
    screenTimeEnabled = enabled;
  }
}

static int enabledScreenCount() {
  int count = 0;
  if (screenTimeEnabled) count++;
  if (screenCryptoEnabled) count++;
  if (screenBtcDayEnabled) count++;
  if (screenKlipperEnabled) count++;
  return count;
}

static void ensureAtLeastOneScreenEnabled() {
  if (enabledScreenCount() > 0) {
    return;
  }
  screenTimeEnabled = true;
}

bool loadScreenSettingsFromFile() {
  if (!settingsStorageReady || !LittleFS.exists(SCREEN_SETTINGS_FILE)) {
    return false;
  }

  File file = LittleFS.open(SCREEN_SETTINGS_FILE, "r");
  if (!file) {
    return false;
  }

  JsonDocument doc(&psramJsonAllocator);
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    Serial.print("Screen Settings JSON Fehler: ");
    Serial.println(err.c_str());
    return false;
  }

  bool loaded = false;
  if (doc["time"].is<bool>()) {
    screenTimeEnabled = doc["time"].as<bool>();
    loaded = true;
  }
  if (doc["price"].is<bool>()) {
    screenCryptoEnabled = doc["price"].as<bool>();
    loaded = true;
  }
  if (doc["chart"].is<bool>()) {
    screenBtcDayEnabled = doc["chart"].as<bool>();
    loaded = true;
  }
  if (doc["klipper"].is<bool>()) {
    screenKlipperEnabled = doc["klipper"].as<bool>();
    loaded = true;
  }

  ensureAtLeastOneScreenEnabled();
  Serial.printf(
    "Screen Settings geladen: time=%u price=%u chart=%u klipper=%u\n",
    screenTimeEnabled ? 1 : 0,
    screenCryptoEnabled ? 1 : 0,
    screenBtcDayEnabled ? 1 : 0,
    screenKlipperEnabled ? 1 : 0
  );
  return loaded;
}

bool saveScreenSettingsToFile() {
  if (!settingsStorageReady) {
    return false;
  }

  ensureAtLeastOneScreenEnabled();
  JsonDocument doc(&psramJsonAllocator);
  doc["time"] = screenTimeEnabled;
  doc["price"] = screenCryptoEnabled;
  doc["chart"] = screenBtcDayEnabled;
  doc["klipper"] = screenKlipperEnabled;

  File file = LittleFS.open(SCREEN_SETTINGS_FILE, "w");
  if (!file) {
    return false;
  }
  size_t written = serializeJson(doc, file);
  file.close();
  return written > 0;
}

static bool screenSettingsChanged() {
  return screenSettingsLastSavedTime != screenTimeEnabled ||
         screenSettingsLastSavedCrypto != screenCryptoEnabled ||
         screenSettingsLastSavedBtcDay != screenBtcDayEnabled ||
         screenSettingsLastSavedKlipper != screenKlipperEnabled;
}

static void destroyScreenSettingsScreen() {
  if (screenSettingsScreen == nullptr) {
    return;
  }
  lv_obj_delete_async(screenSettingsScreen);
  screenSettingsScreen = nullptr;
  for (int i = 0; i < SCREEN_SETTINGS_OPTION_COUNT; i++) {
    screenSettingsTile[i] = nullptr;
    screenSettingsTitleLabel[i] = nullptr;
    screenSettingsUnderline[i] = nullptr;
    screenSettingsStateLabel[i] = nullptr;
  }
  screenSettingsStatusLabel = nullptr;
}

static void updateScreenSettingsTile(uint8_t index) {
  if (index >= SCREEN_SETTINGS_OPTION_COUNT || screenSettingsTile[index] == nullptr) {
    return;
  }

  bool enabled = isScreenEnabled((ScreenState)screenSettingsStateForIndex(index));
  uint32_t accentColor = screenSettingsColorForIndex(index);
  lv_obj_set_style_bg_color(
    screenSettingsTile[index],
    lv_color_hex(enabled ? 0x151b24 : 0x11161d),
    0
  );
  lv_obj_set_style_border_color(
    screenSettingsTile[index],
    lv_color_hex(enabled ? accentColor : COLOR_DIM),
    0
  );
  lv_obj_set_style_text_color(
    screenSettingsTitleLabel[index],
    lv_color_hex(enabled ? COLOR_TEXT : COLOR_DIM),
    0
  );
  lv_obj_set_style_bg_color(
    screenSettingsUnderline[index],
    lv_color_hex(enabled ? accentColor : COLOR_DIM),
    0
  );
  lv_obj_set_style_text_color(
    screenSettingsStateLabel[index],
    lv_color_hex(enabled ? accentColor : COLOR_MUTED),
    0
  );
  lv_label_set_text(screenSettingsStateLabel[index], enabled ? "Aktiv" : "Aus");
}

static void updateScreenSettingsUi() {
  for (uint8_t i = 0; i < SCREEN_SETTINGS_OPTION_COUNT; i++) {
    updateScreenSettingsTile(i);
  }

  if (screenSettingsStatusLabel == nullptr) {
    return;
  }

  if (enabledScreenCount() <= 1) {
    lv_obj_set_style_text_color(screenSettingsStatusLabel, lv_color_hex(COLOR_ORANGE), 0);
    lv_label_set_text(screenSettingsStatusLabel, "Mindestens eine Seite muss aktiv bleiben");
  } else if (screenSettingsChanged()) {
    lv_obj_set_style_text_color(screenSettingsStatusLabel, lv_color_hex(COLOR_CYAN), 0);
    lv_label_set_text(screenSettingsStatusLabel, "Auswahl wird beim Zurueckgehen gespeichert");
  } else {
    lv_obj_set_style_text_color(screenSettingsStatusLabel, lv_color_hex(COLOR_MUTED), 0);
    lv_label_set_text(screenSettingsStatusLabel, "Klipper erscheint nur, wenn der Drucker erreichbar ist");
  }
}

static void screenSettingsTileEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  uint8_t* indexPtr = (uint8_t*)lv_event_get_user_data(event);
  if (indexPtr == nullptr || *indexPtr >= SCREEN_SETTINGS_OPTION_COUNT) {
    return;
  }

  uint8_t state = screenSettingsStateForIndex(*indexPtr);
  bool currentlyEnabled = isScreenEnabled((ScreenState)state);
  if (currentlyEnabled && enabledScreenCount() <= 1) {
    updateScreenSettingsUi();
    return;
  }

  screenSettingsSetEnabled(state, !currentlyEnabled);
  updateScreenSettingsUi();
}

static void closeScreenSettingsScreen() {
  screenSettingsActive = false;
  ensureAtLeastOneScreenEnabled();

  if (screenSettingsChanged()) {
    if (saveScreenSettingsToFile()) {
      Serial.printf(
        "Screen Settings gespeichert: time=%u price=%u chart=%u klipper=%u\n",
        screenTimeEnabled ? 1 : 0,
        screenCryptoEnabled ? 1 : 0,
        screenBtcDayEnabled ? 1 : 0,
        screenKlipperEnabled ? 1 : 0
      );
      screenSettingsLastSavedTime = screenTimeEnabled;
      screenSettingsLastSavedCrypto = screenCryptoEnabled;
      screenSettingsLastSavedBtcDay = screenBtcDayEnabled;
      screenSettingsLastSavedKlipper = screenKlipperEnabled;
    } else {
      Serial.println("Screen Settings: Speichern fehlgeschlagen");
    }
  }

  destroyScreenSettingsScreen();
  openSettingsMenuScreen();
}

static void screenSettingsBackEvent(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    closeScreenSettingsScreen();
  }
}

static lv_obj_t* createScreenSettingsBackButton(lv_obj_t* parent) {
  lv_obj_t* btn = lv_obj_create(parent);
  styleFilledRect(btn, 0x232b38, 8);
  lv_obj_set_size(btn, 52, 52);
  lv_obj_set_pos(btn, LCD_W - 52 - 24, 30);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(COLOR_DIM), 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(btn, screenSettingsBackEvent, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* label = createLabel(btn, &lv_font_montserrat_24, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(label, LV_SYMBOL_LEFT);
  lv_obj_center(label);
  lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
  return btn;
}

static void createScreenSettingsTile(lv_obj_t* parent, uint8_t index, int x, int y) {
  lv_obj_t* tile = lv_obj_create(parent);
  screenSettingsTile[index] = tile;
  styleFilledRect(tile, 0x151b24, 8);
  lv_obj_set_size(tile, 300, 116);
  lv_obj_set_pos(tile, x, y);
  lv_obj_set_style_border_width(tile, 2, 0);
  lv_obj_set_style_border_color(tile, lv_color_hex(COLOR_DIM), 0);
  lv_obj_set_style_pad_all(tile, 0, 0);
  lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(tile, screenSettingsTileEvent, LV_EVENT_CLICKED, &screenSettingsTileIndex[index]);

  screenSettingsTitleLabel[index] = createLabel(tile, &lv_font_montserrat_30, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(screenSettingsTitleLabel[index], 260, 38);
  lv_obj_set_pos(screenSettingsTitleLabel[index], 20, 20);
  lv_label_set_text(screenSettingsTitleLabel[index], screenSettingsTitleForIndex(index));
  lv_obj_add_flag(screenSettingsTitleLabel[index], LV_OBJ_FLAG_EVENT_BUBBLE);

  screenSettingsUnderline[index] = createDivider(tile, 70, 64, 160, screenSettingsColorForIndex(index));
  lv_obj_add_flag(screenSettingsUnderline[index], LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t* hint = createLabel(tile, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(hint, 160, 28);
  lv_obj_set_pos(hint, 28, 82);
  lv_label_set_text(hint, screenSettingsHintForIndex(index));
  lv_obj_add_flag(hint, LV_OBJ_FLAG_EVENT_BUBBLE);

  screenSettingsStateLabel[index] = createLabel(tile, &lv_font_montserrat_24, COLOR_CYAN, LV_TEXT_ALIGN_RIGHT);
  lv_obj_set_size(screenSettingsStateLabel[index], 82, 28);
  lv_obj_set_pos(screenSettingsStateLabel[index], 190, 82);
  lv_obj_add_flag(screenSettingsStateLabel[index], LV_OBJ_FLAG_EVENT_BUBBLE);

  updateScreenSettingsTile(index);
}

static void createScreenSettingsScreen() {
  destroyScreenSettingsScreen();

  screenSettingsScreen = createScreen();
  createAccent(screenSettingsScreen, COLOR_CYAN);

  lv_obj_t* title = createLabel(screenSettingsScreen, &lv_font_montserrat_40, COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(title, 500, 52);
  lv_obj_set_pos(title, 100, 32);
  lv_label_set_text(title, "Screens");

  createScreenSettingsBackButton(screenSettingsScreen);

  createScreenSettingsTile(screenSettingsScreen, 0, 100, 126);
  createScreenSettingsTile(screenSettingsScreen, 1, 400, 126);
  createScreenSettingsTile(screenSettingsScreen, 2, 100, 260);
  createScreenSettingsTile(screenSettingsScreen, 3, 400, 260);

  screenSettingsStatusLabel = createLabel(screenSettingsScreen, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(screenSettingsStatusLabel, 680, 34);
  lv_obj_set_pos(screenSettingsStatusLabel, 60, 414);

  updateScreenSettingsUi();
}

void openScreenSettingsScreen() {
  closePopupMenu();
  screenSettingsLastSavedTime = screenTimeEnabled;
  screenSettingsLastSavedCrypto = screenCryptoEnabled;
  screenSettingsLastSavedBtcDay = screenBtcDayEnabled;
  screenSettingsLastSavedKlipper = screenKlipperEnabled;
  createScreenSettingsScreen();
  screenSettingsActive = true;
  lv_screen_load(screenSettingsScreen);
  lastScreenSwitch = millis();
  lastUiRefresh = millis();
}
