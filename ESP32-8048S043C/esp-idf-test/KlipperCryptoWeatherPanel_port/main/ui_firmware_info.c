// Fullscreen-Firmware-Info-Screen, geoeffnet aus dem Long-Press-Popup.
// Zeigt Versions-Daten aus version.h und die OTA-UI (Check-Button,
// Status-Label, Install-Button). OTA-Service laeuft in ota_service.c; hier
// nur Trigger + Polling per LVGL-Timer.

#include "app_state.h"
#include "i18n.h"
#include "ota_service.h"
#include "version.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_screen        = NULL;
static lv_obj_t *s_return_screen = NULL;

static lv_obj_t *s_status_label  = NULL;
static lv_obj_t *s_check_btn     = NULL;
static lv_obj_t *s_install_btn   = NULL;
static lv_timer_t *s_status_timer = NULL;

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

static void close_screen(void)
{
    if (!s_screen) return;
    if (s_status_timer) {
        lv_timer_delete(s_status_timer);
        s_status_timer = NULL;
    }
    if (s_return_screen) {
        lv_screen_load(s_return_screen);
    }
    lv_obj_delete_async(s_screen);
    s_screen        = NULL;
    s_return_screen = NULL;
    s_status_label  = NULL;
    s_check_btn     = NULL;
    s_install_btn   = NULL;
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    close_screen();
}

static void on_check_clicked(lv_event_t *e)
{
    (void)e;
    ota_service_start_check();
}

static void on_install_clicked(lv_event_t *e)
{
    (void)e;
    ota_service_start_install();
}

static const char *ota_state_label(ota_state_t state)
{
    switch (state) {
        case OTA_STATE_IDLE:             return T(OTA_IDLE);
        case OTA_STATE_CHECKING:         return T(OTA_CHECKING);
        case OTA_STATE_NO_UPDATE:        return T(OTA_NO_UPDATE);
        case OTA_STATE_UPDATE_AVAILABLE: return T(OTA_UPDATE_AVAILABLE);
        case OTA_STATE_DOWNLOADING:      return T(OTA_DOWNLOADING);
        case OTA_STATE_SUCCESS:          return T(OTA_SUCCESS);
        case OTA_STATE_ERROR:            return T(OTA_ERROR);
        case OTA_STATE_NOT_CONFIGURED:   return T(OTA_NOT_CONFIGURED);
    }
    return "";
}

static void status_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_status_label) return;

    ota_status_t st;
    ota_service_get_status(&st);

    char line[160];
    if (st.state == OTA_STATE_DOWNLOADING) {
        snprintf(line, sizeof(line), "%s  %d%%", ota_state_label(st.state),
                 st.progress_percent);
    } else if (st.state == OTA_STATE_UPDATE_AVAILABLE && st.available_version[0]) {
        snprintf(line, sizeof(line), "%s: %s", ota_state_label(st.state),
                 st.available_version);
    } else if (st.message[0]) {
        snprintf(line, sizeof(line), "%s", st.message);
    } else {
        snprintf(line, sizeof(line), "%s", ota_state_label(st.state));
    }
    lv_label_set_text(s_status_label, line);

    // Install-Button nur sichtbar wenn ein Update bereitsteht.
    bool show_install = (st.state == OTA_STATE_UPDATE_AVAILABLE);
    if (s_install_btn) {
        if (show_install) lv_obj_remove_flag(s_install_btn, LV_OBJ_FLAG_HIDDEN);
        else              lv_obj_add_flag(s_install_btn,   LV_OBJ_FLAG_HIDDEN);
    }

    // Check-Button waehrend laufender Aktion deaktivieren.
    bool busy = (st.state == OTA_STATE_CHECKING || st.state == OTA_STATE_DOWNLOADING ||
                 st.state == OTA_STATE_SUCCESS);
    if (s_check_btn) {
        if (busy) lv_obj_add_state(s_check_btn,    LV_STATE_DISABLED);
        else      lv_obj_remove_state(s_check_btn, LV_STATE_DISABLED);
    }
}

static lv_obj_t *make_action_button(lv_obj_t *parent, int x, int y, int w, int h,
                                    uint32_t color, const char *text,
                                    lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_obj_create(parent);
    style_filled_rect(btn, color, 8);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x101820), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(btn, lv_color_hex(COLOR_DIM), LV_STATE_DISABLED);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = make_label(btn, &lv_font_montserrat_24, COLOR_TEXT,
                               LV_TEXT_ALIGN_CENTER, 0, 0, w, h, text);
    lv_obj_set_size(lbl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(lbl);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    return btn;
}

bool ui_firmware_info_is_open(void)
{
    return s_screen != NULL;
}

void ui_firmware_info_open(void)
{
    if (s_screen) return;

    s_return_screen = lv_screen_active();

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Accent + Titel (gleicher Pattern wie die anderen Sub-Screens)
    lv_obj_t *accent = lv_obj_create(s_screen);
    style_filled_rect(accent, COLOR_SETTINGS, 2);
    lv_obj_set_size(accent, 36, 5);
    lv_obj_set_pos(accent, 42, 58);

    make_label(s_screen, &lv_font_montserrat_40, COLOR_TEXT, LV_TEXT_ALIGN_LEFT,
               100, 32, 500, 52, T(POPUP_FIRMWARE));

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

    // Info-Karte mit allen Versions-Daten
    const struct { const char *label; const char *value; } rows[4] = {
        { T(POPUP_FW_NAME),     APP_FW_NAME    },
        { T(POPUP_FW_VERSION),  APP_FW_VERSION },
        { T(POPUP_FW_BOARD),    APP_FW_BOARD   },
        { T(POPUP_FW_LANGUAGE), APP_FW_LANG    },
    };
    const int row_count = sizeof(rows) / sizeof(rows[0]);

    const int label_x = 40;
    const int value_x = 240;
    const int first_y = 28;
    const int row_h   = 44;
    const int label_w = value_x - label_x - 16;
    const int value_w = 600 - value_x - 40;
    const int card_h  = first_y * 2 + row_h * row_count - (row_h - 32);

    lv_obj_t *card = lv_obj_create(s_screen);
    style_filled_rect(card, 0x151b24, 10);
    lv_obj_set_size(card, 600, card_h);
    lv_obj_set_pos(card, 100, 140);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_pad_all(card, 0, 0);

    for (int i = 0; i < row_count; ++i) {
        int y = first_y + row_h * i;
        make_label(card, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_LEFT,
                   label_x, y, label_w, 32, rows[i].label);
        make_label(card, &lv_font_montserrat_24, COLOR_TEXT, LV_TEXT_ALIGN_LEFT,
                   value_x, y, value_w, 32, rows[i].value);
    }

    // OTA-Status-Label unter der Karte
    const int below_y = 140 + card_h + 14;
    s_status_label = make_label(s_screen, &lv_font_montserrat_18, COLOR_MUTED,
                                LV_TEXT_ALIGN_CENTER, 100, below_y, 600, 28,
                                T(OTA_IDLE));

    // Buttons: Check links, Install rechts (Install initial hidden)
    const int btn_y = below_y + 36;
    const int btn_w = 280;
    const int btn_h = 56;
    s_check_btn = make_action_button(s_screen, 100, btn_y, btn_w, btn_h,
                                     0x232b38, T(OTA_CHECK), on_check_clicked);
    s_install_btn = make_action_button(s_screen, 100 + btn_w + 40, btn_y, btn_w, btn_h,
                                       0x1f3a2a, T(OTA_INSTALL), on_install_clicked);
    lv_obj_set_style_border_color(s_install_btn, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_add_flag(s_install_btn, LV_OBJ_FLAG_HIDDEN);

    // Status-Polling 4 Hz - reicht fuer Progress, ist billig.
    s_status_timer = lv_timer_create(status_timer_cb, 250, NULL);
    status_timer_cb(s_status_timer);  // initial fill

    lv_screen_load(s_screen);
}
