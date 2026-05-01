lv_obj_t* createScreen() {
  lv_obj_t* screen = lv_obj_create(NULL);
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  return screen;
}

lv_obj_t* createLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t color, lv_text_align_t align) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_remove_style_all(label);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(label, align, 0);
  lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  return label;
}

lv_obj_t* createAccent(lv_obj_t* parent, uint32_t color) {
  lv_obj_t* accent = lv_obj_create(parent);
  lv_obj_remove_style_all(accent);
  lv_obj_set_size(accent, 28, 4);
  lv_obj_set_style_bg_color(accent, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(accent, 2, 0);
  lv_obj_set_pos(accent, 22, 31);
  return accent;
}

lv_obj_t* createDivider(lv_obj_t* parent, int x, int y, int w, uint32_t color) {
  lv_obj_t* divider = lv_obj_create(parent);
  lv_obj_remove_style_all(divider);
  lv_obj_set_size(divider, w, DIVIDER_H);
  lv_obj_set_style_bg_color(divider, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(divider, 2, 0);
  lv_obj_set_pos(divider, x, y);
  lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
  return divider;
}

void updateTimeSecondProgress(int second) {
  if (timeSecondFill == nullptr) {
    return;
  }

  if (second < 0) {
    lastTimeSecondProgress = -1;
    setHidden(timeSecondFill, true);
    return;
  }

  if (second > 59) {
    second = 59;
  }
  if (second == lastTimeSecondProgress) {
    return;
  }

  lastTimeSecondProgress = second;
  const int fillWidth = (second * TIME_SECOND_BAR_W) / 59;
  setHidden(timeSecondFill, fillWidth <= 0);
  if (fillWidth > 0) {
    lv_obj_set_width(timeSecondFill, fillWidth);
  }
}

void setHidden(lv_obj_t* obj, bool hidden) {
  if (obj == nullptr) {
    return;
  }

  if (hidden) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

bool setLabelTextIfChanged(lv_obj_t* label, const char* text) {
  if (label == nullptr || text == nullptr) {
    return false;
  }

  const char* currentText = lv_label_get_text(label);
  if (currentText != nullptr && strcmp(currentText, text) == 0) {
    return false;
  }

  lv_label_set_text(label, text);
  return true;
}

void setTimeNormalVisible(bool visible) {
  setHidden(timeAccent, !visible);
  setHidden(timeLocationLabel, !visible);
  setHidden(timeLabel, !visible);
  setHidden(weekdayLabel, !visible);
  setHidden(dateLabel, !visible);
  setHidden(tempLabel, !visible);
  setHidden(weatherIconRoot, !visible);
  setHidden(timeDivider, !visible);
  setHidden(timeStatusTitle, visible);
  setHidden(timeStatusDetail, visible);
}

void styleFilledRect(lv_obj_t* obj, uint32_t color, int radius) {
  lv_obj_remove_style_all(obj);
  lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* createSunRay(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* ray = lv_obj_create(parent);
  styleFilledRect(ray, COLOR_SUN, 2);
  lv_obj_set_size(ray, w, h);
  lv_obj_set_pos(ray, x, y);
  return ray;
}

WeatherVisual weatherVisualFromCode(int code) {
  if (code == 0) {
    return WEATHER_VISUAL_CLEAR;
  }
  if (code >= 1 && code <= 2) {
    return WEATHER_VISUAL_PARTLY;
  }
  if (code == 3) {
    return WEATHER_VISUAL_CLOUD;
  }
  if (code == 45 || code == 48) {
    return WEATHER_VISUAL_FOG;
  }
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    return WEATHER_VISUAL_RAIN;
  }
  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
    return WEATHER_VISUAL_SNOW;
  }
  if (code >= 95 && code <= 99) {
    return WEATHER_VISUAL_THUNDER;
  }
  return WEATHER_VISUAL_UNKNOWN;
}

void weatherCanvasSetPixel(int x, int y, uint32_t color) {
  if (weatherCanvas == nullptr || x < 0 || y < 0 || x >= WEATHER_ICON_W || y >= WEATHER_ICON_H) {
    return;
  }
  lv_canvas_set_px(weatherCanvas, x, y, lv_color_hex(color), LV_OPA_COVER);
}

void weatherCanvasFillRect(int x, int y, int w, int h, uint32_t color) {
  if (w <= 0 || h <= 0) {
    return;
  }

  int x2 = x + w - 1;
  int y2 = y + h - 1;
  if (x2 < 0 || y2 < 0 || x >= WEATHER_ICON_W || y >= WEATHER_ICON_H) {
    return;
  }

  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x2 >= WEATHER_ICON_W) x2 = WEATHER_ICON_W - 1;
  if (y2 >= WEATHER_ICON_H) y2 = WEATHER_ICON_H - 1;

  lv_color_t lvColor = lv_color_hex(color);
  for (int yy = y; yy <= y2; yy++) {
    for (int xx = x; xx <= x2; xx++) {
      lv_canvas_set_px(weatherCanvas, xx, yy, lvColor, LV_OPA_COVER);
    }
  }
}

void weatherCanvasFillCircle(int cx, int cy, int r, uint32_t color) {
  int rr = r * r;
  for (int y = -r; y <= r; y++) {
    for (int x = -r; x <= r; x++) {
      if ((x * x) + (y * y) <= rr) {
        weatherCanvasSetPixel(cx + x, cy + y, color);
      }
    }
  }
}

void drawWeatherSun(int cx, int cy) {
  weatherCanvasFillCircle(cx, cy, 9, COLOR_SUN);
  weatherCanvasFillRect(cx - 1, cy - 18, 3, 6, COLOR_SUN);
  weatherCanvasFillRect(cx - 1, cy + 12, 3, 6, COLOR_SUN);
  weatherCanvasFillRect(cx - 18, cy - 1, 6, 3, COLOR_SUN);
  weatherCanvasFillRect(cx + 12, cy - 1, 6, 3, COLOR_SUN);
}

void drawWeatherCloud(int x, int y, uint32_t color) {
  weatherCanvasFillCircle(x + 14, y + 16, 9, color);
  weatherCanvasFillCircle(x + 27, y + 11, 12, color);
  weatherCanvasFillCircle(x + 38, y + 17, 8, color);
  weatherCanvasFillRect(x + 8, y + 17, 37, 13, color);
}

void drawWeatherRain() {
  weatherCanvasFillRect(18, 34, 3, 10, COLOR_RAIN);
  weatherCanvasFillRect(28, 35, 3, 10, COLOR_RAIN);
  weatherCanvasFillRect(38, 34, 3, 10, COLOR_RAIN);
}

void drawWeatherSnow() {
  weatherCanvasFillRect(20, 35, 12, 3, COLOR_MOON);
  weatherCanvasFillRect(25, 30, 3, 13, COLOR_MOON);
  weatherCanvasFillRect(36, 35, 12, 3, COLOR_MOON);
  weatherCanvasFillRect(41, 30, 3, 13, COLOR_MOON);
}

void drawWeatherFog() {
  weatherCanvasFillRect(12, 32, 34, 4, COLOR_FOG);
  weatherCanvasFillRect(17, 40, 26, 4, COLOR_FOG);
}

void drawWeatherLightning() {
  weatherCanvasFillRect(30, 29, 8, 9, COLOR_ORANGE);
  weatherCanvasFillRect(25, 36, 10, 4, COLOR_ORANGE);
  weatherCanvasFillRect(25, 39, 6, 7, COLOR_ORANGE);
}

lv_obj_t* createWeatherCanvas(lv_obj_t* parent) {
  const size_t bufferBytes = LV_CANVAS_BUF_SIZE(WEATHER_ICON_W, WEATHER_ICON_H, 16, LV_DRAW_BUF_STRIDE_ALIGN);
  weatherCanvasBuf = (uint8_t*)heap_caps_malloc(bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  weatherCanvasBufInPsram = (weatherCanvasBuf != nullptr);
  if (weatherCanvasBuf == nullptr) {
    weatherCanvasBuf = (uint8_t*)heap_caps_malloc(bufferBytes, MALLOC_CAP_8BIT);
    weatherCanvasBufInPsram = false;
  }

  if (weatherCanvasBuf == nullptr) {
    Serial.println("Wetter-Canvas Buffer Init fehlgeschlagen");
    return nullptr;
  }

  Serial.printf("Wetter-Canvas Buffer: %u Bytes in %s\n", (unsigned)bufferBytes, weatherCanvasBufInPsram ? "PSRAM" : "internem RAM");
  weatherCanvas = lv_canvas_create(parent);
  lv_canvas_set_buffer(weatherCanvas, weatherCanvasBuf, WEATHER_ICON_W, WEATHER_ICON_H, LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(weatherCanvas, 304, 72);
  lv_obj_clear_flag(weatherCanvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_canvas_fill_bg(weatherCanvas, lv_color_hex(COLOR_BG), LV_OPA_COVER);
  return weatherCanvas;
}

void updateWeatherCanvas(int code) {
  if (weatherCanvas == nullptr) {
    return;
  }

  WeatherVisual visual = weatherVisualFromCode(code);
  if (weatherCanvasDrawn && visual == lastWeatherVisual) {
    return;
  }

  lastWeatherVisual = visual;
  weatherCanvasDrawn = true;
  lv_canvas_fill_bg(weatherCanvas, lv_color_hex(COLOR_BG), LV_OPA_COVER);

  switch (visual) {
    case WEATHER_VISUAL_CLEAR:
      drawWeatherSun(28, 22);
      break;
    case WEATHER_VISUAL_PARTLY:
      drawWeatherSun(20, 18);
      drawWeatherCloud(12, 14, COLOR_CLOUD);
      break;
    case WEATHER_VISUAL_RAIN:
      drawWeatherCloud(8, 8, COLOR_CLOUD);
      drawWeatherRain();
      break;
    case WEATHER_VISUAL_SNOW:
      drawWeatherCloud(8, 8, COLOR_CLOUD);
      drawWeatherSnow();
      break;
    case WEATHER_VISUAL_FOG:
      drawWeatherCloud(8, 5, COLOR_FOG);
      drawWeatherFog();
      break;
    case WEATHER_VISUAL_THUNDER:
      drawWeatherCloud(8, 7, COLOR_CLOUD);
      drawWeatherLightning();
      break;
    case WEATHER_VISUAL_CLOUD:
    case WEATHER_VISUAL_UNKNOWN:
    default:
      drawWeatherCloud(8, 10, COLOR_CLOUD);
      break;
  }

  lv_obj_invalidate(weatherCanvas);
}

SunStatusIcon createSunStatusIcon(lv_obj_t* parent) {
  SunStatusIcon icon;

  icon.root = lv_obj_create(parent);
  lv_obj_remove_style_all(icon.root);
  lv_obj_set_size(icon.root, 42, 42);
  lv_obj_set_pos(icon.root, 416, 13);
  lv_obj_set_style_bg_opa(icon.root, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(icon.root, LV_OBJ_FLAG_SCROLLABLE);

  icon.rayTop = createSunRay(icon.root, 20, 1, 3, 7);
  icon.rayRight = createSunRay(icon.root, 34, 20, 7, 3);
  icon.rayBottom = createSunRay(icon.root, 20, 34, 3, 7);
  icon.rayLeft = createSunRay(icon.root, 1, 20, 7, 3);

  icon.disk = lv_obj_create(icon.root);
  styleFilledRect(icon.disk, COLOR_SUN, 100);
  lv_obj_set_size(icon.disk, 24, 24);
  lv_obj_set_pos(icon.disk, 9, 9);

  icon.shade = lv_obj_create(icon.root);
  styleFilledRect(icon.shade, COLOR_BG, 100);
  lv_obj_set_size(icon.shade, 24, 24);
  lv_obj_set_pos(icon.shade, 22, 6);

  icon.starTop = lv_obj_create(icon.root);
  styleFilledRect(icon.starTop, COLOR_MOON, 100);
  lv_obj_set_size(icon.starTop, 4, 4);
  lv_obj_set_pos(icon.starTop, 31, 7);
  setHidden(icon.starTop, true);

  icon.starBottom = lv_obj_create(icon.root);
  styleFilledRect(icon.starBottom, COLOR_MOON, 100);
  lv_obj_set_size(icon.starBottom, 3, 3);
  lv_obj_set_pos(icon.starBottom, 35, 29);
  setHidden(icon.starBottom, true);

  return icon;
}

void setMoonStarsVisible(SunStatusIcon& icon, bool visible) {
  setHidden(icon.starTop, !visible);
  setHidden(icon.starBottom, !visible);
}

void setRayColorAndVisibility(SunStatusIcon& icon, uint32_t color, bool visible) {
  setHidden(icon.rayTop, !visible);
  setHidden(icon.rayRight, !visible);
  setHidden(icon.rayBottom, !visible);
  setHidden(icon.rayLeft, !visible);

  lv_obj_t* rays[] = { icon.rayTop, icon.rayRight, icon.rayBottom, icon.rayLeft };
  for (uint8_t i = 0; i < 4; i++) {
    if (rays[i] != nullptr) {
      lv_obj_set_style_bg_color(rays[i], lv_color_hex(color), 0);
    }
  }
}

uint32_t blendColor(uint32_t fromColor, uint32_t toColor, int percent) {
  percent = constrain(percent, 0, 100);
  const int fromR = (fromColor >> 16) & 0xff;
  const int fromG = (fromColor >> 8) & 0xff;
  const int fromB = fromColor & 0xff;
  const int toR = (toColor >> 16) & 0xff;
  const int toG = (toColor >> 8) & 0xff;
  const int toB = toColor & 0xff;

  const int r = fromR + (((toR - fromR) * percent) / 100);
  const int g = fromG + (((toG - fromG) * percent) / 100);
  const int b = fromB + (((toB - fromB) * percent) / 100);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

int calculateSunsetMoonProgress(const struct tm& timeinfo, int sunriseMinute, int sunsetMinute) {
  const int transitionBeforeSunset = 120;
  const int transitionAfterSunset = 30;
  const int nowMinute = (timeinfo.tm_hour * 60) + timeinfo.tm_min;

  if (nowMinute < sunriseMinute || nowMinute >= sunsetMinute + transitionAfterSunset) {
    return 100;
  }

  const int transitionStart = sunsetMinute - transitionBeforeSunset;
  if (nowMinute < transitionStart) {
    return 0;
  }

  const int transitionLength = transitionBeforeSunset + transitionAfterSunset;
  return ((nowMinute - transitionStart) * 100) / transitionLength;
}

void updateSunStatusIcon(SunStatusIcon& icon, bool nightMode, int sunStrength, int moonProgress) {
  if (icon.root == nullptr) {
    return;
  }

  setHidden(icon.root, false);

  if (nightMode && moonProgress >= 100) {
    lv_obj_set_style_bg_color(icon.disk, lv_color_hex(COLOR_MOON), 0);
    setRayColorAndVisibility(icon, COLOR_MOON, false);
    setMoonStarsVisible(icon, true);
    setHidden(icon.shade, false);
    lv_obj_set_pos(icon.shade, 17, 6);
    return;
  }

  setMoonStarsVisible(icon, false);

  uint32_t sunColor = COLOR_SUN;
  if (sunStrength < 35) {
    sunColor = COLOR_ORANGE;
  } else if (sunStrength < 65) {
    sunColor = 0xffb347;
  }

  lv_obj_set_style_bg_color(icon.disk, lv_color_hex(sunColor), 0);
  setRayColorAndVisibility(icon, sunColor, sunStrength >= 45);

  if (moonProgress > 0) {
    const uint32_t blendedColor = blendColor(sunColor, COLOR_MOON, moonProgress);
    const int shadeX = 30 - ((moonProgress * 13) / 100);
    lv_obj_set_style_bg_color(icon.disk, lv_color_hex(blendedColor), 0);
    setRayColorAndVisibility(icon, blendedColor, moonProgress < 35);
    setMoonStarsVisible(icon, moonProgress >= 60);
    setHidden(icon.shade, false);
    lv_obj_set_pos(icon.shade, shadeX, 6);
    return;
  }

  if (sunStrength >= 85) {
    setHidden(icon.shade, true);
    return;
  }

  setHidden(icon.shade, true);
}

void updateSunStatusIcon(const struct tm& timeinfo) {
  int sunriseMinute = 0;
  int sunsetMinute = 0;
  if (!calculateSunTimes(timeinfo, sunriseMinute, sunsetMinute)) {
    setHidden(timeSunIcon.root, true);
    return;
  }

  const int nowMinute = (timeinfo.tm_hour * 60) + timeinfo.tm_min;
  const bool nightMode = (nowMinute < sunriseMinute || nowMinute >= sunsetMinute);
  const int sunStrength = calculateSunStrength(timeinfo, sunriseMinute, sunsetMinute);
  const int moonProgress = calculateSunsetMoonProgress(timeinfo, sunriseMinute, sunsetMinute);

  updateSunStatusIcon(timeSunIcon, nightMode, sunStrength, moonProgress);
}

