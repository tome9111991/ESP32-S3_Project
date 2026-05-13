// Location-Settings: zwei Textfelder fuer Breiten- und Laengengrad.
// Wird ueber die ui_keyboard-Tastatur befuellt; Save schreibt nach NVS via
// location_settings_save() und triggert beim Naechsten Weather-Fetch
// implizit eine neue Reverse-Geocoding-Aufloesung.

#include "app_state.h"
#include "ui_keyboard.h"

#include "esp_log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "loc_ui";

#define LAT_MAX_LEN  12
#define LON_MAX_LEN  12

#define FORM_LEFT   40
#define FIELD_X     220
#define FIELD_W     400
#define FIELD_H     50

static lv_obj_t *s_screen        = NULL;
static lv_obj_t *s_lat_ta        = NULL;
static lv_obj_t *s_lon_ta        = NULL;
static lv_obj_t *s_status_lbl    = NULL;
static lv_obj_t *s_return_screen = NULL;
static bool      s_from_settings_menu = false;

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
    lv_label_set_text(label, text);
    return label;
}

static void set_status(const char *text, uint32_t color)
{
    if (!s_status_lbl) return;
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(color), 0);
    lv_label_set_text(s_status_lbl, text);
}

// Akzeptiert "52.520007", "-12.34", "13", "13,5" (Komma wird zu Punkt). Lehnt
// leere Eingaben ab und stellt sicher dass die Zahl im Bereich liegt.
static bool parse_coord(const char *text, float *out, float min_v, float max_v)
{
    if (!text || !text[0]) return false;
    char buf[24];
    size_t n = 0;
    for (const char *p = text; *p && n + 1 < sizeof(buf); p++) {
        char c = (*p == ',') ? '.' : *p;
        buf[n++] = c;
    }
    buf[n] = '\0';
    char *end = NULL;
    float v = strtof(buf, &end);
    if (!end || end == buf) return false;
    // Trailing whitespace tolerieren.
    while (*end) {
        if (!isspace((unsigned char)*end)) return false;
        end++;
    }
    if (v < min_v || v > max_v) return false;
    *out = v;
    return true;
}

static void on_textarea_event(lv_event_t *e)
{
    // Nur CLICKED triggert den Keyboard-Screen; FOCUSED feuert auch beim
    // Re-Load und wuerde sonst sofort wieder oeffnen.
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *ta = lv_event_get_target_obj(e);
    if (ta == s_lat_ta) {
        ui_keyboard_open(ta, "Breitengrad (-90 bis 90)", false, LAT_MAX_LEN);
    } else if (ta == s_lon_ta) {
        ui_keyboard_open(ta, "Laengengrad (-180 bis 180)", false, LON_MAX_LEN);
    }
}

static void destroy_screen_objects(void)
{
    if (!s_screen) return;
    lv_obj_delete_async(s_screen);
    s_screen      = NULL;
    s_lat_ta      = NULL;
    s_lon_ta      = NULL;
    s_status_lbl  = NULL;
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    const bool back_to_menu = s_from_settings_menu;
    lv_obj_t *ret = s_return_screen;
    s_return_screen      = NULL;
    s_from_settings_menu = false;

    if (back_to_menu) {
        ui_settings_menu_reopen();
    } else if (ret) {
        lv_screen_load(ret);
    }
    destroy_screen_objects();
}

static void on_save_clicked(lv_event_t *e)
{
    (void)e;
    if (!s_lat_ta || !s_lon_ta) return;

    float lat = 0.0f, lon = 0.0f;
    if (!parse_coord(lv_textarea_get_text(s_lat_ta), &lat, -90.0f, 90.0f)) {
        set_status("Breitengrad ungueltig (-90 bis 90)", COLOR_LOSS);
        return;
    }
    if (!parse_coord(lv_textarea_get_text(s_lon_ta), &lon, -180.0f, 180.0f)) {
        set_status("Laengengrad ungueltig (-180 bis 180)", COLOR_LOSS);
        return;
    }

    if (!location_settings_save(lat, lon)) {
        set_status("Speichern fehlgeschlagen", COLOR_LOSS);
        return;
    }

    ESP_LOGI(TAG, "Location gespeichert: %.6f, %.6f", (double)lat, (double)lon);
    set_status("Gespeichert", COLOR_GREEN);
}

static lv_obj_t *create_textarea(lv_obj_t *parent, int x, int y, const char *placeholder)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, FIELD_W, FIELD_H);
    lv_obj_set_pos(ta, x, y);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x151b24), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(COLOR_CYAN), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(ta, 6, 0);
    lv_obj_set_style_pad_left(ta, 18, 0);
    lv_obj_set_style_pad_right(ta, 18, 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(COLOR_TEXT), 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_max_length(ta, LON_MAX_LEN);
    lv_textarea_set_password_mode(ta, false);
    return ta;
}

bool ui_location_settings_is_open(void)
{
    return s_screen != NULL;
}

void ui_location_settings_open(void)
{
    if (s_screen) return;

    lv_obj_t *menu_return = ui_settings_menu_return_target();
    if (menu_return) {
        s_return_screen      = menu_return;
        s_from_settings_menu = true;
    } else {
        s_return_screen      = lv_screen_active();
        s_from_settings_menu = false;
    }

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Accent + Titel
    lv_obj_t *accent = lv_obj_create(s_screen);
    style_filled_rect(accent, COLOR_SETTINGS, 2);
    lv_obj_set_size(accent, 36, 5);
    lv_obj_set_pos(accent, 42, 58);

    make_label(s_screen, &lv_font_montserrat_40, COLOR_TEXT, LV_TEXT_ALIGN_LEFT,
               100, 32, 500, 52, "Standort");

    // Back-Button oben rechts
    lv_obj_t *back = lv_obj_create(s_screen);
    style_filled_rect(back, 0x232b38, 8);
    lv_obj_set_size(back, 52, 52);
    lv_obj_set_pos(back, LCD_H_RES - 52 - 24, 30);
    lv_obj_set_style_border_width(back, 2, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(COLOR_DIM), 0);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = make_label(back, &lv_font_montserrat_24, COLOR_TEXT,
                                      LV_TEXT_ALIGN_CENTER, 0, 0, 52, 52, LV_SYMBOL_LEFT);
    lv_obj_set_size(back_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(back_label);
    lv_obj_add_flag(back_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Felder
    make_label(s_screen, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_LEFT,
               FORM_LEFT, 138, 180, 30, "Breitengrad");
    make_label(s_screen, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_LEFT,
               FORM_LEFT, 218, 180, 30, "Laengengrad");

    s_lat_ta = create_textarea(s_screen, FIELD_X, 128, "z.B. 52.520007");
    s_lon_ta = create_textarea(s_screen, FIELD_X, 208, "z.B. 13.404954");
    lv_obj_add_event_cb(s_lat_ta, on_textarea_event, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(s_lon_ta, on_textarea_event, LV_EVENT_ALL, NULL);

    // Hilfetext + Status
    make_label(s_screen, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_LEFT,
               FORM_LEFT, 290, 720, 32,
               "Dezimalgrad (z.B. maps.google.com -> Rechtsklick)");
    s_status_lbl = make_label(s_screen, &lv_font_montserrat_24, COLOR_MUTED,
                              LV_TEXT_ALIGN_LEFT, FORM_LEFT, 326, 720, 32, "");

    // Save-Button
    lv_obj_t *save = lv_obj_create(s_screen);
    style_filled_rect(save, COLOR_CYAN, 10);
    lv_obj_set_size(save, 360, 64);
    lv_obj_set_pos(save, (LCD_H_RES - 360) / 2, 380);
    lv_obj_add_flag(save, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(save, on_save_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_lbl = make_label(save, &lv_font_montserrat_30, COLOR_BG,
                                    LV_TEXT_ALIGN_CENTER, 0, 14, 360, 50, "Speichern");
    lv_obj_add_flag(save_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Vorbelegen mit aktuellem Wert.
    float lat_f = 0.0f, lon_f = 0.0f;
    location_snapshot(&lat_f, &lon_f);
    char buf[24];
    snprintf(buf, sizeof(buf), "%.6f", (double)lat_f);
    lv_textarea_set_text(s_lat_ta, buf);
    snprintf(buf, sizeof(buf), "%.6f", (double)lon_f);
    lv_textarea_set_text(s_lon_ta, buf);

    lv_screen_load(s_screen);
}
