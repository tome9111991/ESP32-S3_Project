// Screen-Settings-Screen: sichtbare Dashboard-Seiten auswaehlen.
// Port von 15_ScreenSettings.ino auf ESP-IDF/NVS.

#include "app_state.h"
#include "i18n.h"
#include "ui_assets.h"

#include "esp_log.h"
#include "nvs.h"

#include <stdio.h>

static const char *TAG = "screen_ui";

#define SCREEN_NVS_NS      "screens"
#define SCREEN_NVS_TIME    "time"
#define SCREEN_NVS_PRICE   "price"
#define SCREEN_NVS_CHART   "chart"
#define SCREEN_NVS_KLIPPER "klipper"
#define SCREEN_NVS_DUR_TIME    "d_time"
#define SCREEN_NVS_DUR_PRICE   "d_price"
#define SCREEN_NVS_DUR_CHART   "d_chart"
#define SCREEN_NVS_DUR_KLIPPER "d_klipper"

#define OPTION_COUNT 4
#define TILE_W       290
#define TILE_H       116
#define TILE_X       100
#define TILE_Y       126
#define TILE_GAP_X   20
#define TILE_GAP_Y   18

static bool s_enabled[OPTION_COUNT] = { true, true, true, true };
static uint8_t s_duration[OPTION_COUNT] = {
    SCREEN_DURATION_TIME_DEF,
    SCREEN_DURATION_OTHER_DEF,
    SCREEN_DURATION_OTHER_DEF,
    SCREEN_DURATION_OTHER_DEF,
};
static uint8_t s_saved_duration[OPTION_COUNT] = {
    SCREEN_DURATION_TIME_DEF,
    SCREEN_DURATION_OTHER_DEF,
    SCREEN_DURATION_OTHER_DEF,
    SCREEN_DURATION_OTHER_DEF,
};

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_return_screen = NULL;
static bool s_from_settings_menu = false;

static lv_obj_t *s_tile[OPTION_COUNT];
static lv_obj_t *s_title_label[OPTION_COUNT];
static lv_obj_t *s_underline[OPTION_COUNT];
static lv_obj_t *s_state_label[OPTION_COUNT];
static lv_obj_t *s_settings_btn[OPTION_COUNT];
static lv_obj_t *s_status_label = NULL;
static uint8_t s_tile_data[OPTION_COUNT] = { 0, 1, 2, 3 };

// Per-Tile Detail-Screen mit Anzeigedauer-Slider.
static lv_obj_t *s_detail_screen = NULL;
static uint8_t   s_detail_index  = 0;
static lv_obj_t *s_detail_duration_slider = NULL;
static lv_obj_t *s_detail_duration_value  = NULL;

static bool s_saved_time = true;
static bool s_saved_price = true;
static bool s_saved_chart = true;
static bool s_saved_klipper = true;

static bool state_valid(screen_state_t state)
{
    return (int)state >= 0 && (int)state < OPTION_COUNT;
}

static screen_state_t state_for_index(uint8_t index)
{
    if (index == 1) return SCREEN_CRYPTO;
    if (index == 2) return SCREEN_BTC_DAY;
    if (index == 3) return SCREEN_KLIPPER;
    return SCREEN_TIME;
}

static const char *title_for_index(uint8_t index)
{
    if (index == 1) return T(SCREEN_PRICE_SHORT);
    if (index == 2) return T(SCREEN_CHART_SHORT);
    if (index == 3) return T(SCREEN_KLIPPER_SHORT);
    return T(SCREEN_CLOCK_SHORT);
}

static const char *hint_for_index(uint8_t index)
{
    if (index == 1) return T(SCREEN_LIVE_PRICE);
    if (index == 2) return T(SCREEN_CANDLE_CHART);
    if (index == 3) return T(SCREEN_PRINTER_STATUS);
    return T(SCREEN_TIME_WEATHER);
}

static uint32_t color_for_index(uint8_t index)
{
    if (index == 3) return COLOR_GREEN;
    if (index == 1 || index == 2) return COLOR_BTC;
    return COLOR_CYAN;
}

static bool changed(void)
{
    if (s_saved_time != s_enabled[SCREEN_TIME] ||
        s_saved_price != s_enabled[SCREEN_CRYPTO] ||
        s_saved_chart != s_enabled[SCREEN_BTC_DAY] ||
        s_saved_klipper != s_enabled[SCREEN_KLIPPER]) {
        return true;
    }
    for (int i = 0; i < OPTION_COUNT; i++) {
        if (s_duration[i] != s_saved_duration[i]) return true;
    }
    return false;
}

static uint8_t clamp_duration(int v)
{
    if (v < SCREEN_DURATION_MIN_S) v = SCREEN_DURATION_MIN_S;
    if (v > SCREEN_DURATION_MAX_S) v = SCREEN_DURATION_MAX_S;
    return (uint8_t)v;
}

void screen_settings_init_defaults(void)
{
    s_enabled[SCREEN_TIME] = true;
    s_enabled[SCREEN_CRYPTO] = true;
    s_enabled[SCREEN_BTC_DAY] = true;
    s_enabled[SCREEN_KLIPPER] = true;
    s_duration[SCREEN_TIME]    = SCREEN_DURATION_TIME_DEF;
    s_duration[SCREEN_CRYPTO]  = SCREEN_DURATION_OTHER_DEF;
    s_duration[SCREEN_BTC_DAY] = SCREEN_DURATION_OTHER_DEF;
    s_duration[SCREEN_KLIPPER] = SCREEN_DURATION_OTHER_DEF;
}

uint8_t screen_settings_duration_seconds(screen_state_t state)
{
    if (!state_valid(state)) return SCREEN_DURATION_OTHER_DEF;
    return s_duration[state];
}

void screen_settings_set_duration_seconds(screen_state_t state, uint8_t seconds)
{
    if (!state_valid(state)) return;
    s_duration[state] = clamp_duration(seconds);
}

int screen_settings_enabled_count(void)
{
    int count = 0;
    for (int i = 0; i < OPTION_COUNT; i++) {
        if (s_enabled[i]) count++;
    }
    return count;
}

void screen_settings_ensure_one_enabled(void)
{
    if (screen_settings_enabled_count() > 0) return;
    s_enabled[SCREEN_TIME] = true;
}

bool screen_settings_is_enabled(screen_state_t state)
{
    if (!state_valid(state)) return true;
    return s_enabled[state];
}

void screen_settings_set_enabled(screen_state_t state, bool enabled)
{
    if (!state_valid(state)) return;
    s_enabled[state] = enabled;
    screen_settings_ensure_one_enabled();
}

bool screen_settings_load(void)
{
    screen_settings_init_defaults();

    nvs_handle_t h;
    if (nvs_open(SCREEN_NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;

    bool loaded = false;
    uint8_t value = 0;
    if (nvs_get_u8(h, SCREEN_NVS_TIME, &value) == ESP_OK) {
        s_enabled[SCREEN_TIME] = value != 0;
        loaded = true;
    }
    if (nvs_get_u8(h, SCREEN_NVS_PRICE, &value) == ESP_OK) {
        s_enabled[SCREEN_CRYPTO] = value != 0;
        loaded = true;
    }
    if (nvs_get_u8(h, SCREEN_NVS_CHART, &value) == ESP_OK) {
        s_enabled[SCREEN_BTC_DAY] = value != 0;
        loaded = true;
    }
    if (nvs_get_u8(h, SCREEN_NVS_KLIPPER, &value) == ESP_OK) {
        s_enabled[SCREEN_KLIPPER] = value != 0;
        loaded = true;
    }
    if (nvs_get_u8(h, SCREEN_NVS_DUR_TIME, &value) == ESP_OK) {
        s_duration[SCREEN_TIME] = clamp_duration(value);
        loaded = true;
    }
    if (nvs_get_u8(h, SCREEN_NVS_DUR_PRICE, &value) == ESP_OK) {
        s_duration[SCREEN_CRYPTO] = clamp_duration(value);
        loaded = true;
    }
    if (nvs_get_u8(h, SCREEN_NVS_DUR_CHART, &value) == ESP_OK) {
        s_duration[SCREEN_BTC_DAY] = clamp_duration(value);
        loaded = true;
    }
    if (nvs_get_u8(h, SCREEN_NVS_DUR_KLIPPER, &value) == ESP_OK) {
        s_duration[SCREEN_KLIPPER] = clamp_duration(value);
        loaded = true;
    }
    nvs_close(h);

    screen_settings_ensure_one_enabled();
    for (int i = 0; i < OPTION_COUNT; i++) s_saved_duration[i] = s_duration[i];
    ESP_LOGI(TAG, "Screen Settings geladen: time=%u price=%u chart=%u klipper=%u",
             s_enabled[SCREEN_TIME] ? 1u : 0u,
             s_enabled[SCREEN_CRYPTO] ? 1u : 0u,
             s_enabled[SCREEN_BTC_DAY] ? 1u : 0u,
             s_enabled[SCREEN_KLIPPER] ? 1u : 0u);
    ESP_LOGI(TAG, "Screen Dauer: time=%us price=%us chart=%us klipper=%us",
             s_duration[SCREEN_TIME], s_duration[SCREEN_CRYPTO],
             s_duration[SCREEN_BTC_DAY], s_duration[SCREEN_KLIPPER]);
    return loaded;
}

bool screen_settings_save(void)
{
    screen_settings_ensure_one_enabled();

    nvs_handle_t h;
    esp_err_t err = nvs_open(SCREEN_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "screen nvs_open RW: %s", esp_err_to_name(err));
        return false;
    }

    bool ok = nvs_set_u8(h, SCREEN_NVS_TIME, s_enabled[SCREEN_TIME] ? 1 : 0) == ESP_OK;
    if (ok) ok = nvs_set_u8(h, SCREEN_NVS_PRICE, s_enabled[SCREEN_CRYPTO] ? 1 : 0) == ESP_OK;
    if (ok) ok = nvs_set_u8(h, SCREEN_NVS_CHART, s_enabled[SCREEN_BTC_DAY] ? 1 : 0) == ESP_OK;
    if (ok) ok = nvs_set_u8(h, SCREEN_NVS_KLIPPER, s_enabled[SCREEN_KLIPPER] ? 1 : 0) == ESP_OK;
    if (ok) ok = nvs_set_u8(h, SCREEN_NVS_DUR_TIME, s_duration[SCREEN_TIME]) == ESP_OK;
    if (ok) ok = nvs_set_u8(h, SCREEN_NVS_DUR_PRICE, s_duration[SCREEN_CRYPTO]) == ESP_OK;
    if (ok) ok = nvs_set_u8(h, SCREEN_NVS_DUR_CHART, s_duration[SCREEN_BTC_DAY]) == ESP_OK;
    if (ok) ok = nvs_set_u8(h, SCREEN_NVS_DUR_KLIPPER, s_duration[SCREEN_KLIPPER]) == ESP_OK;
    if (ok) ok = nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

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

static lv_obj_t *make_divider(lv_obj_t *parent, int x, int y, int w, uint32_t color)
{
    lv_obj_t *divider = lv_obj_create(parent);
    style_filled_rect(divider, color, 2);
    lv_obj_set_size(divider, w, 4);
    lv_obj_set_pos(divider, x, y);
    return divider;
}

static lv_obj_t *make_back_button(lv_obj_t *parent);
static void update_ui(void);

static void destroy_detail_screen(void);

static void destroy_screen(void)
{
    destroy_detail_screen();
    if (!s_screen) return;
    lv_obj_delete_async(s_screen);
    s_screen = NULL;
    s_status_label = NULL;
    for (int i = 0; i < OPTION_COUNT; i++) {
        s_tile[i] = NULL;
        s_title_label[i] = NULL;
        s_underline[i] = NULL;
        s_state_label[i] = NULL;
        s_settings_btn[i] = NULL;
    }
}

static void destroy_detail_screen(void)
{
    if (!s_detail_screen) return;
    lv_obj_delete_async(s_detail_screen);
    s_detail_screen = NULL;
    s_detail_duration_slider = NULL;
    s_detail_duration_value  = NULL;
}

static void update_duration_value_label(uint8_t seconds)
{
    if (!s_detail_duration_value) return;
    char buf[24];
    snprintf(buf, sizeof(buf), "%u s", (unsigned)seconds);
    lv_label_set_text(s_detail_duration_value, buf);
}

static void on_duration_slider_changed(lv_event_t *e)
{
    (void)e;
    if (!s_detail_duration_slider) return;
    if (s_detail_index >= OPTION_COUNT) return;
    int32_t v = lv_slider_get_value(s_detail_duration_slider);
    uint8_t seconds = clamp_duration((int)v);
    s_duration[state_for_index(s_detail_index)] = seconds;
    update_duration_value_label(seconds);
}

static void on_detail_back_clicked(lv_event_t *e)
{
    (void)e;
    if (s_screen) lv_screen_load(s_screen);
    destroy_detail_screen();
}

static void open_tile_detail(uint8_t index)
{
    if (index >= OPTION_COUNT) return;
    if (s_detail_screen) destroy_detail_screen();
    s_detail_index = index;

    s_detail_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_detail_screen);
    lv_obj_set_style_bg_color(s_detail_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_detail_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_detail_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent = lv_obj_create(s_detail_screen);
    style_filled_rect(accent, color_for_index(index), 2);
    lv_obj_set_size(accent, 36, 5);
    lv_obj_set_pos(accent, 42, 58);

    char title_buf[48];
    snprintf(title_buf, sizeof(title_buf), "%s %s",
             title_for_index(index), T(SCREEN_OPTIONS));
    make_label(s_detail_screen, &lv_font_montserrat_40, COLOR_TEXT,
               LV_TEXT_ALIGN_LEFT, 100, 32, 600, 52, title_buf);

    // Back button (close detail, return to screen settings overview).
    lv_obj_t *btn = lv_obj_create(s_detail_screen);
    style_filled_rect(btn, 0x232b38, 8);
    lv_obj_set_size(btn, 52, 52);
    lv_obj_set_pos(btn, LCD_H_RES - 52 - 24, 30);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, on_detail_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = make_label(btn, &lv_font_montserrat_24, COLOR_TEXT,
                                      LV_TEXT_ALIGN_CENTER, 0, 0, 52, 52,
                                      LV_SYMBOL_LEFT);
    lv_obj_set_size(back_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(back_label);
    lv_obj_add_flag(back_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Anzeigedauer-Slider (5-120 s) mit Live-Label.
    const uint32_t accent_color = color_for_index(index);
    const uint8_t  current_dur  = s_duration[state_for_index(index)];

    make_label(s_detail_screen, &lv_font_montserrat_30, COLOR_TEXT,
               LV_TEXT_ALIGN_LEFT, 100, 160, 360, 36, T(SCREEN_DISPLAY_DURATION));

    s_detail_duration_value = make_label(s_detail_screen, &lv_font_montserrat_24,
                                         COLOR_CYAN, LV_TEXT_ALIGN_RIGHT,
                                         460, 164, 240, 32, "");
    update_duration_value_label(current_dur);

    s_detail_duration_slider = lv_slider_create(s_detail_screen);
    lv_obj_set_size(s_detail_duration_slider, 600, 24);
    lv_obj_set_pos(s_detail_duration_slider, 100, 230);
    lv_slider_set_range(s_detail_duration_slider,
                        SCREEN_DURATION_MIN_S, SCREEN_DURATION_MAX_S);
    lv_slider_set_value(s_detail_duration_slider, current_dur, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_detail_duration_slider,
                              lv_color_hex(0x232b38), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_detail_duration_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_detail_duration_slider,
                              lv_color_hex(accent_color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_detail_duration_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_detail_duration_slider,
                              lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_detail_duration_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_detail_duration_slider, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(s_detail_duration_slider, on_duration_slider_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);

    make_label(s_detail_screen, &lv_font_montserrat_24, COLOR_MUTED,
               LV_TEXT_ALIGN_LEFT, 100, 274, 200, 28, "5 s");
    make_label(s_detail_screen, &lv_font_montserrat_24, COLOR_MUTED,
               LV_TEXT_ALIGN_RIGHT, 500, 274, 200, 28, "120 s");

    make_label(s_detail_screen, &lv_font_montserrat_24, COLOR_MUTED,
               LV_TEXT_ALIGN_CENTER, 60, 340, 680, 32,
               T(COMMON_SAVED_ON_BACK));

    lv_screen_load(s_detail_screen);
}

static void on_tile_settings_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    uint8_t *index = (uint8_t *)lv_event_get_user_data(e);
    if (!index || *index >= OPTION_COUNT) return;
    open_tile_detail(*index);
}

static void update_tile(uint8_t index)
{
    if (index >= OPTION_COUNT || !s_tile[index]) return;

    const bool enabled = screen_settings_is_enabled(state_for_index(index));
    const uint32_t accent = color_for_index(index);
    lv_obj_set_style_bg_color(s_tile[index],
                              lv_color_hex(enabled ? 0x151b24 : 0x11161d), 0);
    lv_obj_set_style_border_color(s_tile[index],
                                  lv_color_hex(enabled ? accent : COLOR_DIM), 0);
    lv_obj_set_style_text_color(s_title_label[index],
                                lv_color_hex(enabled ? COLOR_TEXT : COLOR_DIM), 0);
    lv_obj_set_style_bg_color(s_underline[index],
                              lv_color_hex(enabled ? accent : COLOR_DIM), 0);
    lv_obj_set_style_text_color(s_state_label[index],
                                lv_color_hex(enabled ? accent : COLOR_MUTED), 0);
    lv_label_set_text(s_state_label[index], enabled ? T(COMMON_ACTIVE) : T(COMMON_OFF));
}

static void update_ui(void)
{
    for (uint8_t i = 0; i < OPTION_COUNT; i++) update_tile(i);

    if (!s_status_label) return;
    if (screen_settings_enabled_count() <= 1) {
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_ORANGE), 0);
        lv_label_set_text(s_status_label, T(SCREEN_ONE_REQUIRED));
    } else if (changed()) {
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_CYAN), 0);
        lv_label_set_text(s_status_label, T(COMMON_SELECTION_SAVED_ON_BACK));
    } else {
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_MUTED), 0);
        lv_label_set_text(s_status_label, T(SCREEN_KLIPPER_ONLY_ONLINE));
    }
}

static void on_tile_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    uint8_t *index = (uint8_t *)lv_event_get_user_data(e);
    if (!index || *index >= OPTION_COUNT) return;

    screen_state_t state = state_for_index(*index);
    bool enabled = screen_settings_is_enabled(state);
    if (enabled && screen_settings_enabled_count() <= 1) {
        update_ui();
        return;
    }

    screen_settings_set_enabled(state, !enabled);
    update_ui();
}

static void close_screen(void)
{
    const bool back_to_menu = s_from_settings_menu;
    lv_obj_t *ret = s_return_screen;

    screen_settings_ensure_one_enabled();
    if (changed()) {
        if (screen_settings_save()) {
            s_saved_time = s_enabled[SCREEN_TIME];
            s_saved_price = s_enabled[SCREEN_CRYPTO];
            s_saved_chart = s_enabled[SCREEN_BTC_DAY];
            s_saved_klipper = s_enabled[SCREEN_KLIPPER];
            for (int i = 0; i < OPTION_COUNT; i++) s_saved_duration[i] = s_duration[i];
            ESP_LOGI(TAG, "Screen Settings gespeichert");
        } else {
            ESP_LOGW(TAG, "Screen Settings speichern fehlgeschlagen");
        }
    }

    s_return_screen = NULL;
    s_from_settings_menu = false;
    // Erst Zielscreen laden, dann den aktiven Settings-Screen loeschen.
    if (back_to_menu) ui_settings_menu_reopen();
    else if (ret) ui_load_current_screen_no_anim();
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
                                 LV_TEXT_ALIGN_CENTER, 0, 0, 52, 52,
                                 LV_SYMBOL_LEFT);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(label);
    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
    return btn;
}

static void make_tile(lv_obj_t *parent, uint8_t index, int x, int y)
{
    lv_obj_t *tile = lv_obj_create(parent);
    s_tile[index] = tile;
    style_filled_rect(tile, 0x151b24, 8);
    lv_obj_set_size(tile, TILE_W, TILE_H);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_style_border_width(tile, 2, 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile, on_tile_clicked, LV_EVENT_CLICKED, &s_tile_data[index]);

    s_title_label[index] = make_label(tile, &lv_font_montserrat_30, COLOR_TEXT,
                                      LV_TEXT_ALIGN_CENTER, 20, 20,
                                      TILE_W - 40, 38, title_for_index(index));
    lv_obj_add_flag(s_title_label[index], LV_OBJ_FLAG_EVENT_BUBBLE);

    s_underline[index] = make_divider(tile, (TILE_W - 160) / 2, 64, 160,
                                      color_for_index(index));
    lv_obj_add_flag(s_underline[index], LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *hint = make_label(tile, &lv_font_montserrat_24, COLOR_MUTED,
                                LV_TEXT_ALIGN_LEFT, 28, 82, 160, 28,
                                hint_for_index(index));
    lv_obj_add_flag(hint, LV_OBJ_FLAG_EVENT_BUBBLE);

    s_state_label[index] = make_label(tile, &lv_font_montserrat_24, COLOR_CYAN,
                                      LV_TEXT_ALIGN_RIGHT, TILE_W - 110, 82,
                                      82, 28, "");
    lv_obj_add_flag(s_state_label[index], LV_OBJ_FLAG_EVENT_BUBBLE);

    // Schraubenschluessel-Icon-Button oben rechts. Eigener Click-Handler;
    // ohne EVENT_BUBBLE, damit der Tile-Toggle nicht mitausgeloest wird.
    lv_obj_t *gear = lv_obj_create(tile);
    s_settings_btn[index] = gear;
    lv_obj_remove_style_all(gear);
    lv_obj_set_style_bg_color(gear, lv_color_hex(0x232b38), 0);
    lv_obj_set_style_bg_opa(gear, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(gear, 6, 0);
    lv_obj_set_style_border_width(gear, 1, 0);
    lv_obj_set_style_border_color(gear, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_pad_all(gear, 0, 0);
    lv_obj_set_size(gear, 34, 34);
    lv_obj_set_pos(gear, TILE_W - 34 - 8, 8);
    lv_obj_remove_flag(gear, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gear, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(gear, on_tile_settings_clicked, LV_EVENT_CLICKED,
                        &s_tile_data[index]);

    // Icon-Source ist nativ 28x28; nicht via set_size beschneiden, sonst clippt LVGL.
    lv_obj_t *gear_icon = lv_image_create(gear);
    lv_image_set_src(gear_icon, &icon_tile_settings);
    lv_obj_center(gear_icon);
    lv_obj_remove_flag(gear_icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(gear_icon, LV_OBJ_FLAG_CLICKABLE);

    update_tile(index);
}

bool ui_screen_settings_is_open(void)
{
    return s_screen != NULL;
}

void ui_screen_settings_open(void)
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

    s_saved_time = s_enabled[SCREEN_TIME];
    s_saved_price = s_enabled[SCREEN_CRYPTO];
    s_saved_chart = s_enabled[SCREEN_BTC_DAY];
    s_saved_klipper = s_enabled[SCREEN_KLIPPER];
    for (int i = 0; i < OPTION_COUNT; i++) s_saved_duration[i] = s_duration[i];

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
               100, 32, 500, 52, T(TILE_SCREENS));
    make_back_button(s_screen);

    make_tile(s_screen, 0, TILE_X, TILE_Y);
    make_tile(s_screen, 1, TILE_X + TILE_W + TILE_GAP_X, TILE_Y);
    make_tile(s_screen, 2, TILE_X, TILE_Y + TILE_H + TILE_GAP_Y);
    make_tile(s_screen, 3, TILE_X + TILE_W + TILE_GAP_X, TILE_Y + TILE_H + TILE_GAP_Y);

    s_status_label = make_label(s_screen, &lv_font_montserrat_24, COLOR_MUTED,
                                LV_TEXT_ALIGN_CENTER, 60, 414, 680, 34, "");
    update_ui();
    lv_screen_load(s_screen);
}
