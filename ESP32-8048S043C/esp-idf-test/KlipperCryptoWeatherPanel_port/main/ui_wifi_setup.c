// WLAN-Setup mit Fullscreen-Keyboard.
// Das WLAN-Formular zeigt SSID + Passwort als Textfelder (read-only Anzeige).
// Tippt der Nutzer auf ein Feld, oeffnet sich ein eigener Screen, auf dem die
// LVGL-Tastatur den ganzen unteren Bildschirm fuellt - dadurch kein Padding-
// Clipping wie beim Overlay-Versuch. Fertig-Button schreibt den Wert zurueck.

#include "app_state.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "wifi_ui";

#define WIFI_SSID_MAX_LEN     32
#define WIFI_PASSWORD_MAX_LEN 64

#define FORM_LEFT             40
#define FIELD_X               190
#define FIELD_W               560
#define FIELD_H               50

#define KB_X                  0
#define KB_Y                  120
#define KB_H                  360

static lv_obj_t *s_screen           = NULL;
static lv_obj_t *s_ssid_ta          = NULL;
static lv_obj_t *s_pass_ta          = NULL;
static lv_obj_t *s_pass_toggle_lbl  = NULL;
static lv_obj_t *s_status_lbl       = NULL;
static lv_obj_t *s_return_screen    = NULL;
static bool      s_from_settings_menu = false;

// Fullscreen-Keyboard-Screen-State
static lv_obj_t *s_kb_screen     = NULL;
static lv_obj_t *s_kb_screen_ta  = NULL;
static lv_obj_t *s_kb_widget     = NULL;
static lv_obj_t *s_kb_target_ta  = NULL;
static bool      s_kb_target_is_password = false;

#define KB_CHAR(width) (LV_BUTTONMATRIX_CTRL_POPOVER | (width))
#define KB_CTRL(width) (LV_KEYBOARD_CTRL_BUTTON_FLAGS | (width))

static const char * const wifi_kb_map_lc[] = {
    "1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_OK, "\n",
    "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
    // Unten rechts verwirft X die aktuelle Eingabe.
    LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_CLOSE, ""
};

static const lv_buttonmatrix_ctrl_t wifi_kb_ctrl_lc[] = {
    KB_CTRL(5), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CTRL(7),
    KB_CTRL(6), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CTRL(7),
    KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1),
    KB_CTRL(2), 10, KB_CTRL(2)
};

static const char * const wifi_kb_map_uc[] = {
    "1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_OK, "\n",
    "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
    // Unten rechts verwirft X die aktuelle Eingabe.
    LV_SYMBOL_CLOSE, " ", LV_SYMBOL_CLOSE, ""
};

static const lv_buttonmatrix_ctrl_t wifi_kb_ctrl_uc[] = {
    KB_CTRL(5), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CHAR(4), KB_CTRL(7),
    KB_CTRL(6), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CHAR(3), KB_CTRL(7),
    KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1),
    KB_CTRL(2), 10, KB_CTRL(2)
};

static const char * const wifi_kb_map_spec[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
    "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
    // Unten rechts verwirft X die aktuelle Eingabe.
    LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_CLOSE, ""
};

static const lv_buttonmatrix_ctrl_t wifi_kb_ctrl_spec[] = {
    KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CTRL(2),
    KB_CTRL(2), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1),
    KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1), KB_CHAR(1),
    KB_CTRL(2), 10, KB_CTRL(2)
};

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

// --- Fullscreen-Keyboard-Screen ---------------------------------------------

static void close_keyboard_screen(bool commit)
{
    if (!s_kb_screen) return;

    if (commit && s_kb_target_ta && s_kb_screen_ta) {
        const char *txt = lv_textarea_get_text(s_kb_screen_ta);
        lv_textarea_set_text(s_kb_target_ta, txt ? txt : "");
    }

    // Zurueck zum WLAN-Formular und Keyboard-Screen verwerfen.
    if (s_screen) {
        lv_screen_load(s_screen);
    }
    lv_obj_t *to_delete = s_kb_screen;
    s_kb_screen    = NULL;
    s_kb_screen_ta = NULL;
    s_kb_widget    = NULL;
    s_kb_target_ta = NULL;
    s_kb_target_is_password = false;
    lv_obj_delete_async(to_delete);
}

static void on_kb_done_clicked(lv_event_t *e)
{
    (void)e;
    close_keyboard_screen(true);
}

static void on_kb_widget_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        close_keyboard_screen(true);
    } else if (code == LV_EVENT_CANCEL) {
        close_keyboard_screen(false);
    }
}

static void open_keyboard_screen(lv_obj_t *target_ta, const char *title, bool password)
{
    if (s_kb_screen || !target_ta) return;

    s_kb_target_ta          = target_ta;
    s_kb_target_is_password = password;

    s_kb_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_kb_screen);
    lv_obj_set_style_bg_color(s_kb_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_kb_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_kb_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Kompakter Header: Titel + Fertig-Button auf einer Zeile.
    make_label(s_kb_screen, &lv_font_montserrat_24, COLOR_TEXT, LV_TEXT_ALIGN_LEFT,
               20, 12, 500, 32, title);

    lv_obj_t *done = lv_obj_create(s_kb_screen);
    style_filled_rect(done, COLOR_CYAN, 8);
    lv_obj_set_size(done, 130, 36);
    lv_obj_set_pos(done, LCD_H_RES - 130 - 16, 10);
    lv_obj_add_flag(done, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(done, on_kb_done_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *done_lbl = make_label(done, &lv_font_montserrat_24, COLOR_BG,
                                    LV_TEXT_ALIGN_CENTER, 0, 4, 130, 32, "Fertig");
    lv_obj_add_flag(done_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Live-Anzeige direkt darunter, schmaler Streifen.
    s_kb_screen_ta = lv_textarea_create(s_kb_screen);
    lv_obj_set_size(s_kb_screen_ta, LCD_H_RES - 40, 42);
    lv_obj_set_pos(s_kb_screen_ta, 20, 52);
    lv_obj_set_style_bg_color(s_kb_screen_ta, lv_color_hex(0x151b24), 0);
    lv_obj_set_style_bg_opa(s_kb_screen_ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_kb_screen_ta, 2, 0);
    lv_obj_set_style_border_color(s_kb_screen_ta, lv_color_hex(COLOR_CYAN), 0);
    lv_obj_set_style_radius(s_kb_screen_ta, 6, 0);
    lv_obj_set_style_pad_left(s_kb_screen_ta, 14, 0);
    lv_obj_set_style_pad_right(s_kb_screen_ta, 14, 0);
    lv_obj_set_style_text_font(s_kb_screen_ta, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_kb_screen_ta, lv_color_hex(COLOR_TEXT), 0);
    lv_textarea_set_one_line(s_kb_screen_ta, true);
    lv_textarea_set_max_length(s_kb_screen_ta,
        password ? WIFI_PASSWORD_MAX_LEN : WIFI_SSID_MAX_LEN);
    lv_textarea_set_password_mode(s_kb_screen_ta, false);
    lv_textarea_set_text(s_kb_screen_ta, lv_textarea_get_text(target_ta));
    lv_obj_add_state(s_kb_screen_ta, LV_STATE_FOCUSED);

    // Tastatur direkt unter dem Eingabefeld halten und unten Reserve lassen.
    // Auf dem 480px-Panel klebte die alte Vollhoehe am Rand und wurde optisch
    // abgeschnitten.
    s_kb_widget = lv_keyboard_create(s_kb_screen);
    lv_obj_remove_style_all(s_kb_widget);
    lv_obj_set_size(s_kb_widget, LCD_H_RES - (KB_X * 2), KB_H);
    lv_obj_align(s_kb_widget, LV_ALIGN_TOP_LEFT, KB_X, KB_Y);
    lv_keyboard_set_map(s_kb_widget, LV_KEYBOARD_MODE_TEXT_LOWER,
                        wifi_kb_map_lc, wifi_kb_ctrl_lc);
    lv_keyboard_set_map(s_kb_widget, LV_KEYBOARD_MODE_TEXT_UPPER,
                        wifi_kb_map_uc, wifi_kb_ctrl_uc);
    lv_keyboard_set_map(s_kb_widget, LV_KEYBOARD_MODE_SPECIAL,
                        wifi_kb_map_spec, wifi_kb_ctrl_spec);
    lv_keyboard_set_mode(s_kb_widget, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_popovers(s_kb_widget, false);
    lv_obj_set_style_bg_color(s_kb_widget, lv_color_hex(0x101720), 0);
    lv_obj_set_style_bg_opa(s_kb_widget, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_kb_widget, 0, 0);
    lv_obj_set_style_pad_top(s_kb_widget, 0, 0);
    lv_obj_set_style_pad_bottom(s_kb_widget, 0, 0);
    lv_obj_set_style_pad_left(s_kb_widget, 2, 0);
    lv_obj_set_style_pad_right(s_kb_widget, 2, 0);
    lv_obj_set_style_pad_row(s_kb_widget, 1, 0);
    lv_obj_set_style_pad_column(s_kb_widget, 2, 0);
    lv_obj_remove_flag(s_kb_widget, LV_OBJ_FLAG_SCROLLABLE);
    // Grosse Tastenbeschriftung fuer die Fullscreen-WLAN-Tastatur.
    lv_obj_set_style_text_font(s_kb_widget, &lv_font_montserrat_30, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_kb_widget, lv_color_hex(0x18202b), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_kb_widget, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_kb_widget, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_kb_widget, lv_color_hex(COLOR_DIM), LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_kb_widget, lv_color_hex(COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_opa(s_kb_widget, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_kb_widget, 4, LV_PART_ITEMS);
    lv_obj_set_style_pad_top(s_kb_widget, 0, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(s_kb_widget, 0, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(s_kb_widget, 1, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(s_kb_widget, 1, LV_PART_ITEMS);
    lv_keyboard_set_textarea(s_kb_widget, s_kb_screen_ta);
    lv_obj_add_event_cb(s_kb_widget, on_kb_widget_event, LV_EVENT_ALL, NULL);

    lv_screen_load(s_kb_screen);
}

// --- WLAN-Formular -----------------------------------------------------------

static void on_textarea_event(lv_event_t *e)
{
    // Nur CLICKED triggert den Keyboard-Screen; FOCUSED feuert auch beim
    // Re-Loaden des Form-Screens und wuerde sonst sofort wieder oeffnen.
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *ta = lv_event_get_target_obj(e);
    if (ta == s_ssid_ta) {
        open_keyboard_screen(ta, "SSID eingeben", false);
    } else if (ta == s_pass_ta) {
        open_keyboard_screen(ta, "Passwort eingeben", true);
    }
}

static void on_pw_toggle_clicked(lv_event_t *e)
{
    (void)e;
    if (!s_pass_ta) return;
    bool hidden = lv_textarea_get_password_mode(s_pass_ta);
    lv_textarea_set_password_mode(s_pass_ta, !hidden);
    if (s_pass_toggle_lbl) {
        lv_label_set_text(s_pass_toggle_lbl, hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    }
}

static void destroy_screen_objects(void)
{
    if (s_kb_screen) {
        lv_obj_t *to_del = s_kb_screen;
        s_kb_screen    = NULL;
        s_kb_screen_ta = NULL;
        s_kb_widget    = NULL;
        s_kb_target_ta = NULL;
        lv_obj_delete_async(to_del);
    }
    if (!s_screen) return;
    lv_obj_delete_async(s_screen);
    s_screen          = NULL;
    s_ssid_ta         = NULL;
    s_pass_ta         = NULL;
    s_pass_toggle_lbl = NULL;
    s_status_lbl      = NULL;
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    const bool back_to_menu = s_from_settings_menu;
    lv_obj_t *ret = s_return_screen;
    s_return_screen      = NULL;
    s_from_settings_menu = false;

    // Erst Zielscreen laden, dann den aktiven WLAN-Screen loeschen.
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
    if (!s_ssid_ta || !s_pass_ta) return;
    const char *ssid = lv_textarea_get_text(s_ssid_ta);
    const char *pass = lv_textarea_get_text(s_pass_ta);

    bool has_ssid = false;
    for (const char *p = ssid; p && *p; p++) {
        if (*p != ' ' && *p != '\t') { has_ssid = true; break; }
    }
    if (!has_ssid) {
        set_status("SSID fehlt", COLOR_LOSS);
        return;
    }

    if (!wifi_credentials_save(ssid, pass ? pass : "")) {
        set_status("Speichern fehlgeschlagen", COLOR_LOSS);
        return;
    }

    set_status("Gespeichert, Neustart...", COLOR_CYAN);
    ESP_LOGI(TAG, "WLAN-Daten gespeichert, Reboot");
    ui_perform_clean_reboot();
}

static lv_obj_t *create_textarea(lv_obj_t *parent, int x, int y,
                                 const char *placeholder, bool pwd)
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
    lv_textarea_set_max_length(ta, pwd ? WIFI_PASSWORD_MAX_LEN : WIFI_SSID_MAX_LEN);
    lv_textarea_set_password_mode(ta, pwd);
    return ta;
}

// --- Public API --------------------------------------------------------------

bool ui_wifi_setup_is_open(void)
{
    return s_screen != NULL;
}

void ui_wifi_setup_open(void)
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
               100, 32, 400, 52, "WLAN");

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
               FORM_LEFT, 138, 140, 30, "SSID");
    make_label(s_screen, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_LEFT,
               FORM_LEFT, 218, 140, 30, "Passwort");

    s_ssid_ta = create_textarea(s_screen, FIELD_X, 128, "WLAN-Name", false);
    s_pass_ta = create_textarea(s_screen, FIELD_X, 208, "WLAN-Passwort", true);
    lv_obj_set_style_pad_right(s_pass_ta, 56, 0);
    lv_obj_add_event_cb(s_ssid_ta, on_textarea_event, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(s_pass_ta, on_textarea_event, LV_EVENT_ALL, NULL);

    // Auge / Passwort sichtbar machen
    lv_obj_t *eye = lv_obj_create(s_screen);
    lv_obj_remove_style_all(eye);
    lv_obj_set_size(eye, 50, FIELD_H);
    lv_obj_set_pos(eye, FIELD_X + FIELD_W - 50, 208);
    lv_obj_set_style_bg_opa(eye, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(eye, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(eye, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(eye, on_pw_toggle_clicked, LV_EVENT_CLICKED, NULL);
    s_pass_toggle_lbl = make_label(eye, &lv_font_montserrat_30, COLOR_MUTED,
                                   LV_TEXT_ALIGN_CENTER, 0, 10, 50, 40,
                                   LV_SYMBOL_EYE_OPEN);
    lv_obj_add_flag(s_pass_toggle_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Status
    s_status_lbl = make_label(s_screen, &lv_font_montserrat_24, COLOR_MUTED,
                              LV_TEXT_ALIGN_LEFT, FORM_LEFT, 290, 700, 32,
                              "Tippe auf ein Feld, um es zu bearbeiten");

    // Save-Button unten breit. Hauptaktion -> bewusst gross + unten.
    lv_obj_t *save = lv_obj_create(s_screen);
    style_filled_rect(save, COLOR_CYAN, 10);
    lv_obj_set_size(save, 360, 64);
    lv_obj_set_pos(save, (LCD_H_RES - 360) / 2, 380);
    lv_obj_add_flag(save, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(save, on_save_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_lbl = make_label(save, &lv_font_montserrat_30, COLOR_BG,
                                    LV_TEXT_ALIGN_CENTER, 0, 14, 360, 50, "Speichern");
    lv_obj_add_flag(save_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Vorbelegen mit aktuellen Credentials.
    char ssid_buf[33] = {0};
    char pass_buf[65] = {0};
    bool have_wifi_config = wifi_credentials_load(ssid_buf, sizeof(ssid_buf),
                                                  pass_buf, sizeof(pass_buf));
    lv_textarea_set_text(s_ssid_ta, ssid_buf);
    lv_textarea_set_text(s_pass_ta, pass_buf);
    if (!have_wifi_config) {
        // Boot-Onboarding: Nutzer landet hier automatisch, wenn WLAN fehlt.
        set_status("Keine WLAN-Daten gespeichert", COLOR_LOSS);
    }

    lv_screen_load(s_screen);
}
