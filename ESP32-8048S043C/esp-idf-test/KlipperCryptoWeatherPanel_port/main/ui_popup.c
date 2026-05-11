// Popup-Menue Port (Settings/Reboot/Factory Reset) inkl. Confirm-Dialog.
// Pendant zum Arduino-Sketch 10_PopupMenu.ino.

#include "app_state.h"
#include "ui_assets.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

static const char *TAG = "popup";

#define POPUP_MENU_W            360
#define POPUP_MENU_H            330
#define POPUP_BUTTON_W          240
#define POPUP_BUTTON_H          64
#define POPUP_BUTTON_GAP        16
#define POPUP_BUTTONS_TOTAL_H   (POPUP_BUTTON_H * 3 + POPUP_BUTTON_GAP * 2)
#define POPUP_BUTTONS_TOP_PAD   ((POPUP_MENU_H - POPUP_BUTTONS_TOTAL_H) / 2)

#define CONFIRM_PANEL_W         600
#define CONFIRM_PANEL_H         280
#define CONFIRM_BTN_W           240
#define CONFIRM_BTN_H           64

static lv_obj_t *s_backdrop      = NULL;
static lv_obj_t *s_panel         = NULL;
static lv_obj_t *s_confirm_back  = NULL;
static lv_obj_t *s_confirm_panel = NULL;

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
                            lv_text_align_t align, int x, int y, int w, int h, const char *text)
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

// --- Button-Callbacks --------------------------------------------------------

static void close_confirm(void)
{
    if (!s_confirm_back) return;
    lv_obj_delete(s_confirm_back);
    s_confirm_back  = NULL;
    s_confirm_panel = NULL;
}

static void on_settings_clicked(lv_event_t *e)
{
    (void)e;
    ui_popup_close();
    ui_settings_menu_open();
}

static void on_reboot_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Reboot ueber Popup-Menue");
    ui_perform_clean_reboot();
}

static void on_factory_confirm_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Factory Reset bestaetigt");
    ui_perform_factory_reset();
}

static void on_factory_cancel_clicked(lv_event_t *e)
{
    (void)e;
    close_confirm();
}

static void open_factory_confirm(void);
static void on_factory_reset_clicked(lv_event_t *e)
{
    (void)e;
    open_factory_confirm();
}

// Tap ausserhalb des Panels: Popup schliessen (entspricht Arduino-Verhalten).
static void on_backdrop_clicked(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    if (target == s_backdrop) {
        ui_popup_close();
    }
}

// --- Confirm-Dialog ----------------------------------------------------------

static void open_factory_confirm(void)
{
    if (s_confirm_back) return;

    lv_obj_t *parent = lv_screen_active();
    s_confirm_back = lv_obj_create(parent);
    lv_obj_remove_style_all(s_confirm_back);
    lv_obj_set_size(s_confirm_back, LCD_H_RES, LCD_V_RES);
    lv_obj_set_pos(s_confirm_back, 0, 0);
    lv_obj_set_style_bg_color(s_confirm_back, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_confirm_back, 200, 0);
    lv_obj_remove_flag(s_confirm_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_confirm_back, LV_OBJ_FLAG_CLICKABLE);  // schluckt Klicks Richtung Popup darunter

    s_confirm_panel = lv_obj_create(s_confirm_back);
    style_filled_rect(s_confirm_panel, 0x171d26, 10);
    lv_obj_set_size(s_confirm_panel, CONFIRM_PANEL_W, CONFIRM_PANEL_H);
    lv_obj_set_pos(s_confirm_panel, (LCD_H_RES - CONFIRM_PANEL_W) / 2, (LCD_V_RES - CONFIRM_PANEL_H) / 2);
    lv_obj_set_style_border_width(s_confirm_panel, 2, 0);
    lv_obj_set_style_border_color(s_confirm_panel, lv_color_hex(COLOR_LOSS), 0);

    make_label(s_confirm_panel, &lv_font_montserrat_30, COLOR_LOSS, LV_TEXT_ALIGN_CENTER,
               20, 24, CONFIRM_PANEL_W - 40, 40, "Factory Reset");
    make_label(s_confirm_panel, &lv_font_montserrat_24, COLOR_TEXT, LV_TEXT_ALIGN_CENTER,
               30, 78, CONFIRM_PANEL_W - 60, 32, "Alle Einstellungen werden geloescht");
    make_label(s_confirm_panel, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_CENTER,
               30, 116, CONFIRM_PANEL_W - 60, 32, "und das Geraet startet neu.");

    int btn_y = CONFIRM_PANEL_H - CONFIRM_BTN_H - 28;

    lv_obj_t *cancel = lv_obj_create(s_confirm_panel);
    style_filled_rect(cancel, 0x232b38, 8);
    lv_obj_set_size(cancel, CONFIRM_BTN_W, CONFIRM_BTN_H);
    lv_obj_set_pos(cancel, 32, btn_y);
    lv_obj_set_style_border_width(cancel, 2, 0);
    lv_obj_set_style_border_color(cancel, lv_color_hex(COLOR_DIM), 0);
    lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cancel, on_factory_cancel_clicked, LV_EVENT_CLICKED, NULL);
    make_label(cancel, &lv_font_montserrat_30, COLOR_TEXT, LV_TEXT_ALIGN_CENTER,
               0, 14, CONFIRM_BTN_W, CONFIRM_BTN_H - 14, "Abbrechen");

    lv_obj_t *ok = lv_obj_create(s_confirm_panel);
    style_filled_rect(ok, COLOR_LOSS, 8);
    lv_obj_set_size(ok, CONFIRM_BTN_W, CONFIRM_BTN_H);
    lv_obj_set_pos(ok, CONFIRM_PANEL_W - CONFIRM_BTN_W - 32, btn_y);
    lv_obj_add_flag(ok, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ok, on_factory_confirm_clicked, LV_EVENT_CLICKED, NULL);
    make_label(ok, &lv_font_montserrat_30, COLOR_BG, LV_TEXT_ALIGN_CENTER,
               0, 14, CONFIRM_BTN_W, CONFIRM_BTN_H - 14, "Zuruecksetzen");
}

// --- Popup-Menue -------------------------------------------------------------

bool ui_popup_is_open(void)
{
    return s_backdrop != NULL;
}

void ui_popup_open(void)
{
    if (s_backdrop) return;

    lv_obj_t *parent = lv_screen_active();
    s_backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(s_backdrop);
    lv_obj_set_size(s_backdrop, LCD_H_RES, LCD_V_RES);
    lv_obj_set_pos(s_backdrop, 0, 0);
    lv_obj_set_style_bg_color(s_backdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_backdrop, 150, 0);
    lv_obj_remove_flag(s_backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_backdrop, on_backdrop_clicked, LV_EVENT_CLICKED, NULL);

    s_panel = lv_obj_create(s_backdrop);
    style_filled_rect(s_panel, 0x171d26, 8);
    lv_obj_set_size(s_panel, POPUP_MENU_W, POPUP_MENU_H);
    lv_obj_set_pos(s_panel, (LCD_H_RES - POPUP_MENU_W) / 2, (LCD_V_RES - POPUP_MENU_H) / 2);
    lv_obj_set_style_border_width(s_panel, 2, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(COLOR_DIM), 0);
    // Klicks im Panel-Hintergrund (zwischen den Buttons) absorbieren, damit sie
    // nicht zum Backdrop durchschlagen und das Popup schliessen.
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_CLICKABLE);

    int btn_x = (POPUP_MENU_W - POPUP_BUTTON_W) / 2;
    int settings_y = POPUP_BUTTONS_TOP_PAD;
    int reboot_y   = settings_y + POPUP_BUTTON_H + POPUP_BUTTON_GAP;
    int reset_y    = reboot_y   + POPUP_BUTTON_H + POPUP_BUTTON_GAP;

    // Settings
    lv_obj_t *settings = lv_obj_create(s_panel);
    style_filled_rect(settings, COLOR_CYAN, 8);
    lv_obj_set_size(settings, POPUP_BUTTON_W, POPUP_BUTTON_H);
    lv_obj_set_pos(settings, btn_x, settings_y);
    lv_obj_add_flag(settings, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(settings, on_settings_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *icon = lv_image_create(settings);
    lv_image_set_src(icon, &icon_settings);
    lv_obj_set_size(icon, 48, 48);
    lv_obj_set_pos(icon, 30, 8);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);

    make_label(settings, &lv_font_montserrat_30, COLOR_BG, LV_TEXT_ALIGN_CENTER,
               86, 12, 130, POPUP_BUTTON_H - 12, "Settings");

    // Reboot
    lv_obj_t *reboot = lv_obj_create(s_panel);
    style_filled_rect(reboot, COLOR_ORANGE, 8);
    lv_obj_set_size(reboot, POPUP_BUTTON_W, POPUP_BUTTON_H);
    lv_obj_set_pos(reboot, btn_x, reboot_y);
    lv_obj_add_flag(reboot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(reboot, on_reboot_clicked, LV_EVENT_CLICKED, NULL);
    make_label(reboot, &lv_font_montserrat_30, COLOR_BG, LV_TEXT_ALIGN_CENTER,
               0, 14, POPUP_BUTTON_W, POPUP_BUTTON_H - 14, "Reboot");

    // Factory Reset
    lv_obj_t *reset = lv_obj_create(s_panel);
    style_filled_rect(reset, COLOR_LOSS, 8);
    lv_obj_set_size(reset, POPUP_BUTTON_W, POPUP_BUTTON_H);
    lv_obj_set_pos(reset, btn_x, reset_y);
    lv_obj_add_flag(reset, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(reset, on_factory_reset_clicked, LV_EVENT_CLICKED, NULL);
    make_label(reset, &lv_font_montserrat_30, COLOR_BG, LV_TEXT_ALIGN_CENTER,
               0, 14, POPUP_BUTTON_W, POPUP_BUTTON_H - 14, "Factory Reset");
}

void ui_popup_close(void)
{
    if (!s_backdrop) return;
    close_confirm();
    lv_obj_delete(s_backdrop);
    s_backdrop = NULL;
    s_panel    = NULL;
}

// --- Reboot / Factory Reset --------------------------------------------------

void ui_perform_clean_reboot(void)
{
    ESP_LOGI(TAG, "Sauberer Neustart angefordert");
    // 250 ms wie im Arduino-Sketch, damit LVGL-Buttons noch Release-Event sehen.
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
}

void ui_perform_factory_reset(void)
{
    ESP_LOGI(TAG, "Factory Reset: NVS wird geloescht");
    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_flash_erase fehlgeschlagen: %s", esp_err_to_name(err));
    }
    ui_perform_clean_reboot();
}
