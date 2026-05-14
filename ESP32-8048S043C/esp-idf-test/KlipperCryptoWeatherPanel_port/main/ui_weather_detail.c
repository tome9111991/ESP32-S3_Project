// Wetter-Detail-Screen: wird per Tap aufs Wetter-Icon im Time-Screen geoeffnet.
// Layout ist ein 2x3 Kachelraster (Icon/Temp/Gefuehlt/Wind/Feuchte/Sonne) plus
// 5-Tage-Forecast-Strip darunter. Quelle ist Open-Meteo; Daten liegen in
// g_app.weather_* und werden vom net_fetcher periodisch aktualisiert.
//
// Der Screen verhaelt sich wie die anderen Control-Screens (Settings, etc.):
// solange ui_weather_detail_is_open() true ist, pausiert main.c die Auto-
// Rotation und das Swipe-Routing.

#include "app_state.h"
#include "i18n.h"
#include "ui_assets.h"

#include "esp_log.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "wdetail";

#define TILE_BG_COLOR           0x151b24
#define TILE_RADIUS             12
#define TILE_OUTER_X            22
#define TILE_GRID_TOP_Y         80
#define TILE_W                  245
#define TILE_H                  105
#define TILE_GAP                12
#define TILE_CAPTION_Y          8
#define TILE_VALUE_Y            38

#define DAILY_HEADER_Y          318
#define DAILY_ROW_TOP_Y         352
#define DAILY_ROW_HEIGHT        24

static lv_obj_t *s_screen        = NULL;
static lv_obj_t *s_return_screen = NULL;

// --- Helfer ------------------------------------------------------------------
static void style_filled_rect(lv_obj_t *obj, uint32_t color, int radius)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                            lv_text_align_t align, int x, int y, int w, int h,
                            const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_size(label, w, h);
    lv_obj_set_pos(label, x, y);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
    return label;
}

// WMO-Code -> lesbare deutsche Beschreibung. Bewusst kurz gehalten, damit's bei
// kleiner Font in der Tagesliste und der Icon-Kachel passt.
static const char *weather_text_from_code(int code)
{
    if (code < 0) return "--";
    switch (code) {
        case 0:  return T(WEATHER_CLEAR);
        case 1:  return T(WEATHER_MAINLY_CLEAR);
        case 2:  return T(WEATHER_PARTLY_CLOUDY);
        case 3:  return T(WEATHER_OVERCAST);
        case 45: case 48: return T(WEATHER_FOG);
        case 51: return T(WEATHER_DRIZZLE_LIGHT);
        case 53: return T(WEATHER_DRIZZLE);
        case 55: return T(WEATHER_DRIZZLE_HEAVY);
        case 56: case 57: return T(WEATHER_FREEZING_DRIZZLE);
        case 61: return T(WEATHER_RAIN_LIGHT);
        case 63: return T(WEATHER_RAIN);
        case 65: return T(WEATHER_RAIN_HEAVY);
        case 66: case 67: return T(WEATHER_FREEZING_RAIN);
        case 71: return T(WEATHER_SNOW_LIGHT);
        case 73: return T(WEATHER_SNOW);
        case 75: return T(WEATHER_SNOW_HEAVY);
        case 77: return T(WEATHER_SNOW_GRAINS);
        case 80: return T(WEATHER_SHOWERS_LIGHT);
        case 81: return T(WEATHER_SHOWERS);
        case 82: return T(WEATHER_SHOWERS_HEAVY);
        case 85: case 86: return T(WEATHER_SNOW_SHOWERS);
        case 95: return T(WEATHER_THUNDER);
        case 96: case 99: return T(WEATHER_THUNDER_HAIL);
        default: return T(WEATHER_TITLE);
    }
}

static const lv_image_dsc_t *weather_icon_from_code(int code)
{
    if (code == 0) return &icon_weather_clear;
    if (code >= 1 && code <= 2) return &icon_weather_partly_cloudy;
    if (code == 3) return &icon_weather_cloudy;
    if (code == 45 || code == 48) return &icon_weather_fog;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return &icon_weather_rain;
    if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return &icon_weather_snow;
    if (code >= 95 && code <= 99) return &icon_weather_thunder;
    return &icon_weather_cloudy;
}

static const char *compass_from_degrees(int deg)
{
    if (deg < 0) return "";
    int idx = ((deg + 22) / 45) % 8;
    const char *const dirs[] = {
        T(DIR_N), T(DIR_NE), T(DIR_E), T(DIR_SE),
        T(DIR_S), T(DIR_SW), T(DIR_W), T(DIR_NW)
    };
    return dirs[idx];
}

static const char *weekday_short_name(int weekday)
{
    switch (weekday) {
        case 0: return T(WEEKDAY_SUN_SHORT);
        case 1: return T(WEEKDAY_MON_SHORT);
        case 2: return T(WEEKDAY_TUE_SHORT);
        case 3: return T(WEEKDAY_WED_SHORT);
        case 4: return T(WEEKDAY_THU_SHORT);
        case 5: return T(WEEKDAY_FRI_SHORT);
        case 6: return T(WEEKDAY_SAT_SHORT);
        default: return "--";
    }
}

// --- Close / Open ------------------------------------------------------------
static void close_detail(void)
{
    if (!s_screen) return;
    lv_obj_t *ret = s_return_screen;
    s_return_screen = NULL;
    if (ret) lv_screen_load(ret);
    lv_obj_delete_async(s_screen);
    s_screen = NULL;
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    close_detail();
}

// --- Kachel-Layout -----------------------------------------------------------
//
// Eine Kachel ist ein dunkles, abgerundetes Panel mit Caption oben und Wert
// mittig. Returnt das Panel, damit Aufrufer optional weitere Kinder einhaengen
// koennen (Icon-Kachel macht das fuer das grosse Wetter-Icon).
static lv_obj_t *make_tile(int col, int row)
{
    int x = TILE_OUTER_X + col * (TILE_W + TILE_GAP);
    int y = TILE_GRID_TOP_Y + row * (TILE_H + TILE_GAP);
    lv_obj_t *tile = lv_obj_create(s_screen);
    style_filled_rect(tile, TILE_BG_COLOR, TILE_RADIUS);
    lv_obj_set_size(tile, TILE_W, TILE_H);
    lv_obj_set_pos(tile, x, y);
    return tile;
}

static void tile_caption(lv_obj_t *tile, const char *caption)
{
    lv_obj_t *label = lv_label_create(tile);
    lv_obj_remove_style_all(label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_size(label, TILE_W - 24, 28);
    lv_obj_set_pos(label, 14, TILE_CAPTION_Y);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, caption);
}

static void tile_value(lv_obj_t *tile, const lv_font_t *font, uint32_t color,
                       const char *text, int y_override)
{
    lv_obj_t *label = lv_label_create(tile);
    lv_obj_remove_style_all(label);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    int h = font->line_height + 4;
    lv_obj_set_size(label, TILE_W - 24, h);
    lv_obj_set_pos(label, 14, y_override > 0 ? y_override : TILE_VALUE_Y);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
}

// --- Kacheln befuellen -------------------------------------------------------
static void build_tile_icon(int code)
{
    lv_obj_t *tile = make_tile(0, 0);
    // Icon zentriert, leichte Skalierung damit's die Kachel ausfuellt.
    lv_obj_t *icon = lv_image_create(tile);
    lv_image_set_src(icon, weather_icon_from_code(code));
    lv_image_set_scale(icon, 167);  // 112x96-Asset auf alte Detailgroesse gebracht.
    lv_obj_set_size(icon, 76, 70);
    lv_obj_set_pos(icon, 14, 18);
    // Beschreibung rechts daneben.
    lv_obj_t *desc = lv_label_create(tile);
    lv_obj_remove_style_all(desc);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_size(desc, TILE_W - 110, 70);
    lv_obj_set_pos(desc, 100, 22);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_label_set_text(desc, weather_text_from_code(code));
}

static void build_tile_temp(double temp_c)
{
    lv_obj_t *tile = make_tile(1, 0);
    tile_caption(tile, T(WEATHER_TEMP));
    char buf[16];
    if (isfinite(temp_c)) snprintf(buf, sizeof(buf), "%.1f%s", temp_c, TEMP_UNIT);
    else                  snprintf(buf, sizeof(buf), "--");
    tile_value(tile, &lv_font_montserrat_48, COLOR_CYAN, buf, 40);
}

static void build_tile_apparent(double apparent)
{
    lv_obj_t *tile = make_tile(2, 0);
    tile_caption(tile, T(WEATHER_FEELS_LIKE));
    char buf[16];
    if (isfinite(apparent)) snprintf(buf, sizeof(buf), "%.1f%s", apparent, TEMP_UNIT);
    else                    snprintf(buf, sizeof(buf), "--");
    tile_value(tile, &lv_font_montserrat_48, COLOR_TEXT, buf, 40);
}

static void build_tile_wind(double wind_kmh, int wind_dir)
{
    lv_obj_t *tile = make_tile(0, 1);
    tile_caption(tile, T(WEATHER_WIND));
    char buf[24];
    if (isfinite(wind_kmh)) snprintf(buf, sizeof(buf), "%.0f km/h", wind_kmh);
    else                    snprintf(buf, sizeof(buf), "--");
    tile_value(tile, &lv_font_montserrat_40, COLOR_TEXT, buf, 40);
    const char *compass = compass_from_degrees(wind_dir);
    if (compass[0]) {
        make_label(tile, &lv_font_montserrat_24, COLOR_DIM, LV_TEXT_ALIGN_RIGHT,
                   TILE_W - 90, 12, 76, 28, compass);
    }
}

static void build_tile_humidity(int humidity)
{
    lv_obj_t *tile = make_tile(1, 1);
    tile_caption(tile, T(WEATHER_HUMIDITY));
    char buf[16];
    if (humidity >= 0) snprintf(buf, sizeof(buf), "%d %%", humidity);
    else               snprintf(buf, sizeof(buf), "--");
    tile_value(tile, &lv_font_montserrat_48, COLOR_TEXT, buf, 40);
}

static void build_tile_sun(int sunrise_min, int sunset_min)
{
    lv_obj_t *tile = make_tile(2, 1);
    tile_caption(tile, T(WEATHER_SUN));
    // Zwei kleine Icons + Zeiten. Klemmt aufrecht in eine Zeile.
    lv_obj_t *sr_icon = lv_image_create(tile);
    lv_image_set_src(sr_icon, &icon_status_sunrise_line);
    lv_image_set_scale(sr_icon, 100);  // 100/256 ~ 0.39x
    lv_obj_set_size(sr_icon, 32, 32);
    lv_obj_set_pos(sr_icon, 8, 50);

    lv_obj_t *ss_icon = lv_image_create(tile);
    lv_image_set_src(ss_icon, &icon_status_sunset_line);
    lv_image_set_scale(ss_icon, 100);
    lv_obj_set_size(ss_icon, 32, 32);
    lv_obj_set_pos(ss_icon, 124, 50);

    char sr_buf[8] = "--:--", ss_buf[8] = "--:--";
    if (sunrise_min >= 0 && sunrise_min < 24 * 60) {
        snprintf(sr_buf, sizeof(sr_buf), "%02d:%02d", sunrise_min / 60, sunrise_min % 60);
    }
    if (sunset_min >= 0 && sunset_min < 24 * 60) {
        snprintf(ss_buf, sizeof(ss_buf), "%02d:%02d", sunset_min / 60, sunset_min % 60);
    }
    make_label(tile, &lv_font_montserrat_24, COLOR_TEXT, LV_TEXT_ALIGN_LEFT,
               46, 54, 80, 28, sr_buf);
    make_label(tile, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_LEFT,
               162, 54, 80, 28, ss_buf);
}

// --- Daily-Strip -------------------------------------------------------------
static void build_daily(const weather_daily_slot_t *daily, int count)
{
    make_label(s_screen, &lv_font_montserrat_24, COLOR_DIM, LV_TEXT_ALIGN_LEFT,
               TILE_OUTER_X, DAILY_HEADER_Y, 720, 26, T(WEATHER_5_DAYS));

    if (count <= 0) {
        make_label(s_screen, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_LEFT,
                   TILE_OUTER_X, DAILY_ROW_TOP_Y, 720, 30,
                   T(WEATHER_DAILY_LOADING));
        return;
    }

    // Zeilenlayout: Wochentag | Wetter-Icon | Beschreibung |
    //               Tropfen + Regen-% | Thermometer + min/max.
    const int col_wd          = TILE_OUTER_X;
    const int col_icon        = TILE_OUTER_X + 80;
    const int col_desc        = TILE_OUTER_X + 130;
    const int col_precip_icon = TILE_OUTER_X + 464;
    const int col_precip      = TILE_OUTER_X + 490;
    const int col_tt_icon     = TILE_OUTER_X + 586;
    const int col_tt          = TILE_OUTER_X + 612;

    for (int i = 0; i < count && i < WEATHER_DAILY_COUNT; i++) {
        const weather_daily_slot_t *d = &daily[i];
        int row_y = DAILY_ROW_TOP_Y + i * DAILY_ROW_HEIGHT;

        // Wochentag-Kurzname; erster Eintrag heisst "Heute".
        char wd_buf[12];
        if (i == 0) {
            snprintf(wd_buf, sizeof(wd_buf), "%s", T(WEATHER_TODAY));
        } else if (d->weekday >= 0 && d->weekday <= 6) {
            snprintf(wd_buf, sizeof(wd_buf), "%s", weekday_short_name(d->weekday));
        } else {
            snprintf(wd_buf, sizeof(wd_buf), "--");
        }
        uint32_t wd_color = (i == 0) ? COLOR_CYAN : COLOR_TEXT;
        make_label(s_screen, &lv_font_montserrat_24, wd_color, LV_TEXT_ALIGN_LEFT,
                   col_wd, row_y, 80, DAILY_ROW_HEIGHT, wd_buf);

        // Mini-Icon (unskaliert auf 38x38).
        if (d->code >= 0) {
            lv_obj_t *icon = lv_image_create(s_screen);
            lv_image_set_src(icon, weather_icon_from_code(d->code));
            lv_image_set_scale(icon, 83);  // 112x96-Asset auf alte Mini-Groesse gebracht.
            lv_obj_set_size(icon, 38, 32);
            lv_obj_set_pos(icon, col_icon, row_y - 4);
        }

        // Beschreibung.
        make_label(s_screen, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_LEFT,
                   col_desc, row_y, 320, DAILY_ROW_HEIGHT, weather_text_from_code(d->code));

        // Kleines Tropfen-Icon macht die Prozentzahl schneller erkennbar.
        lv_obj_t *precip_icon = lv_image_create(s_screen);
        lv_image_set_src(precip_icon, &icon_weather_precip_prob);
        lv_obj_set_size(precip_icon, 24, 24);
        lv_obj_set_pos(precip_icon, col_precip_icon, row_y);

        // Regenwahrscheinlichkeit (max).
        char prec_buf[16];
        if (d->precip_prob_max >= 0) snprintf(prec_buf, sizeof(prec_buf), "%d %%", d->precip_prob_max);
        else                          snprintf(prec_buf, sizeof(prec_buf), "--");
        make_label(s_screen, &lv_font_montserrat_24, COLOR_CYAN, LV_TEXT_ALIGN_LEFT,
                   col_precip, row_y, 70, DAILY_ROW_HEIGHT, prec_buf);

        // Thermometer markiert die Min/Max-Temperatur der Tageszeile.
        lv_obj_t *temp_icon = lv_image_create(s_screen);
        lv_image_set_src(temp_icon, &icon_weather_temp_range);
        lv_obj_set_size(temp_icon, 24, 24);
        lv_obj_set_pos(temp_icon, col_tt_icon, row_y);

        // min/max-Temperatur.
        char tt_buf[24];
        if (isfinite(d->tmin) && isfinite(d->tmax)) {
            snprintf(tt_buf, sizeof(tt_buf), "%.0f / %.0f%s", d->tmin, d->tmax, TEMP_UNIT);
        } else {
            snprintf(tt_buf, sizeof(tt_buf), "--");
        }
        make_label(s_screen, &lv_font_montserrat_24, COLOR_TEXT, LV_TEXT_ALIGN_LEFT,
                   col_tt, row_y, 160, DAILY_ROW_HEIGHT, tt_buf);
    }
}

// --- Public API --------------------------------------------------------------
bool ui_weather_detail_is_open(void)
{
    return s_screen != NULL;
}

void ui_weather_detail_open(void)
{
    if (s_screen) return;
    s_return_screen = lv_screen_active();

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Snapshot der Daten unter Lock, dann ausserhalb des Locks rendern.
    int    code = -1;
    double temp = NAN, apparent = NAN, wind = NAN;
    int    wind_dir = -1, humidity = -1;
    int    sunrise_min = -1, sunset_min = -1;
    char   location[48];
    char   current_temp_buf[16];
    weather_daily_slot_t  daily[WEATHER_DAILY_COUNT];
    int    daily_count = 0;

    app_lock();
    code     = g_app.weather_code;
    apparent = g_app.weather_apparent_temp;
    wind     = g_app.weather_wind_speed;
    wind_dir = g_app.weather_wind_dir;
    humidity = g_app.weather_humidity;
    strncpy(location, g_app.weather_location, sizeof(location));
    location[sizeof(location) - 1] = 0;
    strncpy(current_temp_buf, g_app.current_temp, sizeof(current_temp_buf));
    current_temp_buf[sizeof(current_temp_buf) - 1] = 0;
    memcpy(daily, g_app.weather_daily, sizeof(daily));
    daily_count = g_app.weather_daily_count;
    if (daily_count > 0) {
        sunrise_min = daily[0].sunrise_min;
        sunset_min  = daily[0].sunset_min;
    }
    app_unlock();

    // current_temp ist als String "%.1f" gespeichert (damit Time-Screen
    // nichts formatieren muss). Zurueck zur Zahl fuer den Detail-Header.
    temp = strtod(current_temp_buf, NULL);
    if (current_temp_buf[0] == '-' && current_temp_buf[1] == '-') temp = NAN;

    // Header: Accent + Titel + Standort + Back-Button.
    lv_obj_t *accent = lv_obj_create(s_screen);
    style_filled_rect(accent, COLOR_CYAN, 2);
    lv_obj_set_size(accent, 36, 5);
    lv_obj_set_pos(accent, 42, 58);

    make_label(s_screen, &lv_font_montserrat_40, COLOR_TEXT, LV_TEXT_ALIGN_LEFT,
               100, 26, 380, 52, T(WEATHER_TITLE));

    if (location[0]) {
        make_label(s_screen, &lv_font_montserrat_24, COLOR_DIM, LV_TEXT_ALIGN_RIGHT,
                   460, 36, 252, 30, location);
    }

    lv_obj_t *back = lv_obj_create(s_screen);
    style_filled_rect(back, 0x232b38, 8);
    lv_obj_set_size(back, 52, 52);
    lv_obj_set_pos(back, LCD_H_RES - 52 - 24, 26);
    lv_obj_set_style_border_width(back, 2, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(COLOR_DIM), 0);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = make_label(back, &lv_font_montserrat_24, COLOR_TEXT,
                                      LV_TEXT_ALIGN_CENTER, 0, 0, 52, 52, LV_SYMBOL_LEFT);
    lv_obj_set_size(back_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(back_label);
    lv_obj_add_flag(back_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Kachelraster.
    build_tile_icon(code);
    build_tile_temp(temp);
    build_tile_apparent(apparent);
    build_tile_wind(wind, wind_dir);
    build_tile_humidity(humidity);
    build_tile_sun(sunrise_min, sunset_min);

    // 5-Tage-Strip.
    build_daily(daily, daily_count);

    ESP_LOGI(TAG, "Detail-Screen geoeffnet (daily=%d)", daily_count);
    lv_screen_load(s_screen);
}
