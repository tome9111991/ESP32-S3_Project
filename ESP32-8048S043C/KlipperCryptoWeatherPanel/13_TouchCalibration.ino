static constexpr const char* TOUCH_CAL_FILE = "/touch_cal.json";

struct TouchCalTarget {
  int x;
  int y;
};

static const TouchCalTarget kTouchCalTargets[] = {
  {60, 60},
  {LCD_W - 60, 60},
  {60, LCD_H - 60},
  {LCD_W - 60, LCD_H - 60},
  {LCD_W / 2, LCD_H / 2},
};
static constexpr int kTouchCalTargetCount =
  sizeof(kTouchCalTargets) / sizeof(kTouchCalTargets[0]);

static constexpr unsigned long TOUCH_CAL_ABORT_HOLD_MS = 2500;
static constexpr unsigned long TOUCH_CAL_RELEASE_MS = 120;
static constexpr int TOUCH_CAL_MIN_SAMPLES = 3;
static constexpr double TOUCH_CAL_MAX_RESIDUAL_PX = 80.0;

static bool touchCalibrationActive = false;
static lv_obj_t* touchCalScreen = nullptr;
static lv_obj_t* touchCalProgressLabel = nullptr;
static lv_obj_t* touchCalStatusLabel = nullptr;
static lv_obj_t* touchCalTargetMarker = nullptr;
static lv_obj_t* touchCalAbortBar = nullptr;
static lv_obj_t* touchCalAbortFill = nullptr;

static int touchCalStep = 0;
static int16_t touchCalRawAvgX[kTouchCalTargetCount];
static int16_t touchCalRawAvgY[kTouchCalTargetCount];
static bool touchCalPressed = false;
static unsigned long touchCalPressStart = 0;
static unsigned long touchCalLastSeen = 0;
static unsigned long touchCalLastPoll = 0;
static unsigned long touchCalAutoCloseAt = 0;
static int32_t touchCalRawSumX = 0;
static int32_t touchCalRawSumY = 0;
static int touchCalRawCount = 0;

bool isTouchCalibrationScreenActive() {
  return touchCalibrationActive;
}

static bool solve3x3(const double m[3][3], const double rhs[3], double out[3]) {
  double a = m[0][0], b = m[0][1], c = m[0][2];
  double d = m[1][0], e = m[1][1], f = m[1][2];
  double g = m[2][0], h = m[2][1], i = m[2][2];

  double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (fabs(det) < 1e-6) {
    return false;
  }

  out[0] = ((e * i - f * h) * rhs[0] + (c * h - b * i) * rhs[1] + (b * f - c * e) * rhs[2]) / det;
  out[1] = ((f * g - d * i) * rhs[0] + (a * i - c * g) * rhs[1] + (c * d - a * f) * rhs[2]) / det;
  out[2] = ((d * h - e * g) * rhs[0] + (b * g - a * h) * rhs[1] + (a * e - b * d) * rhs[2]) / det;
  return true;
}

static void destroyTouchCalibrationScreen() {
  if (touchCalScreen == nullptr) {
    return;
  }
  lv_obj_delete_async(touchCalScreen);
  touchCalScreen = nullptr;
  touchCalProgressLabel = nullptr;
  touchCalStatusLabel = nullptr;
  touchCalTargetMarker = nullptr;
  touchCalAbortBar = nullptr;
  touchCalAbortFill = nullptr;
}

static constexpr int TOUCH_CAL_ABORT_BAR_W = 480;
static constexpr int TOUCH_CAL_ABORT_BAR_H = 12;
// Erst ab dieser Halte-Dauer den Abbruch-Balken zeigen, damit normale
// kurze Taps zum Kalibrieren keinen visuellen Lärm erzeugen.
static constexpr unsigned long TOUCH_CAL_ABORT_BAR_DELAY_MS = 600;

static void touchCalUpdateAbortBar(unsigned long heldMs) {
  if (touchCalAbortBar == nullptr || touchCalAbortFill == nullptr) {
    return;
  }
  if (heldMs < TOUCH_CAL_ABORT_BAR_DELAY_MS) {
    lv_obj_add_flag(touchCalAbortBar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(touchCalAbortFill, 0);
    return;
  }
  if (heldMs > TOUCH_CAL_ABORT_HOLD_MS) {
    heldMs = TOUCH_CAL_ABORT_HOLD_MS;
  }
  lv_obj_remove_flag(touchCalAbortBar, LV_OBJ_FLAG_HIDDEN);
  unsigned long span = TOUCH_CAL_ABORT_HOLD_MS - TOUCH_CAL_ABORT_BAR_DELAY_MS;
  unsigned long progress = heldMs - TOUCH_CAL_ABORT_BAR_DELAY_MS;
  int innerWidth = TOUCH_CAL_ABORT_BAR_W - 4;
  int width = (int)((progress * (unsigned long)innerWidth) / span);
  lv_obj_set_width(touchCalAbortFill, width);
}

static void touchCalSetTargetVisible(int step) {
  if (touchCalTargetMarker == nullptr) {
    return;
  }
  if (step < 0 || step >= kTouchCalTargetCount) {
    lv_obj_add_flag(touchCalTargetMarker, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(touchCalTargetMarker, LV_OBJ_FLAG_HIDDEN);
  const int markerSize = 50;
  const TouchCalTarget& target = kTouchCalTargets[step];
  lv_obj_set_pos(touchCalTargetMarker, target.x - markerSize / 2, target.y - markerSize / 2);
}

static void touchCalUpdateProgress() {
  if (touchCalProgressLabel == nullptr) {
    return;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "Punkt %d / %d", touchCalStep + 1, kTouchCalTargetCount);
  lv_label_set_text(touchCalProgressLabel, buf);
}

static void touchCalSetStatus(const char* text, uint32_t color) {
  if (touchCalStatusLabel == nullptr) {
    return;
  }
  lv_obj_set_style_text_color(touchCalStatusLabel, lv_color_hex(color), 0);
  lv_label_set_text(touchCalStatusLabel, text);
}

static void createTouchCalibrationScreen() {
  destroyTouchCalibrationScreen();

  touchCalScreen = createScreen();

  lv_obj_t* title = createLabel(touchCalScreen, &lv_font_montserrat_40, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(title, LCD_W, 52);
  lv_obj_set_pos(title, 0, 140);
  lv_label_set_text(title, "Touch kalibrieren");

  touchCalProgressLabel = createLabel(touchCalScreen, &lv_font_montserrat_30, COLOR_CYAN, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(touchCalProgressLabel, LCD_W, 40);
  lv_obj_set_pos(touchCalProgressLabel, 0, 200);

  lv_obj_t* hint = createLabel(touchCalScreen, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(hint, LCD_W, 32);
  lv_obj_set_pos(hint, 0, 250);
  lv_label_set_text(hint, "Punkt antippen. Halten zum Abbrechen.");

  touchCalStatusLabel = createLabel(touchCalScreen, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(touchCalStatusLabel, LCD_W, 32);
  lv_obj_set_pos(touchCalStatusLabel, 0, 290);
  lv_label_set_text(touchCalStatusLabel, "");

  touchCalAbortBar = lv_obj_create(touchCalScreen);
  lv_obj_set_size(touchCalAbortBar, TOUCH_CAL_ABORT_BAR_W, TOUCH_CAL_ABORT_BAR_H);
  lv_obj_set_pos(touchCalAbortBar, (LCD_W - TOUCH_CAL_ABORT_BAR_W) / 2, 336);
  lv_obj_set_style_bg_color(touchCalAbortBar, lv_color_hex(0x232b38), 0);
  lv_obj_set_style_bg_opa(touchCalAbortBar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(touchCalAbortBar, lv_color_hex(COLOR_DIM), 0);
  lv_obj_set_style_border_width(touchCalAbortBar, 2, 0);
  lv_obj_set_style_radius(touchCalAbortBar, TOUCH_CAL_ABORT_BAR_H / 2, 0);
  lv_obj_set_style_pad_all(touchCalAbortBar, 0, 0);
  lv_obj_clear_flag(touchCalAbortBar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(touchCalAbortBar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(touchCalAbortBar, LV_OBJ_FLAG_HIDDEN);

  touchCalAbortFill = lv_obj_create(touchCalAbortBar);
  lv_obj_set_size(touchCalAbortFill, 0, TOUCH_CAL_ABORT_BAR_H - 4);
  lv_obj_set_pos(touchCalAbortFill, 0, 0);
  lv_obj_set_style_bg_color(touchCalAbortFill, lv_color_hex(COLOR_LOSS), 0);
  lv_obj_set_style_bg_opa(touchCalAbortFill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(touchCalAbortFill, 0, 0);
  lv_obj_set_style_radius(touchCalAbortFill, (TOUCH_CAL_ABORT_BAR_H - 4) / 2, 0);
  lv_obj_set_style_pad_all(touchCalAbortFill, 0, 0);
  lv_obj_clear_flag(touchCalAbortFill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(touchCalAbortFill, LV_OBJ_FLAG_SCROLLABLE);

  touchCalTargetMarker = lv_obj_create(touchCalScreen);
  lv_obj_set_size(touchCalTargetMarker, 50, 50);
  lv_obj_set_style_bg_color(touchCalTargetMarker, lv_color_hex(COLOR_CYAN), 0);
  lv_obj_set_style_bg_opa(touchCalTargetMarker, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(touchCalTargetMarker, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_border_width(touchCalTargetMarker, 3, 0);
  lv_obj_set_style_radius(touchCalTargetMarker, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(touchCalTargetMarker, 0, 0);
  lv_obj_clear_flag(touchCalTargetMarker, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(touchCalTargetMarker, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* dot = lv_obj_create(touchCalTargetMarker);
  lv_obj_set_size(dot, 10, 10);
  lv_obj_center(dot);
  lv_obj_set_style_bg_color(dot, lv_color_hex(COLOR_BG), 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_pad_all(dot, 0, 0);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
}

static void touchCalReset() {
  touchCalStep = 0;
  touchCalRawSumX = 0;
  touchCalRawSumY = 0;
  touchCalRawCount = 0;
  touchCalPressed = false;
  touchCalPressStart = 0;
  touchCalLastSeen = 0;
  touchCalLastPoll = 0;
  touchCalAutoCloseAt = 0;
  for (int i = 0; i < kTouchCalTargetCount; i++) {
    touchCalRawAvgX[i] = 0;
    touchCalRawAvgY[i] = 0;
  }
}

void openTouchCalibrationScreen() {
  closePopupMenu();
  createTouchCalibrationScreen();
  touchCalReset();
  touchCalibrationActive = true;
  touchCalUpdateProgress();
  touchCalSetTargetVisible(0);
  touchCalSetStatus("", COLOR_MUTED);
  lv_screen_load(touchCalScreen);
  lastScreenSwitch = millis();
  lastUiRefresh = millis();
}

static void touchCalReturnToSettings() {
  touchCalibrationActive = false;
  destroyTouchCalibrationScreen();
  openSettingsMenuScreen();
}

bool saveTouchCalibrationToFile() {
  if (!settingsStorageReady) {
    return false;
  }

  JsonDocument doc(&psramJsonAllocator);
  doc["v"] = 1;
  JsonArray xArr = doc["x"].to<JsonArray>();
  xArr.add(TOUCH_CAL_X_RX);
  xArr.add(TOUCH_CAL_X_RY);
  xArr.add(TOUCH_CAL_X_C);
  JsonArray yArr = doc["y"].to<JsonArray>();
  yArr.add(TOUCH_CAL_Y_RX);
  yArr.add(TOUCH_CAL_Y_RY);
  yArr.add(TOUCH_CAL_Y_C);

  File file = LittleFS.open(TOUCH_CAL_FILE, "w");
  if (!file) {
    return false;
  }
  size_t written = serializeJson(doc, file);
  file.close();
  return written > 0;
}

bool loadTouchCalibrationFromFile() {
  if (!settingsStorageReady || !LittleFS.exists(TOUCH_CAL_FILE)) {
    return false;
  }

  File file = LittleFS.open(TOUCH_CAL_FILE, "r");
  if (!file) {
    return false;
  }

  JsonDocument doc(&psramJsonAllocator);
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    Serial.print("Touch Cal JSON Fehler: ");
    Serial.println(err.c_str());
    return false;
  }

  JsonArrayConst xArr = doc["x"].as<JsonArrayConst>();
  JsonArrayConst yArr = doc["y"].as<JsonArrayConst>();
  if (xArr.size() != 3 || yArr.size() != 3) {
    Serial.println("Touch Cal: ungueltige Struktur");
    return false;
  }

  TOUCH_CAL_X_RX = xArr[0].as<float>();
  TOUCH_CAL_X_RY = xArr[1].as<float>();
  TOUCH_CAL_X_C = xArr[2].as<float>();
  TOUCH_CAL_Y_RX = yArr[0].as<float>();
  TOUCH_CAL_Y_RY = yArr[1].as<float>();
  TOUCH_CAL_Y_C = yArr[2].as<float>();
  Serial.printf(
    "Touch Cal geladen: X=(%.5f,%.5f,%.3f) Y=(%.5f,%.5f,%.3f)\n",
    TOUCH_CAL_X_RX,
    TOUCH_CAL_X_RY,
    TOUCH_CAL_X_C,
    TOUCH_CAL_Y_RX,
    TOUCH_CAL_Y_RY,
    TOUCH_CAL_Y_C
  );
  return true;
}

static void expectedFromTarget(int index, double& ex, double& ey) {
  // applyTouchCalibration() invertiert nach dem Affine-Mapping bei aktiver
  // Display-Rotation noch X und Y. Damit der Affine-Fit den richtigen
  // Endwert produziert, geben wir die vor-rotierten Koordinaten als Ziel an.
  if (displayRotate180) {
    ex = (LCD_W - 1) - kTouchCalTargets[index].x;
    ey = (LCD_H - 1) - kTouchCalTargets[index].y;
  } else {
    ex = kTouchCalTargets[index].x;
    ey = kTouchCalTargets[index].y;
  }
}

static bool fitTouchCalibration(
  float& xRx, float& xRy, float& xC,
  float& yRx, float& yRy, float& yC
) {
  const int n = kTouchCalTargetCount;
  double sxx = 0, sxy = 0, sx = 0, syy = 0, sy = 0;
  double xExpRx = 0, xExpRy = 0, xExp = 0;
  double yExpRx = 0, yExpRy = 0, yExp = 0;

  for (int i = 0; i < n; i++) {
    double rx = touchCalRawAvgX[i];
    double ry = touchCalRawAvgY[i];
    double ex, ey;
    expectedFromTarget(i, ex, ey);
    sxx += rx * rx;
    sxy += rx * ry;
    sx += rx;
    syy += ry * ry;
    sy += ry;
    xExpRx += ex * rx;
    xExpRy += ex * ry;
    xExp += ex;
    yExpRx += ey * rx;
    yExpRy += ey * ry;
    yExp += ey;
  }

  double m[3][3] = {
    {sxx, sxy, sx},
    {sxy, syy, sy},
    {sx, sy, (double)n},
  };
  double rhsX[3] = {xExpRx, xExpRy, xExp};
  double rhsY[3] = {yExpRx, yExpRy, yExp};
  double xResult[3];
  double yResult[3];
  if (!solve3x3(m, rhsX, xResult)) {
    return false;
  }
  if (!solve3x3(m, rhsY, yResult)) {
    return false;
  }

  double maxErr = 0.0;
  for (int i = 0; i < n; i++) {
    double rx = touchCalRawAvgX[i];
    double ry = touchCalRawAvgY[i];
    double ex, ey;
    expectedFromTarget(i, ex, ey);
    double mx = xResult[0] * rx + xResult[1] * ry + xResult[2];
    double my = yResult[0] * rx + yResult[1] * ry + yResult[2];
    double dx = mx - ex;
    double dy = my - ey;
    double err = sqrt(dx * dx + dy * dy);
    if (err > maxErr) {
      maxErr = err;
    }
  }
  if (maxErr > TOUCH_CAL_MAX_RESIDUAL_PX) {
    Serial.printf("Touch Cal Restfehler %.1f px > %.1f px\n", maxErr, TOUCH_CAL_MAX_RESIDUAL_PX);
    return false;
  }

  xRx = (float)xResult[0];
  xRy = (float)xResult[1];
  xC = (float)xResult[2];
  yRx = (float)yResult[0];
  yRy = (float)yResult[1];
  yC = (float)yResult[2];
  return true;
}

static void finishTouchCalibration() {
  float xRx, xRy, xC, yRx, yRy, yC;
  touchCalSetTargetVisible(-1);

  if (!fitTouchCalibration(xRx, xRy, xC, yRx, yRy, yC)) {
    touchCalSetStatus("Kalibrierung fehlgeschlagen, bitte erneut versuchen", COLOR_LOSS);
    touchCalAutoCloseAt = millis() + 2500;
    return;
  }

  TOUCH_CAL_X_RX = xRx;
  TOUCH_CAL_X_RY = xRy;
  TOUCH_CAL_X_C = xC;
  TOUCH_CAL_Y_RX = yRx;
  TOUCH_CAL_Y_RY = yRy;
  TOUCH_CAL_Y_C = yC;

  Serial.printf(
    "Neue Touch Cal: X=(%.5f,%.5f,%.3f) Y=(%.5f,%.5f,%.3f)\n",
    xRx, xRy, xC, yRx, yRy, yC
  );

  if (saveTouchCalibrationToFile()) {
    touchCalSetStatus("Kalibrierung gespeichert", COLOR_GREEN);
  } else {
    touchCalSetStatus("Speichern fehlgeschlagen", COLOR_LOSS);
  }
  touchCalAutoCloseAt = millis() + 1500;
}

void handleTouchCalibrationStep() {
  if (!touchCalibrationActive || touchAddress == 0) {
    return;
  }

  unsigned long now = millis();

  if (touchCalAutoCloseAt != 0) {
    if (now >= touchCalAutoCloseAt) {
      touchCalReturnToSettings();
    }
    return;
  }

  if (now - touchCalLastPoll < touchPollInterval) {
    return;
  }
  touchCalLastPoll = now;

  int16_t rawX = 0;
  int16_t rawY = 0;
  bool got = readTouchRaw(rawX, rawY);

  if (got) {
    if (!touchCalPressed) {
      touchCalPressed = true;
      touchCalPressStart = now;
      touchCalRawSumX = 0;
      touchCalRawSumY = 0;
      touchCalRawCount = 0;
      touchCalSetStatus("Halten...", COLOR_MUTED);
    }
    touchCalRawSumX += rawX;
    touchCalRawSumY += rawY;
    touchCalRawCount++;
    touchCalLastSeen = now;
    touchCalUpdateAbortBar(now - touchCalPressStart);

    if (now - touchCalPressStart >= TOUCH_CAL_ABORT_HOLD_MS) {
      touchCalSetTargetVisible(-1);
      touchCalUpdateAbortBar(0);
      touchCalSetStatus("Kalibrierung abgebrochen", COLOR_LOSS);
      touchCalAutoCloseAt = now + 1200;
      touchCalPressed = false;
    }
    return;
  }

  if (touchCalPressed && now - touchCalLastSeen >= TOUCH_CAL_RELEASE_MS) {
    touchCalPressed = false;
    touchCalUpdateAbortBar(0);
    if (touchCalRawCount >= TOUCH_CAL_MIN_SAMPLES) {
      touchCalRawAvgX[touchCalStep] = (int16_t)(touchCalRawSumX / touchCalRawCount);
      touchCalRawAvgY[touchCalStep] = (int16_t)(touchCalRawSumY / touchCalRawCount);
      touchCalStep++;
      if (touchCalStep >= kTouchCalTargetCount) {
        finishTouchCalibration();
      } else {
        touchCalUpdateProgress();
        touchCalSetTargetVisible(touchCalStep);
        touchCalSetStatus("", COLOR_MUTED);
      }
    } else {
      touchCalSetStatus("Zu kurz, bitte erneut tippen", COLOR_LOSS);
    }
  }
}
