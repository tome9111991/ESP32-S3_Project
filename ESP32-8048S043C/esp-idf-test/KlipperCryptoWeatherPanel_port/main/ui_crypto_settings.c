// Crypto-Settings-Screen: Coin, Waehrung und Chart-Granularitaet.
// Port von 16_CryptoSettings.ino auf ESP-IDF/NVS.

#include "app_state.h"
#include "i18n.h"

#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "crypto_ui";

#define INVALID_INDEX 255

static const char * const BASE_OPTIONS[] = {
    "BTC", "ETH", "SOL", "XRP", "DOGE", "ADA",
};
static const char * const QUOTE_OPTIONS[] = {
    "USD", "EUR", "GBP", "USDC", "USDT",
};
static const char * const TIMEFRAME_OPTIONS[] = {
    "15M", "1H", "6H", "1D",
};

#define BASE_COUNT      (sizeof(BASE_OPTIONS) / sizeof(BASE_OPTIONS[0]))
#define QUOTE_COUNT     (sizeof(QUOTE_OPTIONS) / sizeof(QUOTE_OPTIONS[0]))
#define TIMEFRAME_COUNT (sizeof(TIMEFRAME_OPTIONS) / sizeof(TIMEFRAME_OPTIONS[0]))

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_return_screen = NULL;
static bool s_from_settings_menu = false;

static lv_obj_t *s_base_buttons[BASE_COUNT];
static lv_obj_t *s_quote_buttons[QUOTE_COUNT];
static lv_obj_t *s_timeframe_buttons[TIMEFRAME_COUNT];
static lv_obj_t *s_status_label = NULL;

static uint8_t s_base_data[BASE_COUNT];
static uint8_t s_quote_data[QUOTE_COUNT];
static uint8_t s_timeframe_data[TIMEFRAME_COUNT];
static uint8_t s_base_index = INVALID_INDEX;
static uint8_t s_quote_index = INVALID_INDEX;
static uint8_t s_timeframe_index = INVALID_INDEX;

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

static uint8_t option_index(const char *value, const char * const *options, size_t count)
{
    if (!value) return INVALID_INDEX;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(value, options[i]) == 0) return (uint8_t)i;
    }
    return INVALID_INDEX;
}

static bool draft_complete(void)
{
    return s_base_index < BASE_COUNT &&
           s_quote_index < QUOTE_COUNT &&
           s_timeframe_index < TIMEFRAME_COUNT;
}

static bool draft_changed(void)
{
    if (!draft_complete()) return false;
    return strcmp(g_crypto.base, BASE_OPTIONS[s_base_index]) != 0 ||
           strcmp(g_crypto.quote, QUOTE_OPTIONS[s_quote_index]) != 0 ||
           strcmp(crypto_chart_timeframe_label(), TIMEFRAME_OPTIONS[s_timeframe_index]) != 0;
}

static void update_button(lv_obj_t *button, bool selected)
{
    if (!button) return;
    lv_obj_set_style_bg_color(button, lv_color_hex(selected ? COLOR_BTC : 0x151b24), 0);
    lv_obj_set_style_border_color(button, lv_color_hex(selected ? COLOR_TEXT : COLOR_DIM), 0);
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label) {
        lv_obj_set_style_text_color(label, lv_color_hex(selected ? COLOR_BG : COLOR_TEXT), 0);
    }
}

static void update_ui(void)
{
    for (size_t i = 0; i < BASE_COUNT; i++) update_button(s_base_buttons[i], i == s_base_index);
    for (size_t i = 0; i < QUOTE_COUNT; i++) update_button(s_quote_buttons[i], i == s_quote_index);
    for (size_t i = 0; i < TIMEFRAME_COUNT; i++) update_button(s_timeframe_buttons[i], i == s_timeframe_index);

    if (!s_status_label) return;
    if (!draft_complete()) {
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_ORANGE), 0);
        lv_label_set_text(s_status_label, T(CRYPTO_SELECT_ALL));
    } else if (draft_changed()) {
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_BTC), 0);
        lv_label_set_text(s_status_label, T(COMMON_SELECTION_SAVED_ON_BACK));
    } else {
        char text[64];
        snprintf(text, sizeof(text), "%s / %s  %s %s",
                 g_crypto.base, g_crypto.quote, T(CRYPTO_CANDLES),
                 crypto_chart_timeframe_label());
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_MUTED), 0);
        lv_label_set_text(s_status_label, text);
    }
}

static void on_base_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    uint8_t *index = (uint8_t *)lv_event_get_user_data(e);
    if (index && *index < BASE_COUNT) {
        s_base_index = *index;
        update_ui();
    }
}

static void on_quote_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    uint8_t *index = (uint8_t *)lv_event_get_user_data(e);
    if (index && *index < QUOTE_COUNT) {
        s_quote_index = *index;
        update_ui();
    }
}

static void on_timeframe_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    uint8_t *index = (uint8_t *)lv_event_get_user_data(e);
    if (index && *index < TIMEFRAME_COUNT) {
        s_timeframe_index = *index;
        update_ui();
    }
}

static void destroy_screen(void)
{
    if (!s_screen) return;
    lv_obj_delete_async(s_screen);
    s_screen = NULL;
    s_status_label = NULL;
    memset(s_base_buttons, 0, sizeof(s_base_buttons));
    memset(s_quote_buttons, 0, sizeof(s_quote_buttons));
    memset(s_timeframe_buttons, 0, sizeof(s_timeframe_buttons));
}

static void close_screen(void)
{
    const bool back_to_menu = s_from_settings_menu;
    lv_obj_t *ret = s_return_screen;

    if (draft_changed()) {
        crypto_settings_apply(BASE_OPTIONS[s_base_index],
                              QUOTE_OPTIONS[s_quote_index],
                              TIMEFRAME_OPTIONS[s_timeframe_index]);
        if (!crypto_settings_save()) {
            ESP_LOGW(TAG, "Crypto Settings speichern fehlgeschlagen");
        }
    }

    s_return_screen = NULL;
    s_from_settings_menu = false;
    // Erst Zielscreen laden, dann den aktiven Settings-Screen loeschen.
    if (back_to_menu) ui_settings_menu_reopen();
    else if (ret) lv_screen_load(ret);
    destroy_screen();
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    close_screen();
}

static lv_obj_t *make_back_button(lv_obj_t *parent)
{
    lv_obj_t *btn = lv_obj_create(parent);
    style_filled_rect(btn, 0x232b38, 8);
    lv_obj_set_size(btn, 52, 52);
    lv_obj_set_pos(btn, LCD_H_RES - 52 - 24, 30);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, on_back_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = make_label(btn, &lv_font_montserrat_24, COLOR_TEXT,
                                 LV_TEXT_ALIGN_CENTER, 0, 0, 52, 52, LV_SYMBOL_LEFT);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(label);
    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
    return btn;
}

static void make_section_label(lv_obj_t *parent, const char *text, int y)
{
    make_label(parent, &lv_font_montserrat_28, COLOR_TEXT, LV_TEXT_ALIGN_CENTER,
               100, y, 600, 34, text);
}

static lv_obj_t *make_option(lv_obj_t *parent, int x, int y, int w, const char *text,
                             lv_event_cb_t cb, uint8_t *index_data)
{
    lv_obj_t *btn = lv_obj_create(parent);
    style_filled_rect(btn, 0x151b24, 8);
    lv_obj_set_size(btn, w, 50);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, index_data);

    lv_obj_t *label = make_label(btn, &lv_font_montserrat_24, COLOR_TEXT,
                                 LV_TEXT_ALIGN_CENTER, 0, 9, w, 34, text);
    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
    return btn;
}

bool ui_crypto_settings_is_open(void)
{
    return s_screen != NULL;
}

void ui_crypto_settings_open(void)
{
    if (s_screen) return;

    lv_obj_t *menu_return = ui_settings_menu_return_target();
    if (menu_return) {
        s_return_screen = menu_return;
        s_from_settings_menu = true;
    } else {
        s_return_screen = lv_screen_active();
        s_from_settings_menu = false;
    }

    s_base_index = option_index(g_crypto.base, BASE_OPTIONS, BASE_COUNT);
    s_quote_index = option_index(g_crypto.quote, QUOTE_OPTIONS, QUOTE_COUNT);
    s_timeframe_index = option_index(crypto_chart_timeframe_label(), TIMEFRAME_OPTIONS, TIMEFRAME_COUNT);

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent = lv_obj_create(s_screen);
    style_filled_rect(accent, COLOR_SETTINGS, 2);
    lv_obj_set_size(accent, 36, 5);
    lv_obj_set_pos(accent, 42, 58);

    make_label(s_screen, &lv_font_montserrat_40, COLOR_TEXT, LV_TEXT_ALIGN_LEFT,
               100, 32, 500, 52, "Crypto");
    make_back_button(s_screen);

    make_section_label(s_screen, "Coin", 112);
    for (size_t i = 0; i < BASE_COUNT; i++) {
        s_base_data[i] = (uint8_t)i;
        s_base_buttons[i] = make_option(s_screen, 100 + ((int)i * 98), 152, 90,
                                        BASE_OPTIONS[i], on_base_clicked, &s_base_data[i]);
    }

    make_section_label(s_screen, T(CRYPTO_CURRENCY), 220);
    for (size_t i = 0; i < QUOTE_COUNT; i++) {
        s_quote_data[i] = (uint8_t)i;
        s_quote_buttons[i] = make_option(s_screen, 90 + ((int)i * 124), 260, 112,
                                         QUOTE_OPTIONS[i], on_quote_clicked, &s_quote_data[i]);
    }

    make_section_label(s_screen, T(CRYPTO_CANDLES), 328);
    for (size_t i = 0; i < TIMEFRAME_COUNT; i++) {
        s_timeframe_data[i] = (uint8_t)i;
        s_timeframe_buttons[i] = make_option(s_screen, 150 + ((int)i * 128), 368, 112,
                                             TIMEFRAME_OPTIONS[i], on_timeframe_clicked,
                                             &s_timeframe_data[i]);
    }

    s_status_label = make_label(s_screen, &lv_font_montserrat_24, COLOR_MUTED,
                                LV_TEXT_ALIGN_CENTER, 60, 438, 680, 34, "");
    update_ui();
    lv_screen_load(s_screen);
}
