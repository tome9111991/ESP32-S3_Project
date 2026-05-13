// Display-Settings-Screen: Slider fuer Tag-Helligkeit mit Live-Preview.
// Pendant zu 14_DisplaySettings.ino (reduziert auf Helligkeit; Rotate ist im
// Port aktuell Compile-Time-Konstante).

#include "app_state.h"

#include <stdio.h>

#define SLIDER_X       100
#define SLIDER_Y       210
#define SLIDER_W       600
#define SLIDER_H       24
#define VALUE_LABEL_Y  144

static lv_obj_t *s_screen           = NULL;
static lv_obj_t *s_slider           = NULL;
static lv_obj_t *s_value_label      = NULL;
static lv_obj_t *s_return_screen    = NULL;
static bool      s_from_settings_menu = false;
static lv_obj_t *s_rotate_switch    = NULL;
static lv_obj_t *s_rotate_hint      = NULL;
static uint8_t   s_saved_brightness = DAY_BRIGHTNESS_DEFAULT;
static bool      s_saved_rotate_180 = false;
static bool      s_draft_rotate_180 = false;

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

static void update_value_label(int v)
{
    if (!s_value_label) return;
    char buf[24];
    snprintf(buf, sizeof(buf), "%d / %d", v, DAY_BRIGHTNESS_MAX);
    lv_label_set_text(s_value_label, buf);
}

static void on_slider_changed(lv_event_t *e)
{
    (void)e;
    if (!s_slider) return;
    int32_t v = lv_slider_get_value(s_slider);
    if (v < DAY_BRIGHTNESS_MIN) v = DAY_BRIGHTNESS_MIN;
    if (v > DAY_BRIGHTNESS_MAX) v = DAY_BRIGHTNESS_MAX;
    display_brightness_set_day((uint8_t)v);
    update_value_label((int)v);
}

static void close_screen(void)
{
    if (!s_screen) return;

    const uint8_t current = display_brightness_get_day();
    const bool brightness_changed = current != s_saved_brightness;
    const bool rotate_changed     = s_draft_rotate_180 != s_saved_rotate_180;

    if (brightness_changed) {
        display_brightness_commit_day();
    }
    if (rotate_changed) {
        display_rotate_180_save(s_draft_rotate_180);
    }

    // Naechste Sonnenstand-Auswertung sofort, damit Nacht-Modus nach
    // Live-Preview wieder korrekt einsetzt.
    display_brightness_request_sun_refresh();

    if (rotate_changed) {
        // Rotation aendert LVGL-Buffer-Layout + Touch-Map; sauberer Neustart.
        // Settings-Screen gleich abraeumen, damit kein lebendes Widget mehr
        // beim Reboot-Delay angesprochen wird.
        lv_obj_delete_async(s_screen);
        s_screen        = NULL;
        s_slider        = NULL;
        s_value_label   = NULL;
        s_rotate_switch = NULL;
        s_rotate_hint   = NULL;
        s_return_screen = NULL;
        ui_perform_clean_reboot();
        return;
    }

    const bool back_to_menu = s_from_settings_menu;

    // Erst Zielscreen laden, dann den aktiven Settings-Screen loeschen.
    if (back_to_menu) {
        ui_settings_menu_reopen();
    } else if (s_return_screen) {
        lv_screen_load(s_return_screen);
    }
    lv_obj_delete_async(s_screen);
    s_screen        = NULL;
    s_slider        = NULL;
    s_value_label   = NULL;
    s_rotate_switch = NULL;
    s_rotate_hint   = NULL;
    s_return_screen = NULL;
    s_from_settings_menu = false;
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    close_screen();
}

static void update_rotate_visuals(void)
{
    if (s_rotate_switch) {
        // Switch-State folgt dem Entwurf; gespeichert wird erst beim Zurueckgehen.
        if (s_draft_rotate_180) {
            lv_obj_add_state(s_rotate_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_rotate_switch, LV_STATE_CHECKED);
        }
    }
    if (s_rotate_hint) {
        const bool changed = s_draft_rotate_180 != s_saved_rotate_180;
        lv_obj_set_style_text_color(s_rotate_hint,
            lv_color_hex(changed ? COLOR_ORANGE : COLOR_MUTED), 0);
        lv_label_set_text(s_rotate_hint,
            changed ? "Neustart beim Zurueckgehen"
                    : "Touch-Kalibrierung bleibt");
    }
}

static void on_rotate_row_clicked(lv_event_t *e)
{
    (void)e;
    s_draft_rotate_180 = !s_draft_rotate_180;
    update_rotate_visuals();
}

static void on_rotate_switch_changed(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    s_draft_rotate_180 = lv_obj_has_state(sw, LV_STATE_CHECKED);
    update_rotate_visuals();
}

bool ui_display_settings_is_open(void)
{
    return s_screen != NULL;
}

void ui_display_settings_open(void)
{
    if (s_screen) return;

    // Beim Aufruf aus dem Settings-Menue merken wir das urspruengliche
    // Dashboard-Return-Ziel und gehen per Back zuerst ins Menue zurueck.
    lv_obj_t *menu_return = ui_settings_menu_return_target();
    if (menu_return) {
        s_return_screen      = menu_return;
        s_from_settings_menu = true;
    } else {
        s_return_screen      = lv_screen_active();
        s_from_settings_menu = false;
    }
    s_saved_brightness = display_brightness_get_day();
    s_saved_rotate_180 = display_rotate_180_load();
    s_draft_rotate_180 = s_saved_rotate_180;

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
               100, 32, 500, 52, "Display");

    // Back-Button (oben rechts)
    lv_obj_t *back = lv_obj_create(s_screen);
    style_filled_rect(back, 0x232b38, 8);
    lv_obj_set_size(back, 52, 52);
    lv_obj_set_pos(back, LCD_H_RES - 52 - 24, 30);
    lv_obj_set_style_border_width(back, 2, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(COLOR_DIM), 0);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = make_label(back, &lv_font_montserrat_24, COLOR_TEXT,
                                      LV_TEXT_ALIGN_CENTER, 0, 0, 52, 52,
                                      LV_SYMBOL_LEFT);
    lv_obj_set_size(back_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(back_label);
    lv_obj_add_flag(back_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Section-Header + aktueller Wert
    make_label(s_screen, &lv_font_montserrat_30, COLOR_TEXT, LV_TEXT_ALIGN_LEFT,
               100, 140, 360, 36, "Helligkeit (Tag)");

    s_value_label = make_label(s_screen, &lv_font_montserrat_24, COLOR_MUTED,
                               LV_TEXT_ALIGN_RIGHT, 460, VALUE_LABEL_Y, 240, 32,
                               "");
    update_value_label(s_saved_brightness);

    // Slider
    s_slider = lv_slider_create(s_screen);
    lv_obj_set_size(s_slider, SLIDER_W, SLIDER_H);
    lv_obj_set_pos(s_slider, SLIDER_X, SLIDER_Y);
    lv_slider_set_range(s_slider, DAY_BRIGHTNESS_MIN, DAY_BRIGHTNESS_MAX);
    lv_slider_set_value(s_slider, s_saved_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider, lv_color_hex(0x232b38), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider, lv_color_hex(COLOR_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider, lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_slider, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(s_slider, on_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);

    // Rotate-180 Toggle-Row (klickbarer Container mit LVGL-Switch)
    lv_obj_t *row = lv_obj_create(s_screen);
    style_filled_rect(row, 0x151b24, 8);
    lv_obj_set_size(row, 600, 96);
    lv_obj_set_pos(row, 100, 270);
    lv_obj_set_style_border_width(row, 2, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, on_rotate_row_clicked, LV_EVENT_CLICKED, NULL);

    s_rotate_switch = lv_switch_create(row);
    lv_obj_set_size(s_rotate_switch, 84, 44);
    lv_obj_set_pos(s_rotate_switch, 24, 26);
    lv_obj_set_style_bg_color(s_rotate_switch, lv_color_hex(0x232b38), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_rotate_switch, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_rotate_switch, lv_color_hex(COLOR_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_rotate_switch, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_rotate_switch, lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_rotate_switch, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_add_event_cb(s_rotate_switch, on_rotate_switch_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *title = make_label(row, &lv_font_montserrat_30, COLOR_TEXT,
                                 LV_TEXT_ALIGN_LEFT, 128, 14, 460, 36,
                                 "Display 180 Grad drehen");
    lv_obj_add_flag(title, LV_OBJ_FLAG_EVENT_BUBBLE);

    s_rotate_hint = make_label(row, &lv_font_montserrat_24, COLOR_MUTED,
                               LV_TEXT_ALIGN_LEFT, 128, 54, 460, 32, "");
    lv_obj_add_flag(s_rotate_hint, LV_OBJ_FLAG_EVENT_BUBBLE);

    update_rotate_visuals();

    lv_screen_load(s_screen);
}
