static constexpr int POPUP_MENU_X = 220;
static constexpr int POPUP_MENU_Y = 130;
static constexpr int POPUP_MENU_W = 360;
static constexpr int POPUP_MENU_H = 220;
static constexpr int POPUP_SETTINGS_X = POPUP_MENU_X + 60;
static constexpr int POPUP_SETTINGS_Y = POPUP_MENU_Y + 78;
static constexpr int POPUP_SETTINGS_W = 240;
static constexpr int POPUP_SETTINGS_H = 64;

static lv_obj_t* popupMenuBackdrop = nullptr;
static lv_obj_t* popupMenuPanel = nullptr;
static lv_obj_t* popupSettingsButton = nullptr;
static lv_obj_t* popupSettingsIcon = nullptr;

static bool pointInRect(int16_t x, int16_t y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh);
}

bool isPopupMenuOpen() {
  return popupMenuBackdrop != nullptr;
}

static lv_obj_t* createPopupMenuLabel(
  lv_obj_t* parent,
  const lv_font_t* font,
  uint32_t color,
  lv_text_align_t align,
  int x,
  int y,
  int w,
  int h,
  const char* text
) {
  lv_obj_t* label = createLabel(parent, font, color, align);
  lv_obj_set_size(label, w, h);
  lv_obj_set_pos(label, x, y);
  lv_label_set_text(label, text);
  return label;
}

static void openSettingsScreenFromMenu() {
  Serial.println("Popup: Settings-Screen noch nicht implementiert");
  closePopupMenu();
}

void openPopupMenu() {
  if (isPopupMenuOpen()) {
    return;
  }

  lv_obj_t* parent = lv_screen_active();
  popupMenuBackdrop = lv_obj_create(parent);
  lv_obj_remove_style_all(popupMenuBackdrop);
  lv_obj_set_size(popupMenuBackdrop, LCD_W, LCD_H);
  lv_obj_set_pos(popupMenuBackdrop, 0, 0);
  lv_obj_set_style_bg_color(popupMenuBackdrop, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(popupMenuBackdrop, 150, 0);
  lv_obj_clear_flag(popupMenuBackdrop, LV_OBJ_FLAG_SCROLLABLE);

  popupMenuPanel = lv_obj_create(popupMenuBackdrop);
  styleFilledRect(popupMenuPanel, 0x171d26, 8);
  lv_obj_set_size(popupMenuPanel, POPUP_MENU_W, POPUP_MENU_H);
  lv_obj_set_pos(popupMenuPanel, POPUP_MENU_X, POPUP_MENU_Y);
  lv_obj_set_style_border_width(popupMenuPanel, 2, 0);
  lv_obj_set_style_border_color(popupMenuPanel, lv_color_hex(COLOR_DIM), 0);

  popupSettingsButton = lv_obj_create(popupMenuPanel);
  styleFilledRect(popupSettingsButton, COLOR_CYAN, 8);
  lv_obj_set_size(popupSettingsButton, POPUP_SETTINGS_W, POPUP_SETTINGS_H);
  lv_obj_set_pos(popupSettingsButton, POPUP_SETTINGS_X - POPUP_MENU_X, POPUP_SETTINGS_Y - POPUP_MENU_Y);

  popupSettingsIcon = lv_image_create(popupSettingsButton);
  lv_image_set_src(popupSettingsIcon, &icon_settings);
  lv_obj_set_size(popupSettingsIcon, 48, 48);
  lv_obj_set_pos(popupSettingsIcon, 30, 8);
  lv_obj_clear_flag(popupSettingsIcon, LV_OBJ_FLAG_SCROLLABLE);

  createPopupMenuLabel(
    popupSettingsButton,
    &lv_font_montserrat_30,
    COLOR_BG,
    LV_TEXT_ALIGN_CENTER,
    86,
    12,
    130,
    POPUP_SETTINGS_H - 12,
    "Settings"
  );

  lastScreenSwitch = millis();
  lastUiRefresh = millis();
}

void closePopupMenu() {
  if (popupMenuBackdrop == nullptr) {
    return;
  }

  lv_obj_delete(popupMenuBackdrop);
  popupMenuBackdrop = nullptr;
  popupMenuPanel = nullptr;
  popupSettingsButton = nullptr;
  popupSettingsIcon = nullptr;
  lastScreenSwitch = millis();
  lastUiRefresh = millis();
}

void handlePopupMenuTouch(int16_t x, int16_t y) {
  if (!isPopupMenuOpen()) {
    return;
  }

  if (pointInRect(x, y, POPUP_SETTINGS_X, POPUP_SETTINGS_Y, POPUP_SETTINGS_W, POPUP_SETTINGS_H)) {
    openSettingsScreenFromMenu();
    return;
  }

  if (!pointInRect(x, y, POPUP_MENU_X, POPUP_MENU_Y, POPUP_MENU_W, POPUP_MENU_H)) {
    closePopupMenu();
  }
}
