static lv_obj_t* settingsMenuScreen = nullptr;

static constexpr int SETTINGS_MENU_LIST_Y = 112;
static constexpr int SETTINGS_MENU_LIST_H = LCD_H - SETTINGS_MENU_LIST_Y;
static constexpr int SETTINGS_MENU_ITEM_X = 100;
static constexpr int SETTINGS_MENU_ITEM_W = 600;
static constexpr int SETTINGS_MENU_ITEM_H = 64;
static constexpr int SETTINGS_MENU_ITEM_GAP = 8;
static constexpr int SETTINGS_MENU_ITEM_START_Y = 8;

bool isSettingsMenuScreenActive() {
  return settingsMenuActive;
}

bool isSettingsOverlayActive() {
  return isWifiSetupScreenActive() || isSettingsMenuScreenActive() ||
         isTouchCalibrationScreenActive() || isDisplaySettingsScreenActive() ||
         isScreenSettingsScreenActive() || isCryptoSettingsScreenActive();
}

static void destroySettingsMenuScreen() {
  if (settingsMenuScreen == nullptr) {
    return;
  }
  lv_obj_delete_async(settingsMenuScreen);
  settingsMenuScreen = nullptr;
}

static void closeSettingsMenuScreen() {
  settingsMenuActive = false;
  destroySettingsMenuScreen();
  if (!isScreenAvailableForRotation(currentScreen)) {
    currentScreen = nextScreenState(currentScreen);
  }
  refreshUi();
  lv_screen_load(screenForState(currentScreen));
  lastScreenSwitch = millis();
  lastUiRefresh = millis();
}

static void settingsWifiButtonEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  settingsMenuActive = false;
  destroySettingsMenuScreen();
  openWifiSetupScreen("WLAN-Daten bearbeiten");
}

static void settingsTouchCalButtonEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  settingsMenuActive = false;
  destroySettingsMenuScreen();
  openTouchCalibrationScreen();
}

static void settingsDisplayButtonEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  settingsMenuActive = false;
  destroySettingsMenuScreen();
  openDisplaySettingsScreen();
}

static void settingsScreensButtonEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  settingsMenuActive = false;
  destroySettingsMenuScreen();
  openScreenSettingsScreen();
}

static void settingsCryptoButtonEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  settingsMenuActive = false;
  destroySettingsMenuScreen();
  openCryptoSettingsScreen();
}

static void settingsBackButtonEvent(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    closeSettingsMenuScreen();
  }
}

static lv_obj_t* createSettingsMenuItem(
  lv_obj_t* parent,
  int y,
  uint32_t color,
  uint32_t labelColor,
  const char* title,
  const char* subtitle,
  lv_event_cb_t cb
) {
  lv_obj_t* btn = lv_obj_create(parent);
  styleFilledRect(btn, color, 10);
  lv_obj_set_size(btn, SETTINGS_MENU_ITEM_W, SETTINGS_MENU_ITEM_H);
  lv_obj_set_pos(btn, SETTINGS_MENU_ITEM_X, y);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* titleLabel = createLabel(btn, &lv_font_montserrat_28, labelColor, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(titleLabel, 560, 32);
  lv_obj_set_pos(titleLabel, 30, 5);
  lv_label_set_text(titleLabel, title);
  lv_obj_add_flag(titleLabel, LV_OBJ_FLAG_EVENT_BUBBLE);

  if (subtitle != nullptr && subtitle[0] != '\0') {
    lv_obj_t* subtitleLabel = createLabel(btn, &lv_font_montserrat_24, labelColor, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(subtitleLabel, 560, 28);
    lv_obj_set_pos(subtitleLabel, 30, 34);
    lv_label_set_text(subtitleLabel, subtitle);
    lv_obj_set_style_text_opa(subtitleLabel, LV_OPA_70, 0);
    lv_obj_add_flag(subtitleLabel, LV_OBJ_FLAG_EVENT_BUBBLE);
  }

  return btn;
}

static lv_obj_t* createSettingsMenuList(lv_obj_t* parent) {
  lv_obj_t* list = lv_obj_create(parent);
  lv_obj_remove_style_all(list);
  lv_obj_set_size(list, LCD_W, SETTINGS_MENU_LIST_H);
  lv_obj_set_pos(list, 0, SETTINGS_MENU_LIST_Y);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  return list;
}

static lv_obj_t* createSettingsBackButton(lv_obj_t* parent) {
  lv_obj_t* btn = lv_obj_create(parent);
  styleFilledRect(btn, 0x232b38, 8);
  lv_obj_set_size(btn, 52, 52);
  lv_obj_set_pos(btn, LCD_W - 52 - 24, 30);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(COLOR_DIM), 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(btn, settingsBackButtonEvent, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* label = createLabel(btn, &lv_font_montserrat_24, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(label, LV_SYMBOL_LEFT);
  lv_obj_center(label);
  lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
  return btn;
}

static void createSettingsMenuScreen() {
  destroySettingsMenuScreen();

  settingsMenuScreen = createScreen();
  createAccent(settingsMenuScreen, COLOR_SETTINGS);

  lv_obj_t* title = createLabel(settingsMenuScreen, &lv_font_montserrat_40, COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(title, 500, 52);
  lv_obj_set_pos(title, 100, 32);
  lv_label_set_text(title, "Einstellungen");

  createSettingsBackButton(settingsMenuScreen);

  lv_obj_t* list = createSettingsMenuList(settingsMenuScreen);
  int itemY = SETTINGS_MENU_ITEM_START_Y;

  createSettingsMenuItem(
    list,
    itemY,
    COLOR_CYAN,
    COLOR_BG,
    "WLAN",
    "SSID und Passwort aendern",
    settingsWifiButtonEvent
  );
  itemY += SETTINGS_MENU_ITEM_H + SETTINGS_MENU_ITEM_GAP;

  createSettingsMenuItem(
    list,
    itemY,
    COLOR_CYAN,
    COLOR_BG,
    "Screens",
    "Angezeigte Seiten auswaehlen",
    settingsScreensButtonEvent
  );
  itemY += SETTINGS_MENU_ITEM_H + SETTINGS_MENU_ITEM_GAP;

  createSettingsMenuItem(
    list,
    itemY,
    COLOR_CYAN,
    COLOR_BG,
    "Crypto",
    "Coin, Waehrung und Chart",
    settingsCryptoButtonEvent
  );
  itemY += SETTINGS_MENU_ITEM_H + SETTINGS_MENU_ITEM_GAP;

  createSettingsMenuItem(
    list,
    itemY,
    COLOR_CYAN,
    COLOR_BG,
    "Display",
    "Anzeige-Einstellungen",
    settingsDisplayButtonEvent
  );
  itemY += SETTINGS_MENU_ITEM_H + SETTINGS_MENU_ITEM_GAP;

  createSettingsMenuItem(
    list,
    itemY,
    COLOR_CYAN,
    COLOR_BG,
    "Touch kalibrieren",
    "Beruehrung auf 5 Punkte abgleichen",
    settingsTouchCalButtonEvent
  );
}

void openSettingsMenuScreen() {
  closePopupMenu();
  createSettingsMenuScreen();
  settingsMenuActive = true;
  lv_screen_load(settingsMenuScreen);
  lastScreenSwitch = millis();
  lastUiRefresh = millis();
}
