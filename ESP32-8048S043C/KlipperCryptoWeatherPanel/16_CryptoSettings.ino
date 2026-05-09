static constexpr const char* CRYPTO_SETTINGS_FILE = "/crypto_settings.json";
static constexpr uint8_t CRYPTO_SETTINGS_INVALID_INDEX = 255;

struct CryptoTimeframeOption {
  const char* label;
};

static const char* const CRYPTO_BASE_OPTIONS[] = {
  "BTC",
  "ETH",
  "SOL",
  "XRP",
  "DOGE",
  "ADA",
};
static const char* const CRYPTO_QUOTE_OPTIONS[] = {
  "USD",
  "EUR",
  "GBP",
  "USDC",
  "USDT",
};
static const CryptoTimeframeOption CRYPTO_TIMEFRAME_OPTIONS[] = {
  { "15M" },
  { "1H" },
  { "6H" },
  { "1D" },
};
static constexpr uint8_t CRYPTO_BASE_OPTION_COUNT =
  sizeof(CRYPTO_BASE_OPTIONS) / sizeof(CRYPTO_BASE_OPTIONS[0]);
static constexpr uint8_t CRYPTO_QUOTE_OPTION_COUNT =
  sizeof(CRYPTO_QUOTE_OPTIONS) / sizeof(CRYPTO_QUOTE_OPTIONS[0]);
static constexpr uint8_t CRYPTO_TIMEFRAME_OPTION_COUNT =
  sizeof(CRYPTO_TIMEFRAME_OPTIONS) / sizeof(CRYPTO_TIMEFRAME_OPTIONS[0]);

static bool cryptoSettingsActive = false;
static lv_obj_t* cryptoSettingsScreen = nullptr;
static lv_obj_t* cryptoSettingsBaseButton[CRYPTO_BASE_OPTION_COUNT] = {};
static lv_obj_t* cryptoSettingsQuoteButton[CRYPTO_QUOTE_OPTION_COUNT] = {};
static lv_obj_t* cryptoSettingsTimeframeButton[CRYPTO_TIMEFRAME_OPTION_COUNT] = {};
static lv_obj_t* cryptoSettingsStatusLabel = nullptr;
static uint8_t cryptoSettingsBaseIndexData[CRYPTO_BASE_OPTION_COUNT] = {};
static uint8_t cryptoSettingsQuoteIndexData[CRYPTO_QUOTE_OPTION_COUNT] = {};
static uint8_t cryptoSettingsTimeframeIndexData[CRYPTO_TIMEFRAME_OPTION_COUNT] = {};
static uint8_t cryptoSettingsDraftBaseIndex = CRYPTO_SETTINGS_INVALID_INDEX;
static uint8_t cryptoSettingsDraftQuoteIndex = CRYPTO_SETTINGS_INVALID_INDEX;
static uint8_t cryptoSettingsDraftTimeframeIndex = CRYPTO_SETTINGS_INVALID_INDEX;

bool isCryptoSettingsScreenActive() {
  return cryptoSettingsActive;
}

static void copyCryptoSetting(char* target, size_t targetSize, const char* value) {
  if (target == nullptr || targetSize == 0) {
    return;
  }
  snprintf(target, targetSize, "%s", value != nullptr ? value : "");
}

static uint8_t cryptoOptionIndex(const char* value, const char* const* options, uint8_t count) {
  if (value == nullptr) {
    return CRYPTO_SETTINGS_INVALID_INDEX;
  }
  for (uint8_t i = 0; i < count; i++) {
    if (strcmp(value, options[i]) == 0) {
      return i;
    }
  }
  return CRYPTO_SETTINGS_INVALID_INDEX;
}

static uint8_t cryptoTimeframeOptionIndex(const char* value) {
  if (value == nullptr) {
    return CRYPTO_SETTINGS_INVALID_INDEX;
  }
  if (strcmp(value, "24H") == 0 || strcmp(value, "7D") == 0) {
    return 1;
  }
  if (strcmp(value, "30D") == 0) {
    return 2;
  }
  if (strcmp(value, "90D") == 0) {
    return 3;
  }
  for (uint8_t i = 0; i < CRYPTO_TIMEFRAME_OPTION_COUNT; i++) {
    if (strcmp(value, CRYPTO_TIMEFRAME_OPTIONS[i].label) == 0) {
      return i;
    }
  }
  return CRYPTO_SETTINGS_INVALID_INDEX;
}

static String normalizedCryptoOption(JsonVariantConst value) {
  String text = jsonStringValue(value);
  text.trim();
  text.toUpperCase();
  return text;
}

bool loadCryptoSettingsFromFile() {
  if (!settingsStorageReady || !LittleFS.exists(CRYPTO_SETTINGS_FILE)) {
    return false;
  }

  File file = LittleFS.open(CRYPTO_SETTINGS_FILE, "r");
  if (!file) {
    return false;
  }

  JsonDocument doc(&psramJsonAllocator);
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    Serial.print("Crypto Settings JSON Fehler: ");
    Serial.println(err.c_str());
    return false;
  }

  bool loaded = false;
  String base = normalizedCryptoOption(doc["base"]);
  String quote = normalizedCryptoOption(doc["quote"]);
  String timeframe = normalizedCryptoOption(doc["timeframe"]);

  if (cryptoOptionIndex(base.c_str(), CRYPTO_BASE_OPTIONS, CRYPTO_BASE_OPTION_COUNT) != CRYPTO_SETTINGS_INVALID_INDEX) {
    copyCryptoSetting(cryptoBaseSymbol, sizeof(cryptoBaseSymbol), base.c_str());
    loaded = true;
  }
  if (cryptoOptionIndex(quote.c_str(), CRYPTO_QUOTE_OPTIONS, CRYPTO_QUOTE_OPTION_COUNT) != CRYPTO_SETTINGS_INVALID_INDEX) {
    copyCryptoSetting(cryptoQuoteSymbol, sizeof(cryptoQuoteSymbol), quote.c_str());
    cryptoPricePrefix[0] = '\0';
    loaded = true;
  }
  if (cryptoTimeframeOptionIndex(timeframe.c_str()) != CRYPTO_SETTINGS_INVALID_INDEX) {
    uint8_t timeframeIndex = cryptoTimeframeOptionIndex(timeframe.c_str());
    copyCryptoSetting(cryptoChartTimeframe, sizeof(cryptoChartTimeframe), CRYPTO_TIMEFRAME_OPTIONS[timeframeIndex].label);
    loaded = true;
  }

  if (loaded) {
    resetCryptoDataState(cryptoOkStatus().c_str());
    Serial.printf(
      "Crypto Settings geladen: base=%s quote=%s timeframe=%s\n",
      cryptoBaseSymbol,
      cryptoQuoteSymbol,
      cryptoChartTimeframeLabel()
    );
  }
  return loaded;
}

bool saveCryptoSettingsToFile() {
  if (!settingsStorageReady) {
    return false;
  }

  JsonDocument doc(&psramJsonAllocator);
  doc["base"] = cryptoBaseSymbol;
  doc["quote"] = cryptoQuoteSymbol;
  doc["timeframe"] = cryptoChartTimeframeLabel();

  File file = LittleFS.open(CRYPTO_SETTINGS_FILE, "w");
  if (!file) {
    return false;
  }
  size_t written = serializeJson(doc, file);
  file.close();
  return written > 0;
}

static void destroyCryptoSettingsScreen() {
  if (cryptoSettingsScreen == nullptr) {
    return;
  }
  lv_obj_delete_async(cryptoSettingsScreen);
  cryptoSettingsScreen = nullptr;
  for (uint8_t i = 0; i < CRYPTO_BASE_OPTION_COUNT; i++) {
    cryptoSettingsBaseButton[i] = nullptr;
  }
  for (uint8_t i = 0; i < CRYPTO_QUOTE_OPTION_COUNT; i++) {
    cryptoSettingsQuoteButton[i] = nullptr;
  }
  for (uint8_t i = 0; i < CRYPTO_TIMEFRAME_OPTION_COUNT; i++) {
    cryptoSettingsTimeframeButton[i] = nullptr;
  }
  cryptoSettingsStatusLabel = nullptr;
}

static bool cryptoSettingsDraftComplete() {
  return cryptoSettingsDraftBaseIndex < CRYPTO_BASE_OPTION_COUNT &&
         cryptoSettingsDraftQuoteIndex < CRYPTO_QUOTE_OPTION_COUNT &&
         cryptoSettingsDraftTimeframeIndex < CRYPTO_TIMEFRAME_OPTION_COUNT;
}

static bool cryptoSettingsChanged() {
  if (!cryptoSettingsDraftComplete()) {
    return false;
  }
  return strcmp(cryptoBaseSymbol, CRYPTO_BASE_OPTIONS[cryptoSettingsDraftBaseIndex]) != 0 ||
         strcmp(cryptoQuoteSymbol, CRYPTO_QUOTE_OPTIONS[cryptoSettingsDraftQuoteIndex]) != 0 ||
         strcmp(cryptoChartTimeframeLabel(), CRYPTO_TIMEFRAME_OPTIONS[cryptoSettingsDraftTimeframeIndex].label) != 0;
}

static void updateCryptoSettingsButton(lv_obj_t* button, bool selected) {
  if (button == nullptr) {
    return;
  }
  lv_obj_set_style_bg_color(button, lv_color_hex(selected ? COLOR_BTC : 0x151b24), 0);
  lv_obj_set_style_border_color(button, lv_color_hex(selected ? COLOR_TEXT : COLOR_DIM), 0);
  lv_obj_set_style_text_color(button, lv_color_hex(selected ? COLOR_BG : COLOR_TEXT), 0);
}

static void updateCryptoSettingsUi() {
  for (uint8_t i = 0; i < CRYPTO_BASE_OPTION_COUNT; i++) {
    updateCryptoSettingsButton(cryptoSettingsBaseButton[i], i == cryptoSettingsDraftBaseIndex);
  }
  for (uint8_t i = 0; i < CRYPTO_QUOTE_OPTION_COUNT; i++) {
    updateCryptoSettingsButton(cryptoSettingsQuoteButton[i], i == cryptoSettingsDraftQuoteIndex);
  }
  for (uint8_t i = 0; i < CRYPTO_TIMEFRAME_OPTION_COUNT; i++) {
    updateCryptoSettingsButton(cryptoSettingsTimeframeButton[i], i == cryptoSettingsDraftTimeframeIndex);
  }

  if (cryptoSettingsStatusLabel == nullptr) {
    return;
  }
  if (!cryptoSettingsDraftComplete()) {
    lv_obj_set_style_text_color(cryptoSettingsStatusLabel, lv_color_hex(COLOR_ORANGE), 0);
    lv_label_set_text(cryptoSettingsStatusLabel, "Coin, Waehrung und Chart auswaehlen");
  } else if (cryptoSettingsChanged()) {
    lv_obj_set_style_text_color(cryptoSettingsStatusLabel, lv_color_hex(COLOR_BTC), 0);
    lv_label_set_text(cryptoSettingsStatusLabel, "Auswahl wird beim Zurueckgehen gespeichert");
  } else {
    String text = cryptoPairTitle() + "  Kerzen " + cryptoChartTimeframeLabel();
    lv_obj_set_style_text_color(cryptoSettingsStatusLabel, lv_color_hex(COLOR_MUTED), 0);
    lv_label_set_text(cryptoSettingsStatusLabel, text.c_str());
  }
}

static void cryptoBaseButtonEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  uint8_t* index = (uint8_t*)lv_event_get_user_data(event);
  if (index != nullptr && *index < CRYPTO_BASE_OPTION_COUNT) {
    cryptoSettingsDraftBaseIndex = *index;
    updateCryptoSettingsUi();
  }
}

static void cryptoQuoteButtonEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  uint8_t* index = (uint8_t*)lv_event_get_user_data(event);
  if (index != nullptr && *index < CRYPTO_QUOTE_OPTION_COUNT) {
    cryptoSettingsDraftQuoteIndex = *index;
    updateCryptoSettingsUi();
  }
}

static void cryptoTimeframeButtonEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  uint8_t* index = (uint8_t*)lv_event_get_user_data(event);
  if (index != nullptr && *index < CRYPTO_TIMEFRAME_OPTION_COUNT) {
    cryptoSettingsDraftTimeframeIndex = *index;
    updateCryptoSettingsUi();
  }
}

static void applyCryptoSettingsDraft() {
  bool networkMutexTaken = false;
  if (networkMutex != NULL) {
    networkMutexTaken = xSemaphoreTake(networkMutex, pdMS_TO_TICKS(8000)) == pdTRUE;
  }

  copyCryptoSetting(cryptoBaseSymbol, sizeof(cryptoBaseSymbol), CRYPTO_BASE_OPTIONS[cryptoSettingsDraftBaseIndex]);
  copyCryptoSetting(cryptoQuoteSymbol, sizeof(cryptoQuoteSymbol), CRYPTO_QUOTE_OPTIONS[cryptoSettingsDraftQuoteIndex]);
  copyCryptoSetting(cryptoChartTimeframe, sizeof(cryptoChartTimeframe), CRYPTO_TIMEFRAME_OPTIONS[cryptoSettingsDraftTimeframeIndex].label);
  cryptoPricePrefix[0] = '\0';
  resetCryptoDataState(cryptoOkStatus().c_str());
  cryptoRefreshRequested = true;

  if (networkMutexTaken) {
    xSemaphoreGive(networkMutex);
  }
}

static void closeCryptoSettingsScreen() {
  cryptoSettingsActive = false;

  if (cryptoSettingsChanged()) {
    applyCryptoSettingsDraft();
    if (saveCryptoSettingsToFile()) {
      Serial.printf(
        "Crypto Settings gespeichert: base=%s quote=%s timeframe=%s\n",
        cryptoBaseSymbol,
        cryptoQuoteSymbol,
        cryptoChartTimeframeLabel()
      );
    } else {
      Serial.println("Crypto Settings: Speichern fehlgeschlagen");
    }
  }

  destroyCryptoSettingsScreen();
  openSettingsMenuScreen();
}

static void cryptoSettingsBackEvent(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    closeCryptoSettingsScreen();
  }
}

static lv_obj_t* createCryptoSettingsBackButton(lv_obj_t* parent) {
  lv_obj_t* btn = lv_obj_create(parent);
  styleFilledRect(btn, 0x232b38, 8);
  lv_obj_set_size(btn, 52, 52);
  lv_obj_set_pos(btn, LCD_W - 52 - 24, 30);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(COLOR_DIM), 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(btn, cryptoSettingsBackEvent, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* label = createLabel(btn, &lv_font_montserrat_24, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(label, LV_SYMBOL_LEFT);
  lv_obj_center(label);
  lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
  return btn;
}

static void createCryptoSettingsSectionLabel(lv_obj_t* parent, const char* text, int y) {
  lv_obj_t* label = createLabel(parent, &lv_font_montserrat_28, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(label, 600, 34);
  lv_obj_set_pos(label, 100, y);
  lv_label_set_text(label, text);
}

static lv_obj_t* createCryptoSettingsOption(
  lv_obj_t* parent,
  int x,
  int y,
  int w,
  const char* text,
  lv_event_cb_t eventCb,
  uint8_t* indexData
) {
  lv_obj_t* btn = lv_obj_create(parent);
  styleFilledRect(btn, 0x151b24, 8);
  lv_obj_set_size(btn, w, 50);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(COLOR_DIM), 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(btn, eventCb, LV_EVENT_CLICKED, indexData);

  lv_obj_t* label = createLabel(btn, &lv_font_montserrat_24, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(label, w, 34);
  lv_obj_set_pos(label, 0, 9);
  lv_label_set_text(label, text);
  lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
  return btn;
}

static void createCryptoSettingsScreen() {
  destroyCryptoSettingsScreen();

  cryptoSettingsScreen = createScreen();
  createAccent(cryptoSettingsScreen, COLOR_SETTINGS);

  lv_obj_t* title = createLabel(cryptoSettingsScreen, &lv_font_montserrat_40, COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(title, 500, 52);
  lv_obj_set_pos(title, 100, 32);
  lv_label_set_text(title, "Crypto");

  createCryptoSettingsBackButton(cryptoSettingsScreen);

  createCryptoSettingsSectionLabel(cryptoSettingsScreen, "Coin", 112);
  for (uint8_t i = 0; i < CRYPTO_BASE_OPTION_COUNT; i++) {
    cryptoSettingsBaseIndexData[i] = i;
    cryptoSettingsBaseButton[i] = createCryptoSettingsOption(
      cryptoSettingsScreen,
      100 + (i * 98),
      152,
      90,
      CRYPTO_BASE_OPTIONS[i],
      cryptoBaseButtonEvent,
      &cryptoSettingsBaseIndexData[i]
    );
  }

  createCryptoSettingsSectionLabel(cryptoSettingsScreen, "Waehrung", 220);
  for (uint8_t i = 0; i < CRYPTO_QUOTE_OPTION_COUNT; i++) {
    cryptoSettingsQuoteIndexData[i] = i;
    cryptoSettingsQuoteButton[i] = createCryptoSettingsOption(
      cryptoSettingsScreen,
      90 + (i * 124),
      260,
      112,
      CRYPTO_QUOTE_OPTIONS[i],
      cryptoQuoteButtonEvent,
      &cryptoSettingsQuoteIndexData[i]
    );
  }

  createCryptoSettingsSectionLabel(cryptoSettingsScreen, "Kerzen", 328);
  for (uint8_t i = 0; i < CRYPTO_TIMEFRAME_OPTION_COUNT; i++) {
    cryptoSettingsTimeframeIndexData[i] = i;
    cryptoSettingsTimeframeButton[i] = createCryptoSettingsOption(
      cryptoSettingsScreen,
      150 + (i * 128),
      368,
      112,
      CRYPTO_TIMEFRAME_OPTIONS[i].label,
      cryptoTimeframeButtonEvent,
      &cryptoSettingsTimeframeIndexData[i]
    );
  }

  cryptoSettingsStatusLabel = createLabel(cryptoSettingsScreen, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(cryptoSettingsStatusLabel, 680, 34);
  lv_obj_set_pos(cryptoSettingsStatusLabel, 60, 438);

  updateCryptoSettingsUi();
}

void openCryptoSettingsScreen() {
  closePopupMenu();
  cryptoSettingsDraftBaseIndex = cryptoOptionIndex(cryptoBaseSymbol, CRYPTO_BASE_OPTIONS, CRYPTO_BASE_OPTION_COUNT);
  cryptoSettingsDraftQuoteIndex = cryptoOptionIndex(cryptoQuoteSymbol, CRYPTO_QUOTE_OPTIONS, CRYPTO_QUOTE_OPTION_COUNT);
  cryptoSettingsDraftTimeframeIndex = cryptoTimeframeOptionIndex(cryptoChartTimeframeLabel());
  createCryptoSettingsScreen();
  cryptoSettingsActive = true;
  lv_screen_load(cryptoSettingsScreen);
  lastScreenSwitch = millis();
  lastUiRefresh = millis();
}
