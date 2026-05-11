// Settings-Menue Port (12_SettingsMenu.ino). Liste mit 5 Eintraegen:
// WLAN, Screens, Crypto, Display, Touch kalibrieren. Nur "Display" ist im
// Port aktuell funktional verdrahtet (ui_display_settings), die uebrigen
// oeffnen einen Dummy-Sub-Screen mit "Coming soon" + Back-Button.

#include "app_state.h"

#include <stdio.h>
#include <string.h>

// --- Layout (1:1 vom Arduino-Sketch uebernommen) -----------------------------
#define MENU_LIST_Y         112
#define MENU_ITEM_X         100
#define MENU_ITEM_W         600
#define MENU_ITEM_H         64
#define MENU_ITEM_GAP       8
#define MENU_ITEM_START_Y   8

static lv_obj_t *s_menu_screen      = NULL;
static lv_obj_t *s_dummy_screen     = NULL;
static lv_obj_t *s_return_screen    = NULL;  // Dashboard-Screen, zu dem Back fuehrt

// Forward decls
static void open_settings_menu_screen(bool fresh);
static void on_back_to_dashboard(lv_event_t *e);
static void on_dummy_back(lv_event_t *e);
static void on_item_wifi_clicked(lv_event_t *e);
static void on_item_screens_clicked(lv_event_t *e);
static void on_item_crypto_clicked(lv_event_t *e);
static void on_item_display_clicked(lv_event_t *e);
static void on_item_touch_clicked(lv_event_t *e);

// --- kleine Helfer (lokale Kopien aus ui_popup.c-Stil) ----------------------
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

static lv_obj_t *make_back_button(lv_obj_t *parent, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_obj_create(parent);
    style_filled_rect(btn, 0x232b38, 8);
    lv_obj_set_size(btn, 52, 52);
    lv_obj_set_pos(btn, LCD_H_RES - 52 - 24, 30);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = make_label(btn, &lv_font_montserrat_24, COLOR_TEXT,
                                 LV_TEXT_ALIGN_CENTER, 0, 0, 52, 52, LV_SYMBOL_LEFT);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(label);
    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
    return btn;
}

static lv_obj_t *make_accent(lv_obj_t *parent, uint32_t color)
{
    lv_obj_t *accent = lv_obj_create(parent);
    style_filled_rect(accent, color, 2);
    lv_obj_set_size(accent, 36, 5);
    lv_obj_set_pos(accent, 42, 58);
    return accent;
}

// --- Menue-Eintrag -----------------------------------------------------------
static void make_menu_item(lv_obj_t *parent, int y, uint32_t bg_color,
                           uint32_t label_color, const char *title,
                           const char *subtitle, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_obj_create(parent);
    style_filled_rect(btn, bg_color, 10);
    lv_obj_set_size(btn, MENU_ITEM_W, MENU_ITEM_H);
    lv_obj_set_pos(btn, MENU_ITEM_X, y);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title_label = make_label(btn, &lv_font_montserrat_28, label_color,
                                       LV_TEXT_ALIGN_LEFT, 30, 5, 560, 32, title);
    lv_obj_add_flag(title_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    if (subtitle && subtitle[0]) {
        lv_obj_t *sub = make_label(btn, &lv_font_montserrat_24, label_color,
                                   LV_TEXT_ALIGN_LEFT, 30, 34, 560, 28, subtitle);
        lv_obj_set_style_text_opa(sub, LV_OPA_70, 0);
        lv_obj_add_flag(sub, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
}

// --- Dummy-Sub-Screen --------------------------------------------------------
static void destroy_dummy_screen(void)
{
    if (!s_dummy_screen) return;
    lv_obj_delete_async(s_dummy_screen);
    s_dummy_screen = NULL;
}

static void on_dummy_back(lv_event_t *e)
{
    (void)e;
    // Zurueck ins Menue (frischer Aufbau, da das alte ggf. zerstoert wurde)
    open_settings_menu_screen(true);
    destroy_dummy_screen();
}

static void open_dummy_settings_screen(const char *title)
{
    if (s_dummy_screen) return;

    s_dummy_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_dummy_screen);
    lv_obj_set_style_bg_color(s_dummy_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_dummy_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_dummy_screen, LV_OBJ_FLAG_SCROLLABLE);

    make_accent(s_dummy_screen, COLOR_SETTINGS);
    make_label(s_dummy_screen, &lv_font_montserrat_40, COLOR_TEXT,
               LV_TEXT_ALIGN_LEFT, 100, 32, 500, 52, title);
    make_back_button(s_dummy_screen, on_dummy_back);

    make_label(s_dummy_screen, &lv_font_montserrat_30, COLOR_MUTED,
               LV_TEXT_ALIGN_CENTER, 0, 200, LCD_H_RES, 42,
               "Noch nicht portiert");
    make_label(s_dummy_screen, &lv_font_montserrat_24, COLOR_DIM,
               LV_TEXT_ALIGN_CENTER, 0, 252, LCD_H_RES, 32,
               "Coming soon");

    lv_screen_load(s_dummy_screen);
}

// --- Item-Callbacks ----------------------------------------------------------
static void destroy_menu_screen(void)
{
    if (!s_menu_screen) return;
    lv_obj_delete_async(s_menu_screen);
    s_menu_screen = NULL;
}

static void on_item_wifi_clicked(lv_event_t *e)
{
    (void)e;
    destroy_menu_screen();
    open_dummy_settings_screen("WLAN");
}

static void on_item_screens_clicked(lv_event_t *e)
{
    (void)e;
    destroy_menu_screen();
    open_dummy_settings_screen("Screens");
}

static void on_item_crypto_clicked(lv_event_t *e)
{
    (void)e;
    destroy_menu_screen();
    open_dummy_settings_screen("Crypto");
}

static void on_item_display_clicked(lv_event_t *e)
{
    (void)e;
    destroy_menu_screen();
    // DisplaySettings kehrt ueber ui_settings_menu_reopen() ins Menue zurueck.
    ui_display_settings_open();
}

static void on_item_touch_clicked(lv_event_t *e)
{
    (void)e;
    destroy_menu_screen();
    open_dummy_settings_screen("Touch kalibrieren");
}

// --- Menue-Screen Aufbau / Navigation ---------------------------------------
static void on_back_to_dashboard(lv_event_t *e)
{
    (void)e;
    lv_obj_t *back = s_return_screen;
    s_return_screen = NULL;
    if (back) lv_screen_load(back);
    destroy_menu_screen();
}

static void open_settings_menu_screen(bool fresh)
{
    if (s_menu_screen) {
        lv_screen_load(s_menu_screen);
        return;
    }

    (void)fresh;
    s_menu_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_menu_screen);
    lv_obj_set_style_bg_color(s_menu_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_menu_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_menu_screen, LV_OBJ_FLAG_SCROLLABLE);

    make_accent(s_menu_screen, COLOR_SETTINGS);
    make_label(s_menu_screen, &lv_font_montserrat_40, COLOR_TEXT,
               LV_TEXT_ALIGN_LEFT, 100, 32, 500, 52, "Einstellungen");
    make_back_button(s_menu_screen, on_back_to_dashboard);

    // Item-Liste -------------------------------------------------------------
    int y = MENU_LIST_Y + MENU_ITEM_START_Y;

    make_menu_item(s_menu_screen, y, COLOR_CYAN, COLOR_BG,
                   "WLAN", "SSID und Passwort aendern",
                   on_item_wifi_clicked);
    y += MENU_ITEM_H + MENU_ITEM_GAP;

    make_menu_item(s_menu_screen, y, COLOR_CYAN, COLOR_BG,
                   "Screens", "Angezeigte Seiten auswaehlen",
                   on_item_screens_clicked);
    y += MENU_ITEM_H + MENU_ITEM_GAP;

    make_menu_item(s_menu_screen, y, COLOR_CYAN, COLOR_BG,
                   "Crypto", "Coin, Waehrung und Chart",
                   on_item_crypto_clicked);
    y += MENU_ITEM_H + MENU_ITEM_GAP;

    make_menu_item(s_menu_screen, y, COLOR_CYAN, COLOR_BG,
                   "Display", "Helligkeit und Rotation",
                   on_item_display_clicked);
    y += MENU_ITEM_H + MENU_ITEM_GAP;

    make_menu_item(s_menu_screen, y, COLOR_CYAN, COLOR_BG,
                   "Touch kalibrieren", "Beruehrung auf 5 Punkte abgleichen",
                   on_item_touch_clicked);

    lv_screen_load(s_menu_screen);
}

// --- Public API --------------------------------------------------------------
void ui_settings_menu_open(void)
{
    if (!s_return_screen) {
        s_return_screen = lv_screen_active();
    }
    open_settings_menu_screen(true);
}

void ui_settings_menu_reopen(void)
{
    // Re-Entry aus einem Sub-Screen (z. B. DisplaySettings). s_return_screen
    // bleibt erhalten, damit Back vom Menue weiterhin zum Dashboard fuehrt.
    open_settings_menu_screen(true);
}

bool ui_settings_menu_is_open(void)
{
    return s_menu_screen != NULL || s_dummy_screen != NULL;
}

lv_obj_t *ui_settings_menu_return_target(void)
{
    return s_return_screen;
}
