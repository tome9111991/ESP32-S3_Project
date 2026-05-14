// 4-Screen Dashboard UI (Time/Wetter, Crypto, BTC Day, Klipper). Pure C-Port
// vom Arduino-Original (02_UiWeatherIcon.ino, 03_UiScreens.ino).

#include "app_state.h"
#include "i18n.h"
#include "ui_assets.h"
#include "ui_font_price_digits.h"
#include "ui_font_time_digits.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

static const char *TAG = "ui";

// --- Layout-Konstanten -------------------------------------------------------
#define PROMINENT_DIVIDER_H   5
#define DIVIDER_H             4
#define TIME_SECOND_BAR_W     440
#define TIME_SECOND_BAR_X     ((UI_LOGICAL_W - TIME_SECOND_BAR_W) / 2)
#define CRYPTO_PRICE_BAR_W    460
#define CRYPTO_PRICE_BAR_X    ((UI_LOGICAL_W - CRYPTO_PRICE_BAR_W) / 2)
#define WEATHER_ICON_W        112
#define WEATHER_ICON_H        96
#define WEATHER_ICON_Y        155
#define SCREEN_TRANSITION_MS  180

// --- Screen handles ----------------------------------------------------------
static lv_obj_t *s_time_screen, *s_crypto_screen, *s_btc_day_screen, *s_klipper_screen;

// Time screen widgets
static lv_obj_t *s_time_accent, *s_time_location_label, *s_time_label;
static lv_obj_t *s_weekday_label, *s_date_label, *s_temp_label;
static lv_obj_t *s_weather_icon_root, *s_time_divider, *s_time_second_fill;
static lv_obj_t *s_time_status_title, *s_time_status_detail;
static lv_obj_t *s_weather_image;
static lv_obj_t *s_sun_icon;
static weather_visual_t s_last_weather_visual = WEATHER_VISUAL_UNKNOWN;
static bool s_weather_image_drawn = false;
static sun_status_visual_t s_last_sun_visual = SUN_STATUS_VISUAL_UNKNOWN;
static int  s_last_time_second_progress = -1;

// Crypto screen widgets
static lv_obj_t *s_crypto_title_label, *s_crypto_price_label;
static lv_obj_t *s_crypto_status_label, *s_crypto_change_label;

// BTC day widgets
static lv_obj_t *s_btc_day_title_label, *s_btc_day_price_label, *s_btc_day_change_label;
static lv_obj_t *s_btc_day_time_range_label, *s_btc_day_volume_label, *s_btc_day_candle_label;
static lv_obj_t *s_btc_day_chart_canvas;
static lv_obj_t *s_btc_day_price_high_label, *s_btc_day_price_low_label, *s_btc_day_price_last_label;
static uint8_t  *s_btc_day_chart_canvas_buf = NULL;

// Klipper widgets
static lv_obj_t *s_klipper_accent, *s_klipper_divider;
static lv_obj_t *s_klipper_offline_icon, *s_klipper_offline_ring, *s_klipper_offline_stem, *s_klipper_offline_line;
static lv_obj_t *s_klipper_title_label, *s_klipper_state_label, *s_klipper_file_label;
static lv_obj_t *s_klipper_progress_label, *s_klipper_progress_arc;
static lv_obj_t *s_klipper_nozzle_title_label, *s_klipper_nozzle_label;
static lv_obj_t *s_klipper_bed_title_label, *s_klipper_bed_label;
static lv_obj_t *s_klipper_duration_label, *s_klipper_status_label;
static lv_obj_t *s_klipper_mmu_label;
static lv_obj_t *s_klipper_mmu_gate_box[MMU_GATE_MAX];
static lv_obj_t *s_klipper_mmu_gate_label[MMU_GATE_MAX];
static lv_obj_t *s_klipper_mmu_gate_empty_line[MMU_GATE_MAX];
static lv_point_precise_t s_klipper_mmu_gate_empty_pts[MMU_GATE_MAX][2];

static screen_state_t s_current_screen = SCREEN_TIME;

// --- Hilfsfunktionen ---------------------------------------------------------
static bool wifi_is_connected(void)
{
    bool connected = false;
    app_lock();
    connected = g_app.wifi_connected;
    app_unlock();
    return connected;
}

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (!obj) return;
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden) return;
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static bool set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    if (!label || !text) return false;
    const char *cur = lv_label_get_text(label);
    if (cur && strcmp(cur, text) == 0) return false;
    lv_label_set_text(label, text);
    return true;
}

static lv_obj_t *create_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    return screen;
}

static lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}

static lv_obj_t *create_accent(lv_obj_t *parent, uint32_t color)
{
    lv_obj_t *accent = lv_obj_create(parent);
    lv_obj_remove_style_all(accent);
    lv_obj_set_size(accent, 36, 5);
    lv_obj_set_style_bg_color(accent, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_set_pos(accent, 42, 58);
    return accent;
}

static lv_obj_t *create_divider_sized(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *divider = lv_obj_create(parent);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, w, h);
    lv_obj_set_style_bg_color(divider, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(divider, 2, 0);
    lv_obj_set_pos(divider, x, y);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    return divider;
}

static lv_obj_t *create_divider(lv_obj_t *parent, int x, int y, int w, uint32_t color)
{
    return create_divider_sized(parent, x, y, w, DIVIDER_H, color);
}

static void style_filled_rect(lv_obj_t *obj, uint32_t color, int radius)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static const char *weekday_name(int weekday)
{
    switch (weekday) {
        case 0: return T(WEEKDAY_SUN);
        case 1: return T(WEEKDAY_MON);
        case 2: return T(WEEKDAY_TUE);
        case 3: return T(WEEKDAY_WED);
        case 4: return T(WEEKDAY_THU);
        case 5: return T(WEEKDAY_FRI);
        case 6: return T(WEEKDAY_SAT);
        default: return "--";
    }
}

// --- Weather icon / sun icon -------------------------------------------------
static weather_visual_t weather_visual_from_code(int code)
{
    if (code == 0) return WEATHER_VISUAL_CLEAR;
    if (code >= 1 && code <= 2) return WEATHER_VISUAL_PARTLY;
    if (code == 3) return WEATHER_VISUAL_CLOUD;
    if (code == 45 || code == 48) return WEATHER_VISUAL_FOG;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return WEATHER_VISUAL_RAIN;
    if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return WEATHER_VISUAL_SNOW;
    if (code >= 95 && code <= 99) return WEATHER_VISUAL_THUNDER;
    return WEATHER_VISUAL_UNKNOWN;
}

static const lv_image_dsc_t *weather_image_for_visual(weather_visual_t visual)
{
    switch (visual) {
        case WEATHER_VISUAL_CLEAR:    return &icon_weather_clear;
        case WEATHER_VISUAL_PARTLY:   return &icon_weather_partly_cloudy;
        case WEATHER_VISUAL_RAIN:     return &icon_weather_rain;
        case WEATHER_VISUAL_SNOW:     return &icon_weather_snow;
        case WEATHER_VISUAL_FOG:      return &icon_weather_fog;
        case WEATHER_VISUAL_THUNDER:  return &icon_weather_thunder;
        case WEATHER_VISUAL_CLOUD:
        case WEATHER_VISUAL_UNKNOWN:
        default:                      return &icon_weather_cloudy;
    }
}

static int weather_visual_left_inset(weather_visual_t v)
{
    switch (v) {
        case WEATHER_VISUAL_CLEAR:    return 12;
        case WEATHER_VISUAL_PARTLY:   return 8;
        case WEATHER_VISUAL_CLOUD:
        case WEATHER_VISUAL_UNKNOWN:  return 15;
        default:                      return 17;
    }
}

static int time_text_pixel_width(const char *text)
{
    if (!text) return 0;
    int width = 0;
    for (const char *c = text; *c; c++) {
        lv_font_glyph_dsc_t glyph;
        memset(&glyph, 0, sizeof(glyph));
        uint32_t letter      = (uint8_t)*c;
        uint32_t next_letter = (uint8_t)*(c + 1);
        if (lv_font_get_glyph_dsc(&ui_font_time_digits_160, &glyph, letter, next_letter)) {
            width += glyph.adv_w;
        }
    }
    return width;
}

static void place_weather_image(const char *time_text)
{
    if (!s_weather_image) return;
    int weather_x = 600;
    const int time_width = time_text_pixel_width(time_text);
    if (s_time_label && time_width > 0) {
        const int time_right = lv_obj_get_x(s_time_label) +
                               ((lv_obj_get_width(s_time_label) + time_width) / 2);
        const int visual_gap = 33;
        weather_x = time_right + visual_gap - weather_visual_left_inset(s_last_weather_visual);
    }
    lv_obj_set_size(s_weather_image, WEATHER_ICON_W, WEATHER_ICON_H);
    // 30 % der Icon-Hoehe hoeher als vorher, damit es naeher an der Uhr sitzt.
    lv_obj_set_pos(s_weather_image, weather_x, WEATHER_ICON_Y);
}

static void update_weather_image(int code)
{
    if (!s_weather_image) return;
    weather_visual_t visual = weather_visual_from_code(code);
    if (s_weather_image_drawn && visual == s_last_weather_visual) return;
    s_last_weather_visual = visual;
    s_weather_image_drawn = true;
    lv_image_set_src(s_weather_image, weather_image_for_visual(visual));
    place_weather_image(s_time_label ? lv_label_get_text(s_time_label) : NULL);
    lv_obj_invalidate(s_weather_image);
}

// --- Sun rise/set Helper (Berechnung jetzt in display_brightness.c) --------
static sun_status_visual_t sun_status_visual_from_time(int now_min, int sunrise_min, int sunset_min)
{
    const int sunrise_lead = 30, sunrise_after = 60;
    const int sunset_lead = 90,  sunset_after = 30;
    if (now_min >= sunrise_min - sunrise_lead && now_min < sunrise_min + sunrise_after)
        return SUN_STATUS_VISUAL_SUNRISE;
    if (now_min >= sunset_min - sunset_lead && now_min < sunset_min + sunset_after)
        return SUN_STATUS_VISUAL_SUNSET;
    if (now_min >= sunrise_min && now_min < sunset_min) return SUN_STATUS_VISUAL_DAY;
    return SUN_STATUS_VISUAL_NIGHT;
}

static const lv_image_dsc_t *sun_status_image_for_visual(sun_status_visual_t v)
{
    switch (v) {
        case SUN_STATUS_VISUAL_SUNRISE: return &icon_status_sunrise_line;
        case SUN_STATUS_VISUAL_SUNSET:  return &icon_status_sunset_line;
        case SUN_STATUS_VISUAL_NIGHT:   return &icon_status_night_line;
        case SUN_STATUS_VISUAL_DAY:
        case SUN_STATUS_VISUAL_UNKNOWN:
        default:                        return &icon_status_day_line;
    }
}

static void update_sun_status_icon(const struct tm *t)
{
    int sunrise_min = 0, sunset_min = 0;
    if (!calculate_sun_times(t, &sunrise_min, &sunset_min)) {
        set_hidden(s_sun_icon, true);
        return;
    }
    const int now_min = (t->tm_hour * 60) + t->tm_min;
    sun_status_visual_t visual = sun_status_visual_from_time(now_min, sunrise_min, sunset_min);
    set_hidden(s_sun_icon, false);
    if (s_last_sun_visual == visual) return;
    s_last_sun_visual = visual;
    lv_image_set_src(s_sun_icon, sun_status_image_for_visual(visual));
    lv_obj_invalidate(s_sun_icon);
}

static void update_time_second_progress(int second)
{
    if (!s_time_second_fill) return;
    if (second < 0) {
        s_last_time_second_progress = -1;
        set_hidden(s_time_second_fill, true);
        return;
    }
    if (second > 59) second = 59;
    if (second == s_last_time_second_progress) return;
    s_last_time_second_progress = second;
    const int fill_w = (second * TIME_SECOND_BAR_W) / 59;
    set_hidden(s_time_second_fill, fill_w <= 0);
    if (fill_w > 0) lv_obj_set_width(s_time_second_fill, fill_w);
}

static void set_time_normal_visible(bool visible)
{
    set_hidden(s_time_accent, !visible);
    set_hidden(s_time_location_label, !visible);
    set_hidden(s_time_label, !visible);
    set_hidden(s_weekday_label, !visible);
    set_hidden(s_date_label, !visible);
    set_hidden(s_temp_label, !visible);
    set_hidden(s_weather_icon_root, !visible);
    set_hidden(s_time_divider, !visible);
    set_hidden(s_time_status_title, visible);
    set_hidden(s_time_status_detail, visible);
}

// --- Screen builders ---------------------------------------------------------
static void create_time_screen(void)
{
    s_time_screen = create_screen();
    s_time_accent = create_accent(s_time_screen, COLOR_CYAN);

    // Sun status icon
    s_sun_icon = lv_image_create(s_time_screen);
    lv_obj_set_size(s_sun_icon, 70, 70);
    lv_obj_set_pos(s_sun_icon, 708, 34);
    lv_image_set_pivot(s_sun_icon, 35, 35);
    lv_image_set_scale(s_sun_icon, 220);
    lv_obj_remove_flag(s_sun_icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_src(s_sun_icon, &icon_status_day_line);

    s_time_location_label = create_label(s_time_screen, &lv_font_montserrat_30, COLOR_DIM, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(s_time_location_label, 460, 40);
    lv_obj_set_pos(s_time_location_label, 96, 38);
    lv_label_set_text(s_time_location_label, g_app.weather_location);

    s_time_label = create_label(s_time_screen, &ui_font_time_digits_160, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(s_time_label, 620, 176);
    lv_obj_set_pos(s_time_label, 90, 102);

    s_weather_image = lv_image_create(s_time_screen);
    place_weather_image(NULL);
    lv_obj_remove_flag(s_weather_image, LV_OBJ_FLAG_SCROLLABLE);
    s_weather_icon_root = s_weather_image;
    update_weather_image(g_app.weather_code);

    s_time_divider = create_divider_sized(s_time_screen, TIME_SECOND_BAR_X, 292, TIME_SECOND_BAR_W, PROMINENT_DIVIDER_H, COLOR_DIM);
    s_time_second_fill = create_divider_sized(s_time_divider, 0, 0, 0, PROMINENT_DIVIDER_H, COLOR_CYAN);
    set_hidden(s_time_second_fill, true);

    s_weekday_label = create_label(s_time_screen, &lv_font_montserrat_40, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(s_weekday_label, 720, 52);
    lv_obj_align(s_weekday_label, LV_ALIGN_TOP_MID, 0, 322);

    s_date_label = create_label(s_time_screen, &lv_font_montserrat_30, COLOR_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(s_date_label, 720, 40);
    lv_obj_align(s_date_label, LV_ALIGN_TOP_MID, 0, 364);

    s_temp_label = create_label(s_time_screen, &lv_font_montserrat_48, COLOR_CYAN, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(s_temp_label, 720, 64);
    lv_obj_align(s_temp_label, LV_ALIGN_TOP_MID, 6, 400);

    s_time_status_title = create_label(s_time_screen, &lv_font_montserrat_48, COLOR_ORANGE, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(s_time_status_title, 720, 64);
    lv_obj_align(s_time_status_title, LV_ALIGN_CENTER, 0, -18);

    s_time_status_detail = create_label(s_time_screen, &lv_font_montserrat_30, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(s_time_status_detail, 720, 42);
    lv_obj_align(s_time_status_detail, LV_ALIGN_CENTER, 0, 38);
}

static void create_crypto_screen(void)
{
    s_crypto_screen = create_screen();
    create_accent(s_crypto_screen, COLOR_BTC);

    s_crypto_title_label = create_label(s_crypto_screen, &lv_font_montserrat_30, COLOR_DIM, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(s_crypto_title_label, 460, 40);
    lv_obj_set_pos(s_crypto_title_label, 96, 40);
    char title[32]; crypto_pair_title_text(title, sizeof(title));
    lv_label_set_text(s_crypto_title_label, title);

    s_crypto_price_label = create_label(s_crypto_screen, &lv_font_montserrat_48, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(s_crypto_price_label, 720, 96);
    lv_obj_align(s_crypto_price_label, LV_ALIGN_TOP_MID, 0, 138);

    create_divider_sized(s_crypto_screen, CRYPTO_PRICE_BAR_X, 260, CRYPTO_PRICE_BAR_W, PROMINENT_DIVIDER_H, COLOR_BTC);

    s_crypto_change_label = create_label(s_crypto_screen, &lv_font_montserrat_40, COLOR_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(s_crypto_change_label, 720, 56);
    lv_obj_align(s_crypto_change_label, LV_ALIGN_TOP_MID, 0, 282);

    s_crypto_status_label = create_label(s_crypto_screen, &lv_font_montserrat_30, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(s_crypto_status_label, 720, 42);
    lv_obj_align(s_crypto_status_label, LV_ALIGN_TOP_MID, 0, 380);
}

static void create_btc_day_screen(void)
{
    s_btc_day_screen = create_screen();
    create_accent(s_btc_day_screen, COLOR_BTC);

    s_btc_day_title_label = create_label(s_btc_day_screen, &lv_font_montserrat_30, COLOR_DIM, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(s_btc_day_title_label, 390, 40);
    lv_obj_set_pos(s_btc_day_title_label, 96, 40);
    char title[32]; crypto_day_title_text(title, sizeof(title));
    lv_label_set_text(s_btc_day_title_label, title);

    s_btc_day_change_label = create_label(s_btc_day_screen, &lv_font_montserrat_40, COLOR_MUTED, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_size(s_btc_day_change_label, 260, 50);
    lv_obj_set_pos(s_btc_day_change_label, 500, 36);

    s_btc_day_price_label = create_label(s_btc_day_screen, &lv_font_montserrat_48, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(s_btc_day_price_label, 720, 64);
    lv_obj_align(s_btc_day_price_label, LV_ALIGN_TOP_MID, 0, 86);

    const size_t chart_buf_bytes = LV_CANVAS_BUF_SIZE(BTC_CHART_W, BTC_CHART_CANVAS_H, 16, LV_DRAW_BUF_STRIDE_ALIGN);
    s_btc_day_chart_canvas_buf = (uint8_t *)heap_caps_malloc(chart_buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_btc_day_chart_canvas_buf) {
        s_btc_day_chart_canvas_buf = (uint8_t *)heap_caps_malloc(chart_buf_bytes, MALLOC_CAP_8BIT);
    }
    if (s_btc_day_chart_canvas_buf) {
        ESP_LOGI(TAG, "BTC Chart Canvas Buffer: %u Bytes", (unsigned)chart_buf_bytes);
        s_btc_day_chart_canvas = lv_canvas_create(s_btc_day_screen);
        lv_canvas_set_buffer(s_btc_day_chart_canvas, s_btc_day_chart_canvas_buf,
                             BTC_CHART_W, BTC_CHART_CANVAS_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(s_btc_day_chart_canvas, 40, 142);
        lv_obj_remove_flag(s_btc_day_chart_canvas, LV_OBJ_FLAG_SCROLLABLE);
        lv_canvas_fill_bg(s_btc_day_chart_canvas, lv_color_hex(COLOR_BG), LV_OPA_COVER);
    }

    s_btc_day_time_range_label = create_label(s_btc_day_screen, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(s_btc_day_time_range_label, 400, 32);
    lv_obj_set_pos(s_btc_day_time_range_label, 40, 416);

    s_btc_day_candle_label = create_label(s_btc_day_screen, &lv_font_montserrat_28, COLOR_BTC, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_size(s_btc_day_candle_label, 300, 32);
    lv_obj_set_pos(s_btc_day_candle_label, 460, 414);

    s_btc_day_volume_label = create_label(s_btc_day_screen, &lv_font_montserrat_28, COLOR_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(s_btc_day_volume_label, 720, 32);
    lv_obj_align(s_btc_day_volume_label, LV_ALIGN_TOP_MID, 0, 446);

    s_btc_day_price_high_label = create_label(s_btc_day_screen, &lv_font_montserrat_18, COLOR_MUTED, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_size(s_btc_day_price_high_label, 96, 22);
    lv_obj_set_pos(s_btc_day_price_high_label, 660, 146);
    lv_obj_set_style_bg_color(s_btc_day_price_high_label, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_btc_day_price_high_label, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(s_btc_day_price_high_label, 4, 0);
    lv_obj_set_style_pad_ver(s_btc_day_price_high_label, 1, 0);
    lv_obj_set_style_radius(s_btc_day_price_high_label, 3, 0);
    lv_label_set_text(s_btc_day_price_high_label, "--");

    s_btc_day_price_low_label = create_label(s_btc_day_screen, &lv_font_montserrat_18, COLOR_MUTED, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_size(s_btc_day_price_low_label, 96, 22);
    lv_obj_set_pos(s_btc_day_price_low_label, 660, 372);
    lv_obj_set_style_bg_color(s_btc_day_price_low_label, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_btc_day_price_low_label, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(s_btc_day_price_low_label, 4, 0);
    lv_obj_set_style_pad_ver(s_btc_day_price_low_label, 1, 0);
    lv_obj_set_style_radius(s_btc_day_price_low_label, 3, 0);
    lv_label_set_text(s_btc_day_price_low_label, "--");

    s_btc_day_price_last_label = create_label(s_btc_day_screen, &lv_font_montserrat_18, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(s_btc_day_price_last_label, 96, 22);
    lv_obj_set_pos(s_btc_day_price_last_label, 660, 250);
    lv_obj_set_style_bg_color(s_btc_day_price_last_label, lv_color_hex(COLOR_BTC), 0);
    lv_obj_set_style_bg_opa(s_btc_day_price_last_label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_btc_day_price_last_label, 4, 0);
    lv_obj_set_style_pad_ver(s_btc_day_price_last_label, 1, 0);
    lv_obj_set_style_radius(s_btc_day_price_last_label, 3, 0);
    lv_label_set_text(s_btc_day_price_last_label, "--");
    lv_obj_add_flag(s_btc_day_price_last_label, LV_OBJ_FLAG_HIDDEN);
}

static void create_klipper_screen(void)
{
    s_klipper_screen = create_screen();
    s_klipper_accent = create_accent(s_klipper_screen, COLOR_GREEN);

    s_klipper_title_label = create_label(s_klipper_screen, &lv_font_montserrat_30, COLOR_DIM, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(s_klipper_title_label, 420, 40);
    lv_obj_set_pos(s_klipper_title_label, 96, 40);
    lv_label_set_text(s_klipper_title_label, T(SCREEN_KLIPPER_SHORT));

    s_klipper_state_label = create_label(s_klipper_screen, &lv_font_montserrat_48, COLOR_TEXT, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_size(s_klipper_state_label, 260, 62);
    lv_obj_set_pos(s_klipper_state_label, 500, 30);

    s_klipper_progress_arc = lv_arc_create(s_klipper_screen);
    lv_obj_set_size(s_klipper_progress_arc, 154, 154);
    lv_obj_align(s_klipper_progress_arc, LV_ALIGN_TOP_MID, 0, 88);
    lv_arc_set_rotation(s_klipper_progress_arc, 270);
    lv_arc_set_bg_angles(s_klipper_progress_arc, 0, 360);
    lv_arc_set_range(s_klipper_progress_arc, 0, 100);
    lv_arc_set_value(s_klipper_progress_arc, 0);
    lv_obj_remove_style(s_klipper_progress_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_klipper_progress_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_klipper_progress_arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_arc_width(s_klipper_progress_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_klipper_progress_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_klipper_progress_arc, lv_color_hex(COLOR_DIM), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_klipper_progress_arc, lv_color_hex(COLOR_GREEN), LV_PART_INDICATOR);

    s_klipper_progress_label = create_label(s_klipper_screen, &lv_font_montserrat_48, COLOR_GREEN, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(s_klipper_progress_label, 260, 66);
    lv_obj_align(s_klipper_progress_label, LV_ALIGN_TOP_MID, 0, 130);

    s_klipper_divider = create_divider(s_klipper_screen, 210, 258, 380, COLOR_GREEN);
    set_hidden(s_klipper_divider, true);

    s_klipper_offline_icon = lv_image_create(s_klipper_screen);
    lv_image_set_src(s_klipper_offline_icon, &icon_status_offline);
    lv_obj_set_size(s_klipper_offline_icon, 147, 147);
    lv_obj_set_pos(s_klipper_offline_icon, 94, 119);
    lv_obj_remove_flag(s_klipper_offline_icon, LV_OBJ_FLAG_SCROLLABLE);
    set_hidden(s_klipper_offline_icon, true);

    s_klipper_offline_ring = lv_obj_create(s_klipper_screen);
    lv_obj_remove_style_all(s_klipper_offline_ring);
    lv_obj_set_size(s_klipper_offline_ring, 118, 118);
    lv_obj_set_pos(s_klipper_offline_ring, 108, 148);
    lv_obj_set_style_bg_opa(s_klipper_offline_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_klipper_offline_ring, 8, 0);
    lv_obj_set_style_border_color(s_klipper_offline_ring, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_radius(s_klipper_offline_ring, 59, 0);
    lv_obj_remove_flag(s_klipper_offline_ring, LV_OBJ_FLAG_SCROLLABLE);
    set_hidden(s_klipper_offline_ring, true);

    s_klipper_offline_stem = lv_obj_create(s_klipper_screen);
    style_filled_rect(s_klipper_offline_stem, COLOR_DIM, 3);
    lv_obj_set_size(s_klipper_offline_stem, 16, 58);
    lv_obj_set_pos(s_klipper_offline_stem, 159, 118);
    set_hidden(s_klipper_offline_stem, true);

    s_klipper_offline_line = create_divider(s_klipper_screen, 286, 204, 360, COLOR_DIM);
    set_hidden(s_klipper_offline_line, true);

    s_klipper_file_label = create_label(s_klipper_screen, &lv_font_montserrat_30, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(s_klipper_file_label, 700, 42);
    lv_obj_align(s_klipper_file_label, LV_ALIGN_TOP_MID, 0, 262);

    s_klipper_nozzle_title_label = create_label(s_klipper_screen, &lv_font_montserrat_30, COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(s_klipper_nozzle_title_label, 120, 40);
    lv_obj_set_pos(s_klipper_nozzle_title_label, 130, 320);
    lv_label_set_text(s_klipper_nozzle_title_label, "Nozzle");

    s_klipper_nozzle_label = create_label(s_klipper_screen, &lv_font_montserrat_30, COLOR_CYAN, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(s_klipper_nozzle_label, 180, 40);
    lv_obj_set_pos(s_klipper_nozzle_label, 250, 320);

    s_klipper_bed_title_label = create_label(s_klipper_screen, &lv_font_montserrat_30, COLOR_MUTED, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_size(s_klipper_bed_title_label, 100, 40);
    lv_obj_set_pos(s_klipper_bed_title_label, 440, 320);
    lv_label_set_text(s_klipper_bed_title_label, T(KLIPPER_BED));

    s_klipper_bed_label = create_label(s_klipper_screen, &lv_font_montserrat_30, COLOR_CYAN, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_size(s_klipper_bed_label, 150, 40);
    lv_obj_set_pos(s_klipper_bed_label, 550, 320);

    s_klipper_duration_label = create_label(s_klipper_screen, &lv_font_montserrat_28, COLOR_DIM, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(s_klipper_duration_label, 300, 36);
    lv_obj_set_pos(s_klipper_duration_label, 130, 362);

    s_klipper_status_label = create_label(s_klipper_screen, &lv_font_montserrat_28, COLOR_DIM, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_size(s_klipper_status_label, 340, 36);
    lv_obj_set_pos(s_klipper_status_label, 330, 362);

    s_klipper_mmu_label = create_label(s_klipper_screen, &lv_font_montserrat_28, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_size(s_klipper_mmu_label, 700, 32);
    lv_obj_align(s_klipper_mmu_label, LV_ALIGN_TOP_MID, 0, 402);

    const int gate_w = 70;
    const int gate_h = 36;
    const int gate_gap = 10;
    const int gate_start_x = 85;
    for (int i = 0; i < MMU_GATE_MAX; i++) {
        s_klipper_mmu_gate_box[i] = lv_obj_create(s_klipper_screen);
        style_filled_rect(s_klipper_mmu_gate_box[i], COLOR_DIM, 3);
        lv_obj_set_size(s_klipper_mmu_gate_box[i], gate_w, gate_h);
        lv_obj_set_pos(s_klipper_mmu_gate_box[i], gate_start_x + (i * (gate_w + gate_gap)), 438);
        lv_obj_set_style_outline_color(s_klipper_mmu_gate_box[i], lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_outline_pad(s_klipper_mmu_gate_box[i], 2, 0);
        lv_obj_set_style_outline_width(s_klipper_mmu_gate_box[i], 0, 0);

        // Diagonale Linie markiert ein leeres MMU-Gate.
        s_klipper_mmu_gate_empty_pts[i][0].x = 0;
        s_klipper_mmu_gate_empty_pts[i][0].y = gate_h;
        s_klipper_mmu_gate_empty_pts[i][1].x = gate_w;
        s_klipper_mmu_gate_empty_pts[i][1].y = 0;
        s_klipper_mmu_gate_empty_line[i] = lv_line_create(s_klipper_mmu_gate_box[i]);
        lv_line_set_points(s_klipper_mmu_gate_empty_line[i], s_klipper_mmu_gate_empty_pts[i], 2);
        lv_obj_set_style_line_color(s_klipper_mmu_gate_empty_line[i], lv_color_hex(COLOR_LOSS), 0);
        lv_obj_set_style_line_width(s_klipper_mmu_gate_empty_line[i], 5, 0);
        lv_obj_set_style_line_opa(s_klipper_mmu_gate_empty_line[i], LV_OPA_70, 0);
        lv_obj_set_pos(s_klipper_mmu_gate_empty_line[i], 0, 0);
        set_hidden(s_klipper_mmu_gate_empty_line[i], true);

        s_klipper_mmu_gate_label[i] = create_label(s_klipper_mmu_gate_box[i], &lv_font_montserrat_28,
                                                   COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
        char gate_label[4];
        snprintf(gate_label, sizeof(gate_label), "T%d", i);
        lv_label_set_text(s_klipper_mmu_gate_label[i], gate_label);
        lv_obj_center(s_klipper_mmu_gate_label[i]);
        set_hidden(s_klipper_mmu_gate_box[i], true);
    }
}

// --- Refresh per Screen ------------------------------------------------------
static void refresh_time_ui(void)
{
    if (!g_app.wifi_connected) {
        set_time_normal_visible(false);
        update_time_second_progress(-1);
        set_hidden(s_sun_icon, true);
        lv_obj_set_style_text_color(s_time_status_title, lv_color_hex(COLOR_RED), 0);
        set_label_text_if_changed(s_time_status_title, T(WIFI_CONNECTING));
        set_label_text_if_changed(s_time_status_detail, T(COMMON_WAIT));
        return;
    }

    char temp[16], status[32], location[48];
    int  code;
    app_lock();
    strncpy(temp,     g_app.current_temp,      sizeof(temp));      temp[sizeof(temp) - 1] = 0;
    strncpy(status,   g_app.weather_status,    sizeof(status));    status[sizeof(status) - 1] = 0;
    strncpy(location, g_app.weather_location,  sizeof(location));  location[sizeof(location) - 1] = 0;
    code = g_app.weather_code;
    app_unlock();

    time_t now = time(NULL);
    struct tm tinfo;
    if (now < 100000 || !localtime_r(&now, &tinfo)) {
        set_time_normal_visible(true);
        set_hidden(s_sun_icon, true);
        update_time_second_progress(-1);
        update_weather_image(code);
        char temp_text[24]; snprintf(temp_text, sizeof(temp_text), "%s%s", temp, TEMP_UNIT);
        set_label_text_if_changed(s_time_location_label, location);
        set_label_text_if_changed(s_time_label, "--:--");
        place_weather_image("--:--");
        set_label_text_if_changed(s_weekday_label, T(WEATHER_SYNC_TIME));
        set_label_text_if_changed(s_date_label, status);
        set_label_text_if_changed(s_temp_label, temp_text);
        return;
    }

    char time_buf[16], date_buf[32], weekday_date_buf[64];
    strftime(time_buf, sizeof(time_buf), "%H:%M", &tinfo);
    strftime(date_buf, sizeof(date_buf), "%d.%m.%Y", &tinfo);
    snprintf(weekday_date_buf, sizeof(weekday_date_buf), "%s | %s",
             weekday_name(tinfo.tm_wday), date_buf);

    set_time_normal_visible(true);
    set_hidden(s_date_label, true);
    update_sun_status_icon(&tinfo);
    update_time_second_progress(tinfo.tm_sec);
    update_weather_image(code);
    char temp_text[24]; snprintf(temp_text, sizeof(temp_text), "%s%s", temp, TEMP_UNIT);
    set_label_text_if_changed(s_time_location_label, location);
    set_label_text_if_changed(s_time_label, time_buf);
    place_weather_image(time_buf);
    set_label_text_if_changed(s_weekday_label, weekday_date_buf);
    set_label_text_if_changed(s_temp_label, temp_text);
}

static bool price_font_supports_codepoint(uint32_t cp)
{
    if (cp == ' ' || cp == '$' || cp == '.' || cp == ',' || cp == '-' || cp == '+') return true;
    if (cp >= '0' && cp <= '9') return true;
    return cp == 0x20ac || cp == 0x00a3 || cp == 0x00a5 || cp == 0x20bf;
}

static uint32_t next_utf8_codepoint(const char **cursor)
{
    const uint8_t first = (uint8_t)*(*cursor)++;
    if ((first & 0x80) == 0) return first;
    if ((first & 0xe0) == 0xc0 && ((**cursor) & 0xc0) == 0x80) {
        uint32_t cp = ((uint32_t)(first & 0x1f) << 6) | ((uint8_t)*(*cursor)++ & 0x3f);
        return cp;
    }
    if ((first & 0xf0) == 0xe0 && ((*cursor)[0] & 0xc0) == 0x80 && ((*cursor)[1] & 0xc0) == 0x80) {
        uint8_t second = (uint8_t)*(*cursor)++;
        uint8_t third = (uint8_t)*(*cursor)++;
        uint32_t cp = ((uint32_t)(first & 0x0f) << 12) |
                      ((uint32_t)(second & 0x3f) << 6) |
                      (uint32_t)(third & 0x3f);
        return cp;
    }
    return first;
}

static bool crypto_price_can_use_large_font(const char *price)
{
    bool has_digit = false;
    const char *cursor = price;
    while (*cursor) {
        uint32_t cp = next_utf8_codepoint(&cursor);
        if (cp >= '0' && cp <= '9') has_digit = true;
        if (!price_font_supports_codepoint(cp)) return false;
    }
    return has_digit;
}

static void refresh_crypto_ui(void)
{
    char price[24], status[32];
    float open_price, live_price;
    bool open_ready;
    app_lock();
    strncpy(price,  g_app.crypto_price,  sizeof(price));  price[sizeof(price) - 1] = 0;
    strncpy(status, g_app.crypto_status, sizeof(status)); status[sizeof(status) - 1] = 0;
    open_price = g_app.crypto_24h_open;
    open_ready = g_app.crypto_24h_ready;
    live_price = g_app.crypto_live_price;
    app_unlock();

    char title[32]; crypto_pair_title_text(title, sizeof(title));
    set_label_text_if_changed(s_crypto_title_label, title);

    const lv_font_t *font = crypto_price_can_use_large_font(price) ?
        &ui_font_price_digits_80 : &lv_font_montserrat_48;
    lv_obj_set_style_text_font(s_crypto_price_label, font, 0);
    set_label_text_if_changed(s_crypto_price_label, price);

    char change_text[40];
    uint32_t change_color;
    if (open_ready && open_price > 0.0f && live_price > 0.0f) {
        float pct = (live_price - open_price) / open_price * 100.0f;
        bool positive = pct >= 0.0f;
        snprintf(change_text, sizeof(change_text), "%s 24H %s%.2f%%",
                 positive ? LV_SYMBOL_UP : LV_SYMBOL_DOWN,
                 positive ? "+" : "", pct);
        change_color = positive ? COLOR_GREEN : COLOR_LOSS;
    } else {
        snprintf(change_text, sizeof(change_text), "24H --");
        change_color = COLOR_DIM;
    }
    set_label_text_if_changed(s_crypto_change_label, change_text);
    lv_obj_set_style_text_color(s_crypto_change_label, lv_color_hex(change_color), 0);

    set_label_text_if_changed(s_crypto_status_label, status);
    bool service_ok = strncmp(status, g_crypto.service, strlen(g_crypto.service)) == 0;
    lv_obj_set_style_text_color(s_crypto_status_label,
        lv_color_hex(service_ok ? COLOR_MUTED : COLOR_RED), 0);
}

static void format_btc_candle_countdown(uint32_t candle_time, char *out, size_t out_size)
{
    time_t now = time(NULL);
    if (now <= 100000 || candle_time == 0) {
        snprintf(out, out_size, "LIVE");
        return;
    }
    int elapsed = (int)(now - candle_time);
    if (elapsed < 0) elapsed = 0;
    int candle_seconds = (int)crypto_chart_granularity_seconds();
    if (elapsed >= candle_seconds) elapsed = candle_seconds - 1;
    int remaining = candle_seconds - elapsed;
    int remaining_hours = remaining / 3600;
    int remaining_min = (remaining % 3600) / 60;
    if (remaining_hours > 0)
        snprintf(out, out_size, "LIVE %dH %dM", remaining_hours, remaining_min);
    else
        snprintf(out, out_size, "LIVE %dM", remaining_min < 1 ? 1 : remaining_min);
}

static void crypto_chart_price_text(const char *fallback_price, float live_price, char *out, size_t out_size)
{
    if (isfinite(live_price) && live_price > 0.0f) {
        int decimals = 2;
        if (live_price >= 10000.0f) decimals = 0;
        else if (live_price < 1.0f) decimals = 4;
        snprintf(out, out_size, "%s %.*f", g_crypto.quote, decimals, live_price);
        return;
    }
    snprintf(out, out_size, "%s %s", g_crypto.quote, fallback_price[0] ? fallback_price : "--");
}

static void refresh_btc_day_ui(void)
{
    char price[24], change[24], time_range[64], volume[40], candle[24];
    bool positive, ready;
    int  price_direction;
    float live_price;
    uint32_t candle_time = 0;
    app_lock();
    strncpy(price,      g_app.crypto_price,        sizeof(price));      price[sizeof(price)-1] = 0;
    strncpy(change,     g_app.btc_day_change,      sizeof(change));     change[sizeof(change)-1] = 0;
    strncpy(time_range, g_app.btc_day_time_range,  sizeof(time_range)); time_range[sizeof(time_range)-1] = 0;
    strncpy(volume,     g_app.btc_day_volume,      sizeof(volume));     volume[sizeof(volume)-1] = 0;
    strncpy(candle,     g_app.btc_candle_status,   sizeof(candle));     candle[sizeof(candle)-1] = 0;
    positive   = g_app.btc_day_change_positive;
    ready      = g_app.btc_day_data_ready;
    price_direction = g_app.crypto_price_direction;
    live_price = g_app.crypto_live_price;
    if (g_app.candles && g_app.candle_count > 0)
        candle_time = g_app.candles[g_app.candle_count - 1].time;
    app_unlock();

    if (ready) format_btc_candle_countdown(candle_time, candle, sizeof(candle));

    char title[32]; crypto_day_title_text(title, sizeof(title));
    char chart_price_buf[40], chart_price_full[48];
    crypto_chart_price_text(price, live_price, chart_price_buf, sizeof(chart_price_buf));
    if (price_direction > 0)
        snprintf(chart_price_full, sizeof(chart_price_full), "%s %s", LV_SYMBOL_UP, chart_price_buf);
    else if (price_direction < 0)
        snprintf(chart_price_full, sizeof(chart_price_full), "%s %s", LV_SYMBOL_DOWN, chart_price_buf);
    else
        snprintf(chart_price_full, sizeof(chart_price_full), "%s", chart_price_buf);

    set_label_text_if_changed(s_btc_day_title_label, title);
    set_label_text_if_changed(s_btc_day_price_label, chart_price_full);
    set_label_text_if_changed(s_btc_day_change_label, change);
    set_label_text_if_changed(s_btc_day_time_range_label, time_range);
    set_label_text_if_changed(s_btc_day_volume_label, volume);
    set_label_text_if_changed(s_btc_day_candle_label, candle);
    lv_obj_set_style_text_color(s_btc_day_price_label,
        lv_color_hex(price_direction > 0 ? COLOR_GREEN : (price_direction < 0 ? COLOR_LOSS : COLOR_TEXT)), 0);
    lv_obj_set_style_text_color(s_btc_day_change_label,
        lv_color_hex(!ready ? COLOR_MUTED : (positive ? COLOR_GREEN : COLOR_LOSS)), 0);
}

static void set_klipper_offline_layout(uint32_t state_color)
{
    lv_obj_set_style_bg_color(s_klipper_accent, lv_color_hex(state_color), 0);
    set_hidden(s_klipper_progress_arc, true);
    set_hidden(s_klipper_divider, true);
    set_hidden(s_klipper_offline_ring, true);
    set_hidden(s_klipper_offline_stem, true);
    set_hidden(s_klipper_offline_line, true);
    set_hidden(s_klipper_offline_icon, false);
    lv_obj_align(s_klipper_offline_icon, LV_ALIGN_TOP_MID, 0, 104);
    set_hidden(s_klipper_progress_label, true);
    lv_obj_set_size(s_klipper_file_label, 720, 44);
    lv_obj_align(s_klipper_file_label, LV_ALIGN_TOP_MID, 0, 280);
    lv_label_set_long_mode(s_klipper_file_label, LV_LABEL_LONG_DOT);
    set_hidden(s_klipper_file_label, false);
    lv_obj_set_size(s_klipper_mmu_label, 720, 36);
    lv_obj_align(s_klipper_mmu_label, LV_ALIGN_TOP_MID, 0, 336);
    set_hidden(s_klipper_mmu_label, false);
    set_hidden(s_klipper_nozzle_title_label, true);
    set_hidden(s_klipper_nozzle_label, true);
    set_hidden(s_klipper_bed_title_label, true);
    set_hidden(s_klipper_bed_label, true);
    set_hidden(s_klipper_duration_label, true);
    set_hidden(s_klipper_status_label, true);
    for (int i = 0; i < MMU_GATE_MAX; i++) {
        set_hidden(s_klipper_mmu_gate_box[i], true);
    }
}

static void set_klipper_online_layout(void)
{
    lv_obj_set_style_bg_color(s_klipper_accent, lv_color_hex(COLOR_GREEN), 0);
    set_hidden(s_klipper_progress_arc, false);
    lv_obj_set_size(s_klipper_progress_arc, 154, 154);
    lv_obj_align(s_klipper_progress_arc, LV_ALIGN_TOP_MID, 0, 88);
    set_hidden(s_klipper_divider, true);
    set_hidden(s_klipper_offline_icon, true);
    set_hidden(s_klipper_offline_ring, true);
    set_hidden(s_klipper_offline_stem, true);
    set_hidden(s_klipper_offline_line, true);
    set_hidden(s_klipper_progress_label, false);
    lv_obj_set_size(s_klipper_progress_label, 260, 66);
    lv_obj_align(s_klipper_progress_label, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_set_size(s_klipper_file_label, 700, 42);
    lv_obj_align(s_klipper_file_label, LV_ALIGN_TOP_MID, 0, 262);
    lv_label_set_long_mode(s_klipper_file_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_klipper_file_label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_size(s_klipper_duration_label, 300, 36);
    lv_obj_set_pos(s_klipper_duration_label, 130, 362);
    lv_obj_set_size(s_klipper_status_label, 340, 36);
    lv_obj_set_pos(s_klipper_status_label, 330, 362);
    lv_obj_set_size(s_klipper_mmu_label, 700, 32);
    lv_obj_align(s_klipper_mmu_label, LV_ALIGN_TOP_MID, 0, 402);
    set_hidden(s_klipper_nozzle_title_label, false);
    set_hidden(s_klipper_nozzle_label, false);
    set_hidden(s_klipper_bed_title_label, false);
    set_hidden(s_klipper_bed_label, false);
    set_hidden(s_klipper_duration_label, false);
    set_hidden(s_klipper_status_label, false);
    set_hidden(s_klipper_mmu_label, false);
}

static int progress_percent_from_text(const char *text)
{
    if (!text) return -1;
    size_t len = strlen(text);
    while (len > 0 && text[len-1] == ' ') len--;
    if (len == 0 || text[len-1] != '%') return -1;
    len--;
    while (len > 0 && text[len-1] == ' ') len--;
    if (len == 0) return -1;
    char buf[8] = {0};
    size_t copy = len < sizeof(buf)-1 ? len : sizeof(buf)-1;
    memcpy(buf, text, copy);
    int v = atoi(buf);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    return v;
}

static void refresh_klipper_ui(void)
{
    bool available, host_available;
    bool mmu_available;
    int mmu_gate, mmu_gate_count;
    char state[24], file[96], progress[8], nozzle[24], bed[24];
    char duration[40], status[64], display_msg[64], printer_name[48];
    char connection_state[24], connection_message[96], mmu_info[64];
    uint32_t mmu_gate_colors[MMU_GATE_MAX];
    int mmu_gate_status[MMU_GATE_MAX];

    app_lock();
    available = g_app.klipper_available;
    host_available = g_app.klipper_host_available;
    mmu_available = g_app.klipper_mmu_available;
    mmu_gate = g_app.klipper_mmu_gate;
    mmu_gate_count = g_app.klipper_mmu_gate_count;
    strncpy(connection_state,   g_app.klipper_connection_state,   sizeof(connection_state));   connection_state[sizeof(connection_state)-1] = 0;
    strncpy(connection_message, g_app.klipper_connection_message, sizeof(connection_message)); connection_message[sizeof(connection_message)-1] = 0;
    strncpy(state,        g_app.klipper_state,           sizeof(state));        state[sizeof(state)-1] = 0;
    strncpy(file,         g_app.klipper_file,            sizeof(file));         file[sizeof(file)-1] = 0;
    strncpy(progress,     g_app.klipper_progress,        sizeof(progress));     progress[sizeof(progress)-1] = 0;
    strncpy(nozzle,       g_app.klipper_nozzle,          sizeof(nozzle));       nozzle[sizeof(nozzle)-1] = 0;
    strncpy(bed,          g_app.klipper_bed,             sizeof(bed));          bed[sizeof(bed)-1] = 0;
    strncpy(duration,     g_app.klipper_duration,        sizeof(duration));     duration[sizeof(duration)-1] = 0;
    strncpy(status,       g_app.klipper_status,          sizeof(status));       status[sizeof(status)-1] = 0;
    strncpy(display_msg,  g_app.klipper_display_message, sizeof(display_msg));  display_msg[sizeof(display_msg)-1] = 0;
    strncpy(printer_name, g_app.klipper_printer_name,    sizeof(printer_name)); printer_name[sizeof(printer_name)-1] = 0;
    strncpy(mmu_info,     g_app.klipper_mmu_info,        sizeof(mmu_info));     mmu_info[sizeof(mmu_info)-1] = 0;
    for (int i = 0; i < MMU_GATE_MAX; i++) {
        mmu_gate_colors[i] = g_app.klipper_mmu_gate_colors[i];
        mmu_gate_status[i] = g_app.klipper_mmu_gate_status[i];
    }
    app_unlock();

    if (!available) {
        const char *state_text = host_available ? state : "OFFLINE";
        const char *detail_text = host_available ? (file[0] ? file : status) : T(KLIPPER_MOONRAKER_UNREACHABLE);
        uint32_t state_color = COLOR_ORANGE;
        if (!host_available || strcasecmp(connection_state, "error") == 0) {
            state_color = COLOR_LOSS;
        } else if (strcasecmp(connection_state, "shutdown") == 0 || strcasecmp(connection_state, "disconnected") == 0) {
            state_color = COLOR_DIM;
        }
        set_klipper_offline_layout(state_color);
        set_label_text_if_changed(s_klipper_title_label, host_available ? printer_name : T(SCREEN_KLIPPER_SHORT));
        set_label_text_if_changed(s_klipper_state_label, state_text);
        set_label_text_if_changed(s_klipper_file_label, detail_text);
        set_label_text_if_changed(s_klipper_mmu_label,
                                  host_available ? T(KLIPPER_POWER_ON_PRINTER) : T(KLIPPER_WAITING_MAINSAIL));
        lv_obj_set_style_text_color(s_klipper_state_label, lv_color_hex(state_color), 0);
        lv_obj_set_style_text_color(s_klipper_file_label, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_color(s_klipper_mmu_label, lv_color_hex(COLOR_MUTED), 0);
        return;
    }

    set_klipper_online_layout();
    set_label_text_if_changed(s_klipper_state_label, state);
    set_label_text_if_changed(s_klipper_title_label, printer_name);
    set_label_text_if_changed(s_klipper_file_label, file);
    set_label_text_if_changed(s_klipper_progress_label, progress);
    set_label_text_if_changed(s_klipper_nozzle_label, nozzle);
    set_label_text_if_changed(s_klipper_bed_label, bed);
    set_label_text_if_changed(s_klipper_duration_label, duration);
    set_label_text_if_changed(s_klipper_status_label, display_msg[0] ? display_msg : status);
    set_label_text_if_changed(s_klipper_mmu_label, mmu_available ? mmu_info : T(KLIPPER_MMU_INACTIVE));

    uint32_t state_color = COLOR_GREEN;
    if (strcmp(state, T(PRINT_STATE_PAUSED)) == 0) state_color = COLOR_ORANGE;
    else if (strcmp(state, T(PRINT_STATE_ERROR)) == 0) state_color = COLOR_LOSS;
    else if (strcmp(state, T(PRINT_STATE_READY)) == 0 || strcmp(state, "STANDBY") == 0) state_color = COLOR_MUTED;

    lv_obj_set_style_text_color(s_klipper_state_label, lv_color_hex(state_color), 0);
    lv_obj_set_style_text_color(s_klipper_progress_label, lv_color_hex(state_color), 0);
    int pct = progress_percent_from_text(progress);
    set_hidden(s_klipper_progress_arc, pct < 0);
    if (pct >= 0) {
        lv_arc_set_value(s_klipper_progress_arc, pct);
        lv_obj_set_style_arc_color(s_klipper_progress_arc, lv_color_hex(COLOR_DIM), LV_PART_MAIN);
        lv_obj_set_style_arc_color(s_klipper_progress_arc, lv_color_hex(state_color), LV_PART_INDICATOR);
    }
    lv_obj_set_style_text_color(s_klipper_nozzle_label, lv_color_hex(COLOR_CYAN), 0);
    lv_obj_set_style_text_color(s_klipper_bed_label, lv_color_hex(COLOR_CYAN), 0);
    lv_obj_set_style_text_color(s_klipper_status_label, lv_color_hex(display_msg[0] ? COLOR_CYAN : COLOR_DIM), 0);

    for (int i = 0; i < MMU_GATE_MAX; i++) {
        bool gate_visible = mmu_available && i < mmu_gate_count;
        set_hidden(s_klipper_mmu_gate_box[i], !gate_visible);
        if (!gate_visible) {
            set_hidden(s_klipper_mmu_gate_empty_line[i], true);
            continue;
        }

        bool gate_empty = mmu_gate_status[i] == 0;
        uint32_t gate_color = gate_empty ? COLOR_DIM : mmu_gate_colors[i];
        lv_obj_set_style_bg_color(s_klipper_mmu_gate_box[i], lv_color_hex(gate_color), 0);
        lv_obj_set_style_outline_width(s_klipper_mmu_gate_box[i], i == mmu_gate ? 3 : 0, 0);
        set_hidden(s_klipper_mmu_gate_empty_line[i], !gate_empty);

        uint8_t r = (gate_color >> 16) & 0xff;
        uint8_t g = (gate_color >> 8) & 0xff;
        uint8_t b = gate_color & 0xff;
        uint32_t label_color = ((uint16_t)r + (uint16_t)g + (uint16_t)b) > 380 ? COLOR_BG : COLOR_TEXT;
        lv_obj_set_style_text_color(s_klipper_mmu_gate_label[i], lv_color_hex(label_color), 0);
    }
}

// --- Public API --------------------------------------------------------------
void ui_init(void)
{
    create_time_screen();
    create_crypto_screen();
    create_btc_day_screen();
    create_klipper_screen();
    lv_screen_load(s_time_screen);
    s_current_screen = SCREEN_TIME;
    ui_refresh_current();
}

void ui_refresh_current(void)
{
    switch (s_current_screen) {
        case SCREEN_CRYPTO:  refresh_crypto_ui();  break;
        case SCREEN_BTC_DAY: refresh_btc_day_ui(); break;
        case SCREEN_KLIPPER: refresh_klipper_ui(); break;
        case SCREEN_TIME:
        default:             refresh_time_ui();    break;
    }
}

screen_state_t ui_current_screen(void) { return s_current_screen; }

static bool screen_is_available(screen_state_t state)
{
    if (!screen_settings_is_enabled(state)) return false;

    // Ohne WLAN bleibt das Dashboard auf dem "WLAN verbindet"-Time-Screen.
    if (!wifi_is_connected() && state != SCREEN_TIME) return false;
    if (state != SCREEN_KLIPPER) return true;

    bool available = false;
    app_lock();
    available = g_app.klipper_available || g_app.klipper_host_available;
    app_unlock();
    if (available) return true;

    // Wenn nur Klipper aktiv ist, darf die Offline-Seite sichtbar bleiben.
    int others = 0;
    if (screen_settings_is_enabled(SCREEN_TIME)) others++;
    if (screen_settings_is_enabled(SCREEN_CRYPTO)) others++;
    if (screen_settings_is_enabled(SCREEN_BTC_DAY)) others++;
    return others == 0;
}

bool ui_screen_is_available(screen_state_t state)
{
    return screen_is_available(state);
}

screen_state_t ui_next_screen(screen_state_t state)
{
    for (int i = 1; i <= 4; i++) {
        screen_state_t candidate = (screen_state_t)(((int)state + i) % 4);
        if (screen_is_available(candidate)) return candidate;
    }
    return SCREEN_TIME;
}

screen_state_t ui_previous_screen(screen_state_t state)
{
    for (int i = 1; i <= 4; i++) {
        screen_state_t candidate = (screen_state_t)(((int)state + 4 - i) % 4);
        if (screen_is_available(candidate)) return candidate;
    }
    return SCREEN_TIME;
}

void ui_switch_screen(screen_state_t target)
{
    lv_obj_t *targets[4] = { s_time_screen, s_crypto_screen, s_btc_day_screen, s_klipper_screen };
    if ((int)target < 0 || (int)target > 3) target = SCREEN_TIME;
    if (!wifi_is_connected()) target = SCREEN_TIME;
    if (!screen_is_available(target)) target = ui_next_screen(target);
    if (target == s_current_screen && lv_screen_active() == targets[target]) return;
    s_current_screen = target;
    ui_refresh_current();
    lv_screen_load_anim(targets[target], LV_SCREEN_LOAD_ANIM_FADE_IN,
                        SCREEN_TRANSITION_MS, 0, false);
}

void ui_load_current_screen_no_anim(void)
{
    lv_obj_t *targets[4] = { s_time_screen, s_crypto_screen, s_btc_day_screen, s_klipper_screen };
    screen_state_t target = s_current_screen;
    if ((int)target < 0 || (int)target > 3) target = SCREEN_TIME;
    if (!screen_is_available(target)) target = ui_next_screen(target);
    if ((int)target < 0 || (int)target > 3) target = SCREEN_TIME;
    s_current_screen = target;
    ui_refresh_current();
    lv_screen_load(targets[target]);
}

// Liefert true, wenn (x,y) im User-Space innerhalb des Wetter-Icons auf dem
// Time-Screen liegt. main.c benutzt das, um einen Tap aufs Icon vom normalen
// Screen-Swipe (linke/rechte Bildschirmhaelfte) zu trennen.
bool ui_time_weather_hitbox_contains(int x, int y)
{
    if (s_current_screen != SCREEN_TIME) return false;
    if (!s_weather_image) return false;
    if (lv_obj_has_flag(s_weather_image, LV_OBJ_FLAG_HIDDEN)) return false;
    int ix = lv_obj_get_x(s_weather_image);
    int iy = lv_obj_get_y(s_weather_image);
    int iw = lv_obj_get_width(s_weather_image);
    int ih = lv_obj_get_height(s_weather_image);
    if (iw <= 0) iw = WEATHER_ICON_W;
    if (ih <= 0) ih = WEATHER_ICON_H;
    // 16 px Komfort-Padding fuer Fat-Finger.
    const int pad = 16;
    return x >= ix - pad && x < ix + iw + pad &&
           y >= iy - pad && y < iy + ih + pad;
}

lv_obj_t *ui_get_btc_chart_canvas(void)     { return s_btc_day_chart_canvas; }
lv_obj_t *ui_get_btc_price_high_label(void) { return s_btc_day_price_high_label; }
lv_obj_t *ui_get_btc_price_low_label(void)  { return s_btc_day_price_low_label; }
lv_obj_t *ui_get_btc_price_last_label(void) { return s_btc_day_price_last_label; }
