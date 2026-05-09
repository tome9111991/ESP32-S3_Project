static constexpr const char* DISPLAY_SETTINGS_FILE = "/display_settings.json";

static bool displaySettingsActive = false;
static lv_obj_t* displaySettingsScreen = nullptr;
static lv_obj_t* displayBrightnessSlider = nullptr;
static lv_obj_t* displayRotateBox = nullptr;
static lv_obj_t* displayRotateCheckLabel = nullptr;
static lv_obj_t* displayRotateHintLabel = nullptr;
static uint8_t displaySettingsLastSavedBrightness = DAY_BRIGHTNESS_DEFAULT;
static bool displaySettingsLastSavedRotate180 = DISPLAY_ROTATE_180_DEFAULT;
static bool displaySettingsDraftRotate180 = DISPLAY_ROTATE_180_DEFAULT;

bool isDisplaySettingsScreenActive() {
  return displaySettingsActive;
}

bool loadDisplaySettingsFromFile() {
  if (!settingsStorageReady || !LittleFS.exists(DISPLAY_SETTINGS_FILE)) {
    return false;
  }

  File file = LittleFS.open(DISPLAY_SETTINGS_FILE, "r");
  if (!file) {
    return false;
  }

  JsonDocument doc(&psramJsonAllocator);
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    Serial.print("Display Settings JSON Fehler: ");
    Serial.println(err.c_str());
    return false;
  }

  bool loaded = false;
  int stored = doc["dayBrightness"] | -1;
  if (stored >= DAY_BRIGHTNESS_MIN && stored <= DAY_BRIGHTNESS_MAX) {
    dayBrightness = (uint8_t)stored;
    loaded = true;
  }
  if (doc["rotate180"].is<bool>()) {
    displayRotate180 = doc["rotate180"].as<bool>();
    loaded = true;
  }
  Serial.printf(
    "Display Settings geladen: dayBrightness=%u rotate180=%u\n",
    dayBrightness,
    displayRotate180 ? 1 : 0
  );
  return loaded;
}

bool saveDisplaySettingsToFile() {
  if (!settingsStorageReady) {
    return false;
  }

  JsonDocument doc(&psramJsonAllocator);
  doc["dayBrightness"] = dayBrightness;
  doc["rotate180"] = displayRotate180;

  File file = LittleFS.open(DISPLAY_SETTINGS_FILE, "w");
  if (!file) {
    return false;
  }
  size_t written = serializeJson(doc, file);
  file.close();
  return written > 0;
}

static void destroyDisplaySettingsScreen() {
  if (displaySettingsScreen == nullptr) {
    return;
  }
  lv_obj_delete_async(displaySettingsScreen);
  displaySettingsScreen = nullptr;
  displayBrightnessSlider = nullptr;
  displayRotateBox = nullptr;
  displayRotateCheckLabel = nullptr;
  displayRotateHintLabel = nullptr;
}

static void displayBrightnessSliderEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  int32_t v = lv_slider_get_value(displayBrightnessSlider);
  if (v < DAY_BRIGHTNESS_MIN) v = DAY_BRIGHTNESS_MIN;
  if (v > DAY_BRIGHTNESS_MAX) v = DAY_BRIGHTNESS_MAX;
  dayBrightness = (uint8_t)v;
  setDisplayBrightness(dayBrightness);
}

static void closeDisplaySettingsScreen() {
  displaySettingsActive = false;
  bool brightnessChanged = dayBrightness != displaySettingsLastSavedBrightness;
  bool rotationChanged = displaySettingsDraftRotate180 != displaySettingsLastSavedRotate180;

  if (brightnessChanged || rotationChanged) {
    bool previousRotate180 = displayRotate180;
    displayRotate180 = displaySettingsDraftRotate180;
    if (saveDisplaySettingsToFile()) {
      displaySettingsLastSavedBrightness = dayBrightness;
      displaySettingsLastSavedRotate180 = displayRotate180;
      Serial.printf(
        "Display Settings gespeichert: dayBrightness=%u rotate180=%u\n",
        dayBrightness,
        displayRotate180 ? 1 : 0
      );
    } else {
      Serial.println("Display Settings: Speichern fehlgeschlagen");
      displayRotate180 = previousRotate180;
      rotationChanged = false;
    }
  }

  if (rotationChanged) {
    destroyDisplaySettingsScreen();
    Serial.println("Display Rotation geaendert, Neustart");
    delay(250);
    ESP.restart();
    return;
  }

  destroyDisplaySettingsScreen();
  // Naechste Sun-Helligkeitsauswertung sofort, damit Nacht-Modus nach
  // Live-Preview wieder korrekt einsetzt.
  lastBrightnessRefresh = 0;
  openSettingsMenuScreen();
}

static void displaySettingsBackEvent(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    closeDisplaySettingsScreen();
  }
}

static void updateDisplayRotateUi() {
  if (displayRotateBox != nullptr) {
    lv_obj_set_style_bg_color(
      displayRotateBox,
      lv_color_hex(displaySettingsDraftRotate180 ? COLOR_CYAN : 0x232b38),
      0
    );
    lv_obj_set_style_border_color(
      displayRotateBox,
      lv_color_hex(displaySettingsDraftRotate180 ? COLOR_TEXT : COLOR_DIM),
      0
    );
  }
  if (displayRotateCheckLabel != nullptr) {
    lv_obj_set_style_text_color(
      displayRotateCheckLabel,
      lv_color_hex(displaySettingsDraftRotate180 ? COLOR_BG : COLOR_MUTED),
      0
    );
    lv_label_set_text(displayRotateCheckLabel, displaySettingsDraftRotate180 ? LV_SYMBOL_OK : "");
  }
  if (displayRotateHintLabel != nullptr) {
    const bool changed = displaySettingsDraftRotate180 != displaySettingsLastSavedRotate180;
    lv_obj_set_style_text_color(
      displayRotateHintLabel,
      lv_color_hex(changed ? COLOR_ORANGE : COLOR_MUTED),
      0
    );
    lv_label_set_text(
      displayRotateHintLabel,
      changed ? "Neustart beim Zurueckgehen" : "Touch-Kalibrierung bleibt erhalten"
    );
  }
}

static void displayRotateEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  displaySettingsDraftRotate180 = !displaySettingsDraftRotate180;
  updateDisplayRotateUi();
}

static lv_obj_t* createDisplaySettingsBackButton(lv_obj_t* parent) {
  lv_obj_t* btn = lv_obj_create(parent);
  styleFilledRect(btn, 0x232b38, 8);
  lv_obj_set_size(btn, 52, 52);
  lv_obj_set_pos(btn, LCD_W - 52 - 24, 30);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(COLOR_DIM), 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(btn, displaySettingsBackEvent, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* label = createLabel(btn, &lv_font_montserrat_24, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(label, LV_SYMBOL_LEFT);
  lv_obj_center(label);
  lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
  return btn;
}

static void createDisplayRotateOption(lv_obj_t* parent) {
  lv_obj_t* row = lv_obj_create(parent);
  styleFilledRect(row, 0x151b24, 8);
  lv_obj_set_size(row, 600, 96);
  lv_obj_set_pos(row, 100, 270);
  lv_obj_set_style_border_width(row, 2, 0);
  lv_obj_set_style_border_color(row, lv_color_hex(COLOR_DIM), 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(row, displayRotateEvent, LV_EVENT_CLICKED, nullptr);

  displayRotateBox = lv_obj_create(row);
  lv_obj_set_size(displayRotateBox, 48, 48);
  lv_obj_set_pos(displayRotateBox, 24, 24);
  lv_obj_set_style_radius(displayRotateBox, 6, 0);
  lv_obj_set_style_border_width(displayRotateBox, 2, 0);
  lv_obj_set_style_pad_all(displayRotateBox, 0, 0);
  lv_obj_clear_flag(displayRotateBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(displayRotateBox, LV_OBJ_FLAG_EVENT_BUBBLE);

  displayRotateCheckLabel = createLabel(displayRotateBox, &lv_font_montserrat_30, COLOR_BG, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(displayRotateCheckLabel, 48, 48);
  lv_obj_set_pos(displayRotateCheckLabel, 0, 4);
  lv_obj_add_flag(displayRotateCheckLabel, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t* titleLabel = createLabel(row, &lv_font_montserrat_30, COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(titleLabel, 460, 36);
  lv_obj_set_pos(titleLabel, 96, 14);
  lv_label_set_text(titleLabel, "Display 180 Grad drehen");
  lv_obj_add_flag(titleLabel, LV_OBJ_FLAG_EVENT_BUBBLE);

  displayRotateHintLabel = createLabel(row, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(displayRotateHintLabel, 460, 32);
  lv_obj_set_pos(displayRotateHintLabel, 96, 54);
  lv_obj_add_flag(displayRotateHintLabel, LV_OBJ_FLAG_EVENT_BUBBLE);

  updateDisplayRotateUi();
}

static void createDisplaySettingsScreen() {
  destroyDisplaySettingsScreen();

  displaySettingsScreen = createScreen();
  createAccent(displaySettingsScreen, COLOR_SETTINGS);

  lv_obj_t* title = createLabel(displaySettingsScreen, &lv_font_montserrat_40, COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(title, 500, 52);
  lv_obj_set_pos(title, 100, 32);
  lv_label_set_text(title, "Display");

  createDisplaySettingsBackButton(displaySettingsScreen);

  lv_obj_t* sectionLabel = createLabel(displaySettingsScreen, &lv_font_montserrat_30, COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
  lv_obj_set_size(sectionLabel, 600, 36);
  lv_obj_set_pos(sectionLabel, 100, 140);
  lv_label_set_text(sectionLabel, "Helligkeit (Tag)");

  displayBrightnessSlider = lv_slider_create(displaySettingsScreen);
  lv_obj_set_size(displayBrightnessSlider, 600, 24);
  lv_obj_set_pos(displayBrightnessSlider, 100, 200);
  lv_slider_set_range(displayBrightnessSlider, DAY_BRIGHTNESS_MIN, DAY_BRIGHTNESS_MAX);
  lv_slider_set_value(displayBrightnessSlider, dayBrightness, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(displayBrightnessSlider, lv_color_hex(0x232b38), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(displayBrightnessSlider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(displayBrightnessSlider, lv_color_hex(COLOR_CYAN), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(displayBrightnessSlider, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(displayBrightnessSlider, lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(displayBrightnessSlider, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_pad_all(displayBrightnessSlider, 8, LV_PART_KNOB);
  lv_obj_add_event_cb(displayBrightnessSlider, displayBrightnessSliderEvent, LV_EVENT_VALUE_CHANGED, nullptr);

  createDisplayRotateOption(displaySettingsScreen);
}

void openDisplaySettingsScreen() {
  closePopupMenu();
  displaySettingsLastSavedBrightness = dayBrightness;
  displaySettingsLastSavedRotate180 = displayRotate180;
  displaySettingsDraftRotate180 = displayRotate180;
  createDisplaySettingsScreen();
  displaySettingsActive = true;
  setDisplayBrightness(dayBrightness);
  lv_screen_load(displaySettingsScreen);
  lastScreenSwitch = millis();
  lastUiRefresh = millis();
}
