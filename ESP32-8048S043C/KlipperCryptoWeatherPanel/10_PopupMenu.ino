static constexpr int POPUP_MENU_W = 360;
static constexpr int POPUP_MENU_H = 330;
static constexpr int POPUP_MENU_X = (LCD_W - POPUP_MENU_W) / 2;
static constexpr int POPUP_MENU_Y = (LCD_H - POPUP_MENU_H) / 2;
static constexpr int POPUP_BUTTON_W = 240;
static constexpr int POPUP_BUTTON_H = 64;
static constexpr int POPUP_BUTTON_X = POPUP_MENU_X + (POPUP_MENU_W - POPUP_BUTTON_W) / 2;
static constexpr int POPUP_BUTTON_GAP = 16;
// Buttons vertikal in der Mitte des Panels gruppieren.
static constexpr int POPUP_BUTTONS_TOTAL_H = POPUP_BUTTON_H * 3 + POPUP_BUTTON_GAP * 2;
static constexpr int POPUP_BUTTONS_TOP_PAD = (POPUP_MENU_H - POPUP_BUTTONS_TOTAL_H) / 2;
static constexpr int POPUP_SETTINGS_X = POPUP_BUTTON_X;
static constexpr int POPUP_SETTINGS_Y = POPUP_MENU_Y + POPUP_BUTTONS_TOP_PAD;
static constexpr int POPUP_SETTINGS_W = POPUP_BUTTON_W;
static constexpr int POPUP_SETTINGS_H = POPUP_BUTTON_H;
static constexpr int POPUP_REBOOT_X = POPUP_BUTTON_X;
static constexpr int POPUP_REBOOT_Y = POPUP_SETTINGS_Y + POPUP_BUTTON_H + POPUP_BUTTON_GAP;
static constexpr int POPUP_REBOOT_W = POPUP_BUTTON_W;
static constexpr int POPUP_REBOOT_H = POPUP_BUTTON_H;
static constexpr int POPUP_RESET_X = POPUP_BUTTON_X;
static constexpr int POPUP_RESET_Y = POPUP_REBOOT_Y + POPUP_BUTTON_H + POPUP_BUTTON_GAP;
static constexpr int POPUP_RESET_W = POPUP_BUTTON_W;
static constexpr int POPUP_RESET_H = POPUP_BUTTON_H;

// Bestaetigungsdialog fuer Factory Reset
static constexpr int CONFIRM_PANEL_W = 600;
static constexpr int CONFIRM_PANEL_H = 280;
static constexpr int CONFIRM_PANEL_X = (LCD_W - CONFIRM_PANEL_W) / 2;
static constexpr int CONFIRM_PANEL_Y = (LCD_H - CONFIRM_PANEL_H) / 2;
static constexpr int CONFIRM_BTN_W = 240;
static constexpr int CONFIRM_BTN_H = 64;
static constexpr int CONFIRM_BTN_Y = CONFIRM_PANEL_Y + CONFIRM_PANEL_H - CONFIRM_BTN_H - 28;
static constexpr int CONFIRM_CANCEL_X = CONFIRM_PANEL_X + 32;
static constexpr int CONFIRM_OK_X = CONFIRM_PANEL_X + CONFIRM_PANEL_W - CONFIRM_BTN_W - 32;

static lv_obj_t* popupMenuBackdrop = nullptr;
static lv_obj_t* popupMenuPanel = nullptr;
static lv_obj_t* popupSettingsButton = nullptr;
static lv_obj_t* popupSettingsIcon = nullptr;
static lv_obj_t* popupRebootButton = nullptr;
static lv_obj_t* popupResetButton = nullptr;
static lv_obj_t* popupConfirmBackdrop = nullptr;
static lv_obj_t* popupConfirmPanel = nullptr;

static bool pointInRect(int16_t x, int16_t y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh);
}

bool isPopupMenuOpen() {
  return popupMenuBackdrop != nullptr;
}

static bool isFactoryResetConfirmOpen() {
  return popupConfirmBackdrop != nullptr;
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
  openSettingsMenuScreen();
}

static void destroyFactoryResetConfirm() {
  if (popupConfirmBackdrop == nullptr) {
    return;
  }
  lv_obj_delete(popupConfirmBackdrop);
  popupConfirmBackdrop = nullptr;
  popupConfirmPanel = nullptr;
}

static void openFactoryResetConfirm() {
  if (isFactoryResetConfirmOpen()) {
    return;
  }

  lv_obj_t* parent = lv_screen_active();
  popupConfirmBackdrop = lv_obj_create(parent);
  lv_obj_remove_style_all(popupConfirmBackdrop);
  lv_obj_set_size(popupConfirmBackdrop, LCD_W, LCD_H);
  lv_obj_set_pos(popupConfirmBackdrop, 0, 0);
  lv_obj_set_style_bg_color(popupConfirmBackdrop, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(popupConfirmBackdrop, 200, 0);
  lv_obj_clear_flag(popupConfirmBackdrop, LV_OBJ_FLAG_SCROLLABLE);

  popupConfirmPanel = lv_obj_create(popupConfirmBackdrop);
  styleFilledRect(popupConfirmPanel, 0x171d26, 10);
  lv_obj_set_size(popupConfirmPanel, CONFIRM_PANEL_W, CONFIRM_PANEL_H);
  lv_obj_set_pos(popupConfirmPanel, CONFIRM_PANEL_X, CONFIRM_PANEL_Y);
  lv_obj_set_style_border_width(popupConfirmPanel, 2, 0);
  lv_obj_set_style_border_color(popupConfirmPanel, lv_color_hex(COLOR_LOSS), 0);
  lv_obj_clear_flag(popupConfirmPanel, LV_OBJ_FLAG_SCROLLABLE);

  createPopupMenuLabel(
    popupConfirmPanel,
    &lv_font_montserrat_30,
    COLOR_LOSS,
    LV_TEXT_ALIGN_CENTER,
    20,
    24,
    CONFIRM_PANEL_W - 40,
    40,
    "Factory Reset"
  );

  createPopupMenuLabel(
    popupConfirmPanel,
    &lv_font_montserrat_24,
    COLOR_TEXT,
    LV_TEXT_ALIGN_CENTER,
    30,
    78,
    CONFIRM_PANEL_W - 60,
    32,
    "Alle Einstellungen werden geloescht"
  );

  createPopupMenuLabel(
    popupConfirmPanel,
    &lv_font_montserrat_24,
    COLOR_MUTED,
    LV_TEXT_ALIGN_CENTER,
    30,
    116,
    CONFIRM_PANEL_W - 60,
    32,
    "und das Geraet startet neu."
  );

  lv_obj_t* cancelBtn = lv_obj_create(popupConfirmPanel);
  styleFilledRect(cancelBtn, 0x232b38, 8);
  lv_obj_set_size(cancelBtn, CONFIRM_BTN_W, CONFIRM_BTN_H);
  lv_obj_set_pos(cancelBtn, CONFIRM_CANCEL_X - CONFIRM_PANEL_X, CONFIRM_BTN_Y - CONFIRM_PANEL_Y);
  lv_obj_set_style_border_width(cancelBtn, 2, 0);
  lv_obj_set_style_border_color(cancelBtn, lv_color_hex(COLOR_DIM), 0);
  lv_obj_clear_flag(cancelBtn, LV_OBJ_FLAG_SCROLLABLE);

  createPopupMenuLabel(
    cancelBtn,
    &lv_font_montserrat_30,
    COLOR_TEXT,
    LV_TEXT_ALIGN_CENTER,
    0,
    14,
    CONFIRM_BTN_W,
    CONFIRM_BTN_H - 14,
    "Abbrechen"
  );

  lv_obj_t* okBtn = lv_obj_create(popupConfirmPanel);
  styleFilledRect(okBtn, COLOR_LOSS, 8);
  lv_obj_set_size(okBtn, CONFIRM_BTN_W, CONFIRM_BTN_H);
  lv_obj_set_pos(okBtn, CONFIRM_OK_X - CONFIRM_PANEL_X, CONFIRM_BTN_Y - CONFIRM_PANEL_Y);
  lv_obj_clear_flag(okBtn, LV_OBJ_FLAG_SCROLLABLE);

  createPopupMenuLabel(
    okBtn,
    &lv_font_montserrat_30,
    COLOR_BG,
    LV_TEXT_ALIGN_CENTER,
    0,
    14,
    CONFIRM_BTN_W,
    CONFIRM_BTN_H - 14,
    "Zuruecksetzen"
  );

  lastScreenSwitch = millis();
  lastUiRefresh = millis();
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

  popupRebootButton = lv_obj_create(popupMenuPanel);
  styleFilledRect(popupRebootButton, COLOR_ORANGE, 8);
  lv_obj_set_size(popupRebootButton, POPUP_REBOOT_W, POPUP_REBOOT_H);
  lv_obj_set_pos(popupRebootButton, POPUP_REBOOT_X - POPUP_MENU_X, POPUP_REBOOT_Y - POPUP_MENU_Y);

  createPopupMenuLabel(
    popupRebootButton,
    &lv_font_montserrat_30,
    COLOR_BG,
    LV_TEXT_ALIGN_CENTER,
    0,
    14,
    POPUP_REBOOT_W,
    POPUP_REBOOT_H - 14,
    "Reboot"
  );

  popupResetButton = lv_obj_create(popupMenuPanel);
  styleFilledRect(popupResetButton, COLOR_LOSS, 8);
  lv_obj_set_size(popupResetButton, POPUP_RESET_W, POPUP_RESET_H);
  lv_obj_set_pos(popupResetButton, POPUP_RESET_X - POPUP_MENU_X, POPUP_RESET_Y - POPUP_MENU_Y);

  createPopupMenuLabel(
    popupResetButton,
    &lv_font_montserrat_30,
    COLOR_BG,
    LV_TEXT_ALIGN_CENTER,
    0,
    14,
    POPUP_RESET_W,
    POPUP_RESET_H - 14,
    "Factory Reset"
  );

  lastScreenSwitch = millis();
  lastUiRefresh = millis();
}

void closePopupMenu() {
  if (popupMenuBackdrop == nullptr) {
    return;
  }

  destroyFactoryResetConfirm();
  lv_obj_delete(popupMenuBackdrop);
  popupMenuBackdrop = nullptr;
  popupMenuPanel = nullptr;
  popupSettingsButton = nullptr;
  popupSettingsIcon = nullptr;
  popupRebootButton = nullptr;
  popupResetButton = nullptr;
  lastScreenSwitch = millis();
  lastUiRefresh = millis();
}

void handlePopupMenuTouch(int16_t x, int16_t y) {
  if (!isPopupMenuOpen()) {
    return;
  }

  if (isFactoryResetConfirmOpen()) {
    if (pointInRect(x, y, CONFIRM_OK_X, CONFIRM_BTN_Y, CONFIRM_BTN_W, CONFIRM_BTN_H)) {
      performFactoryReset();
      return;
    }
    if (pointInRect(x, y, CONFIRM_CANCEL_X, CONFIRM_BTN_Y, CONFIRM_BTN_W, CONFIRM_BTN_H)) {
      destroyFactoryResetConfirm();
      return;
    }
    return;
  }

  if (pointInRect(x, y, POPUP_SETTINGS_X, POPUP_SETTINGS_Y, POPUP_SETTINGS_W, POPUP_SETTINGS_H)) {
    openSettingsScreenFromMenu();
    return;
  }

  if (pointInRect(x, y, POPUP_REBOOT_X, POPUP_REBOOT_Y, POPUP_REBOOT_W, POPUP_REBOOT_H)) {
    performCleanReboot();
    return;
  }

  if (pointInRect(x, y, POPUP_RESET_X, POPUP_RESET_Y, POPUP_RESET_W, POPUP_RESET_H)) {
    openFactoryResetConfirm();
    return;
  }

  if (!pointInRect(x, y, POPUP_MENU_X, POPUP_MENU_Y, POPUP_MENU_W, POPUP_MENU_H)) {
    closePopupMenu();
  }
}
