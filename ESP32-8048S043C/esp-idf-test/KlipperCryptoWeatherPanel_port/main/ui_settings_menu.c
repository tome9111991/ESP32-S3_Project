// Settings-Menue: 3x2 Kachel-Grid mit Icon + Label.
// Eintraege: WLAN, Screens, Crypto, Display, Touch, Standort.

#include "app_state.h"
#include "ui_assets.h"

#include <stdio.h>
#include <string.h>

// --- Layout ------------------------------------------------------------------
#define GRID_LEFT       40
#define GRID_TOP        110
#define GRID_COLS       3
#define GRID_ROWS       2
#define TILE_GAP        16
// (LCD_H_RES - 2*GRID_LEFT - (GRID_COLS-1)*TILE_GAP) / GRID_COLS = (800-80-32)/3 = 229
#define TILE_W          229
#define TILE_H          162
#define TILE_ICON_Y     28
#define TILE_LABEL_Y    96

static lv_obj_t *s_menu_screen   = NULL;
static lv_obj_t *s_return_screen = NULL;  // Dashboard-Screen, zu dem Back fuehrt
static bool      s_menu_open     = false;

// Forward decls
static void open_settings_menu_screen(bool fresh);
static void on_back_to_dashboard(lv_event_t *e);
static void on_item_wifi_clicked(lv_event_t *e);
static void on_item_screens_clicked(lv_event_t *e);
static void on_item_crypto_clicked(lv_event_t *e);
static void on_item_display_clicked(lv_event_t *e);
static void on_item_touch_clicked(lv_event_t *e);
static void on_item_location_clicked(lv_event_t *e);

// --- kleine Helfer -----------------------------------------------------------
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

// --- Tile --------------------------------------------------------------------
static void make_tile(lv_obj_t *parent, int col, int row,
                      const lv_image_dsc_t *icon_src, const char *label_text,
                      lv_event_cb_t cb)
{
    int x = GRID_LEFT + col * (TILE_W + TILE_GAP);
    int y = GRID_TOP  + row * (TILE_H + TILE_GAP);

    lv_obj_t *tile = lv_obj_create(parent);
    style_filled_rect(tile, COLOR_CYAN, 12);
    lv_obj_set_size(tile, TILE_W, TILE_H);
    lv_obj_set_pos(tile, x, y);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *icon = lv_image_create(tile);
    lv_image_set_src(icon, icon_src);
    lv_obj_set_size(icon, 48, 48);
    lv_obj_set_pos(icon, (TILE_W - 48) / 2, TILE_ICON_Y);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Label darunter, zentriert.
    lv_obj_t *label = make_label(tile, &lv_font_montserrat_28, COLOR_BG,
                                 LV_TEXT_ALIGN_CENTER, 0, TILE_LABEL_Y, TILE_W, 36,
                                 label_text);
    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
}

// --- Item-Callbacks ----------------------------------------------------------
static void destroy_menu_screen(void)
{
    s_menu_open = false;
    if (!s_menu_screen) return;
    lv_obj_delete_async(s_menu_screen);
    s_menu_screen = NULL;
}

static void on_item_wifi_clicked(lv_event_t *e)
{
    (void)e;
    ui_wifi_setup_open();
    destroy_menu_screen();
}

static void on_item_screens_clicked(lv_event_t *e)
{
    (void)e;
    ui_screen_settings_open();
    destroy_menu_screen();
}

static void on_item_crypto_clicked(lv_event_t *e)
{
    (void)e;
    ui_crypto_settings_open();
    destroy_menu_screen();
}

static void on_item_display_clicked(lv_event_t *e)
{
    (void)e;
    ui_display_settings_open();
    destroy_menu_screen();
}

static void on_item_touch_clicked(lv_event_t *e)
{
    (void)e;
    ui_touch_calibration_open();
    destroy_menu_screen();
}

static void on_item_location_clicked(lv_event_t *e)
{
    (void)e;
    ui_location_settings_open();
    destroy_menu_screen();
}

// --- Menue-Screen Aufbau / Navigation ---------------------------------------
static void on_back_to_dashboard(lv_event_t *e)
{
    (void)e;
    s_return_screen = NULL;
    // Settings verlassen ohne Screen-Animation; vermeidet LVGL-Layout-Haenger
    // und das Menue bleibt persistent, damit kein Delete im Back-Pfad laeuft.
    ui_load_current_screen_no_anim();
    s_menu_open = false;
}

static void open_settings_menu_screen(bool fresh)
{
    if (s_menu_screen) {
        s_menu_open = true;
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

    // 3x2 Tile-Grid. Spaltenreihenfolge: zeilenweise oben links nach rechts.
    make_tile(s_menu_screen, 0, 0, &icon_menu_wifi,     "WLAN",     on_item_wifi_clicked);
    make_tile(s_menu_screen, 1, 0, &icon_menu_screens,  "Screens",  on_item_screens_clicked);
    make_tile(s_menu_screen, 2, 0, &icon_menu_crypto,   "Crypto",   on_item_crypto_clicked);
    make_tile(s_menu_screen, 0, 1, &icon_menu_display,  "Display",  on_item_display_clicked);
    make_tile(s_menu_screen, 1, 1, &icon_menu_touch,    "Touch",    on_item_touch_clicked);
    make_tile(s_menu_screen, 2, 1, &icon_menu_location, "Standort", on_item_location_clicked);

    lv_screen_load(s_menu_screen);
    s_menu_open = true;
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
    return s_menu_open || ui_screen_settings_is_open() ||
           ui_crypto_settings_is_open() || ui_location_settings_is_open();
}

lv_obj_t *ui_settings_menu_return_target(void)
{
    return s_return_screen;
}
