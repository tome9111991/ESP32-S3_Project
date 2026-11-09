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

  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden) {
    return;
  }

  if (hidden) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

// FNV-1a Hash; 0 ist fuer "noch nie gesetzt" reserviert.
static uint32_t labelTextHash(const char* text) {
  uint32_t hash = 2166136261UL;
  for (const char* cursor = text; *cursor != '\0'; cursor++) {
    hash ^= (uint8_t)*cursor;
    hash *= 16777619UL;
  }
  return hash != 0 ? hash : 1;
}

bool setLabelTextIfChanged(lv_obj_t* label, const char* text) {
  if (label == nullptr || text == nullptr) {
    return false;
  }

  // Nicht mit lv_label_get_text() vergleichen: LONG_DOT schreibt "..." in den
  // Label-Text, dann waere der Vergleich bei gekuerzten Texten immer ungleich.
  // Stattdessen Hash des zuletzt gesetzten Originaltexts in user_data merken.
  const uint32_t hash = labelTextHash(text);
  if ((uint32_t)(uintptr_t)lv_obj_get_user_data(label) == hash) {
    return false;
  }

  lv_label_set_text(label, text);
  lv_obj_set_user_data(label, (void*)(uintptr_t)hash);
  return true;
}

// LVGL-Style-Setter invalidieren immer (auch bei gleichem Wert) -> im FULL-Render-Mode
// wird dann der ganze Screen neu gezeichnet. Diese Helper setzen nur bei echter Aenderung.
static bool localStyleColorEquals(lv_obj_t* obj, lv_style_prop_t prop, lv_style_selector_t selector, lv_color_t color) {
  lv_style_value_t value;
  if (lv_obj_get_local_style_prop(obj, prop, &value, selector) != LV_STYLE_RES_FOUND) {
    return false;
  }
  return lv_color_eq(value.color, color);
}

void setTextColorIfChanged(lv_obj_t* obj, uint32_t color) {
  if (obj == nullptr) {
    return;
  }
  const lv_color_t lvColor = lv_color_hex(color);
  if (!localStyleColorEquals(obj, LV_STYLE_TEXT_COLOR, 0, lvColor)) {
    lv_obj_set_style_text_color(obj, lvColor, 0);
  }
}

void setBgColorIfChanged(lv_obj_t* obj, uint32_t color) {
  if (obj == nullptr) {
    return;
  }
  const lv_color_t lvColor = lv_color_hex(color);
  if (!localStyleColorEquals(obj, LV_STYLE_BG_COLOR, 0, lvColor)) {
    lv_obj_set_style_bg_color(obj, lvColor, 0);
  }
}

void setArcIndicatorColorIfChanged(lv_obj_t* obj, uint32_t color) {
  if (obj == nullptr) {
    return;
  }
  const lv_color_t lvColor = lv_color_hex(color);
  if (!localStyleColorEquals(obj, LV_STYLE_ARC_COLOR, LV_PART_INDICATOR, lvColor)) {
    lv_obj_set_style_arc_color(obj, lvColor, LV_PART_INDICATOR);
  }
}

void setBorderWidthIfChanged(lv_obj_t* obj, int32_t width) {
  if (obj == nullptr) {
    return;
  }
  lv_style_value_t value;
  if (lv_obj_get_local_style_prop(obj, LV_STYLE_BORDER_WIDTH, &value, 0) == LV_STYLE_RES_FOUND && value.num == width) {
    return;
  }
  lv_obj_set_style_border_width(obj, width, 0);
}

void setTimeNormalVisible(bool visible) {
  setHidden(timeAccent, !visible);
  setHidden(timeLocationLabel, !visible);
  setHidden(timeLabel, !visible);
  setHidden(timeAmPmLabel, !visible); // nullptr im 24h-Modus -> no-op
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

const lv_image_dsc_t* weatherImageForVisual(WeatherVisual visual) {
  switch (visual) {
    case WEATHER_VISUAL_CLEAR:
      return &icon_weather_clear;
    case WEATHER_VISUAL_PARTLY:
      return &icon_weather_partly_cloudy;
    case WEATHER_VISUAL_RAIN:
      return &icon_weather_rain;
    case WEATHER_VISUAL_SNOW:
      return &icon_weather_snow;
    case WEATHER_VISUAL_FOG:
      return &icon_weather_fog;
    case WEATHER_VISUAL_THUNDER:
      return &icon_weather_thunder;
    case WEATHER_VISUAL_CLOUD:
    case WEATHER_VISUAL_UNKNOWN:
    default:
      return &icon_weather_cloudy;
  }
}

int weatherVisualLeftInset(WeatherVisual visual) {
  switch (visual) {
    case WEATHER_VISUAL_CLEAR:
      return 7;
    case WEATHER_VISUAL_PARTLY:
      return 5;
    case WEATHER_VISUAL_CLOUD:
    case WEATHER_VISUAL_UNKNOWN:
      return 9;
    case WEATHER_VISUAL_RAIN:
    case WEATHER_VISUAL_SNOW:
    case WEATHER_VISUAL_FOG:
    case WEATHER_VISUAL_THUNDER:
    default:
      return 10;
  }
}

int timeTextPixelWidth(const char* text) {
  if (text == nullptr) {
    return 0;
  }

  int width = 0;
  for (const char* cursor = text; *cursor != '\0'; cursor++) {
    lv_font_glyph_dsc_t glyph = {};
    uint32_t letter = (uint8_t)*cursor;
    uint32_t nextLetter = (uint8_t)*(cursor + 1);
    if (lv_font_get_glyph_dsc(&ui_font_time_digits_96, &glyph, letter, nextLetter)) {
      width += glyph.adv_w;
    }
  }
  return width;
}

void placeWeatherImage(const char* timeText) {
  if (weatherImage == nullptr) {
    return;
  }

  int weatherX = 360;
  int amPmX = 360;
  const int timeWidth = timeTextPixelWidth(timeText);
  if (timeLabel != nullptr && timeWidth > 0) {
    const int timeRight = lv_obj_get_x(timeLabel) + ((lv_obj_get_width(timeLabel) + timeWidth) / 2);
    const int visualGap = 15;
    weatherX = timeRight + visualGap - weatherVisualLeftInset(lastWeatherVisual);
    amPmX = timeRight + visualGap;
  }

  lv_obj_set_size(weatherImage, WEATHER_ICON_W, WEATHER_ICON_H);
  lv_obj_set_pos(weatherImage, weatherX, 90);

  // 12h-Modus: AM/PM klein unter dem Wetter-Icon, rechts neben den Ziffern.
  if (timeAmPmLabel != nullptr) {
    lv_obj_set_pos(timeAmPmLabel, amPmX, 140);
  }
}

lv_obj_t* createWeatherImage(lv_obj_t* parent) {
  weatherImage = lv_image_create(parent);
  placeWeatherImage(nullptr);
  lv_obj_clear_flag(weatherImage, LV_OBJ_FLAG_SCROLLABLE);
  return weatherImage;
}

void updateWeatherImage(int code) {
  if (weatherImage == nullptr) {
    return;
  }

  WeatherVisual visual = weatherVisualFromCode(code);
  if (weatherImageDrawn && visual == lastWeatherVisual) {
    return;
  }

  lastWeatherVisual = visual;
  weatherImageDrawn = true;
  lv_image_set_src(weatherImage, weatherImageForVisual(visual));
  placeWeatherImage(timeLabel == nullptr ? nullptr : lv_label_get_text(timeLabel));
  lv_obj_invalidate(weatherImage);
}

void updateWeatherImagePositionForTime(const char* timeText) {
  placeWeatherImage(timeText);
}

SunStatusIcon createSunStatusIcon(lv_obj_t* parent) {
  SunStatusIcon icon = {};

  icon.root = lv_image_create(parent);
  lv_obj_set_size(icon.root, 42, 42);
  lv_obj_set_pos(icon.root, 430, 10);
  lv_image_set_pivot(icon.root, 21, 21);
  lv_image_set_scale(icon.root, 180);
  lv_obj_clear_flag(icon.root, LV_OBJ_FLAG_SCROLLABLE);
  icon.lastVisual = SUN_STATUS_VISUAL_UNKNOWN;
  lv_image_set_src(icon.root, &icon_status_day_line);

  return icon;
}

const lv_image_dsc_t* sunStatusImageForVisual(SunStatusVisual visual) {
  switch (visual) {
    case SUN_STATUS_VISUAL_SUNRISE:
      return &icon_status_sunrise_line;
    case SUN_STATUS_VISUAL_SUNSET:
      return &icon_status_sunset_line;
    case SUN_STATUS_VISUAL_NIGHT:
      return &icon_status_night_line;
    case SUN_STATUS_VISUAL_DAY:
    case SUN_STATUS_VISUAL_UNKNOWN:
    default:
      return &icon_status_day_line;
  }
}

SunStatusVisual sunStatusVisualFromTime(int nowMinute, int sunriseMinute, int sunsetMinute) {
  const int sunriseLeadMinutes = 30;
  const int sunriseAfterMinutes = 60;
  const int sunsetLeadMinutes = 90;
  const int sunsetAfterMinutes = 30;

  if (nowMinute >= sunriseMinute - sunriseLeadMinutes && nowMinute < sunriseMinute + sunriseAfterMinutes) {
    return SUN_STATUS_VISUAL_SUNRISE;
  }
  if (nowMinute >= sunsetMinute - sunsetLeadMinutes && nowMinute < sunsetMinute + sunsetAfterMinutes) {
    return SUN_STATUS_VISUAL_SUNSET;
  }
  if (nowMinute >= sunriseMinute && nowMinute < sunsetMinute) {
    return SUN_STATUS_VISUAL_DAY;
  }
  return SUN_STATUS_VISUAL_NIGHT;
}

void updateSunStatusIcon(SunStatusIcon& icon, SunStatusVisual visual) {
  if (icon.root == nullptr) {
    return;
  }

  setHidden(icon.root, false);
  if (icon.lastVisual == visual) {
    return;
  }

  icon.lastVisual = visual;
  lv_image_set_src(icon.root, sunStatusImageForVisual(visual));
  lv_obj_invalidate(icon.root);
}

void updateSunStatusIcon(const struct tm& timeinfo) {
  int sunriseMinute = 0;
  int sunsetMinute = 0;
  if (!calculateSunTimes(timeinfo, sunriseMinute, sunsetMinute)) {
    // Polartag/-nacht: Tag- bzw. Nacht-Icon statt gar keinem Icon.
    const int polar = polarSunState(timeinfo);
    if (polar == 0) {
      setHidden(timeSunIcon.root, true);
      return;
    }
    updateSunStatusIcon(timeSunIcon, polar > 0 ? SUN_STATUS_VISUAL_DAY : SUN_STATUS_VISUAL_NIGHT);
    return;
  }

  const int nowMinute = (timeinfo.tm_hour * 60) + timeinfo.tm_min;
  const SunStatusVisual visual = sunStatusVisualFromTime(nowMinute, sunriseMinute, sunsetMinute);
  updateSunStatusIcon(timeSunIcon, visual);
}

