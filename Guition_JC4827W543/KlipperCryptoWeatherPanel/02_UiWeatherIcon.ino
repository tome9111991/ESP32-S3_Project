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

lv_obj_t* createWeatherImage(lv_obj_t* parent) {
  weatherImage = lv_image_create(parent);
  lv_obj_set_size(weatherImage, WEATHER_ICON_W, WEATHER_ICON_H);
  lv_obj_set_pos(weatherImage, 360, 82);
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
  lv_obj_invalidate(weatherImage);
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
    setHidden(timeSunIcon.root, true);
    return;
  }

  const int nowMinute = (timeinfo.tm_hour * 60) + timeinfo.tm_min;
  const SunStatusVisual visual = sunStatusVisualFromTime(nowMinute, sunriseMinute, sunsetMinute);
  updateSunStatusIcon(timeSunIcon, visual);
}

