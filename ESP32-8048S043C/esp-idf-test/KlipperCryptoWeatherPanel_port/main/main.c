// ESP32-8048S043C Dashboard host.
// Board init stays here; UI, chart rendering, and network fetching live in
// separate native ESP-IDF modules.

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "app_state.h"

static const char *TAG = "dashboard";

#define LCD_PIXEL_CLOCK_HZ  (16 * 1000 * 1000)
#define UI_REFRESH_MS       1000
#define TOUCH_POLL_MS       50
#define TOUCH_MIN_GAP_MS    450
#define SCREEN_TIME_MS      30000
#define SCREEN_DEFAULT_MS   15000
#define LVGL_PARTIAL_BUFFER_ROWS 20

// Long-press oeffnet Popup-Menue (Werte gespiegelt vom Arduino-Sketch).
#define LONG_PRESS_MS              3000
#define LONG_PRESS_FEEDBACK_MS     180
#define LP_FEEDBACK_SIZE           144
#define LP_FEEDBACK_ARC_SIZE       122

app_state_t g_app = {0};

// Panel (PIN_BL wird ueber display_brightness.c verwaltet)
#define PIN_PCLK   42
#define PIN_HSYNC  39
#define PIN_VSYNC  41
#define PIN_DE     40

// Touch
#define PIN_TP_SDA 19
#define PIN_TP_SCL 20
#define PIN_TP_RST 38

// ---------- Panel ----------------------------------------------------------

static esp_lcd_panel_handle_t panel_init(void)
{
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        // Zwei FBs in PSRAM, esp_lvgl_port nutzt sie fuer tearing-freien Swap.
        .num_fbs = 2,
        // Bounce-Buffer entkoppelt die LCD-DMA vom PSRAM-Bus:
        // DMA liest aus internem SRAM, eine ISR fuellt den BB aus dem
        // aktiven PSRAM-FB nach. Verhindert Shake/Tearing bei UI-Updates,
        // weil LVGLs Writes nicht mehr mit dem DMA-Stream kollidieren.
        // 10 Zeilen * 800 px * 2 Byte = 16 KB pro BB (ping-pong = 32 KB SRAM).
        .bounce_buffer_size_px = LCD_H_RES * 10,
        .dma_burst_size = 64,
        .disp_gpio_num = -1,
        .pclk_gpio_num = PIN_PCLK,
        .vsync_gpio_num = PIN_VSYNC,
        .hsync_gpio_num = PIN_HSYNC,
        .de_gpio_num = PIN_DE,
        .data_gpio_nums = {
            8, 3, 46, 9, 1,
            5, 6, 7, 15, 16, 4,
            45, 48, 47, 21, 14,
        },
        .timings = {
            .pclk_hz = LCD_PIXEL_CLOCK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 16,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 4,
            .vsync_front_porch = 4,
            .flags = {
                .pclk_active_neg = true,
            },
        },
        .flags = {
            .fb_in_psram = true,
        },
    };

    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    return panel;
}

// ---------- Touch-Kalibrierung --------------------------------------------
// Die affine Map lebt in touch_calibration.c, damit sie per UI neu gelernt und
// in NVS gespeichert werden kann.

static void touch_process_coords(esp_lcd_touch_handle_t tp,
                                 uint16_t *x, uint16_t *y, uint16_t *strength,
                                 uint8_t  *point_num, uint8_t max_point_num)
{
    touch_calibration_process_coords(tp, x, y, strength, point_num, max_point_num);
}

// ---------- I2C + GT911 ----------------------------------------------------

static i2c_master_bus_handle_t i2c_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port   = I2C_NUM_0,
        .sda_io_num = PIN_TP_SDA,
        .scl_io_num = PIN_TP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));
    return bus;
}

static esp_lcd_touch_handle_t touch_init(i2c_master_bus_handle_t bus)
{
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &tp_io));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = PIN_TP_RST,
        .int_gpio_num = GPIO_NUM_NC,  // GPIO18 laut Notes nur mit HW-Mod brauchbar
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .process_coordinates = touch_process_coords,
    };
    esp_lcd_touch_handle_t tp = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp));
    return tp;
}

// ---------- Dashboard runtime ----------------------------------------------

static esp_lcd_touch_handle_t s_tp;
static int64_t s_last_touch_switch_us = 0;
static int64_t s_last_auto_switch_us = 0;
static bool s_touch_was_down = false;
// Beim Boot aus NVS gelesen; Toggle ueber Settings + Reboot.
static bool s_rotate_180 = (DISPLAY_ROTATE_180_DEFAULT != 0);

// Long-press State
static int64_t s_touch_press_start_us = 0;
static int16_t s_touch_start_x = 0;
static int16_t s_touch_start_y = 0;
static bool    s_long_press_handled = false;
static lv_obj_t *s_lp_root = NULL;
static lv_obj_t *s_lp_arc  = NULL;
static int       s_lp_value = -1;

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void destroy_lp_feedback(void)
{
    if (!s_lp_root) return;
    lv_obj_delete(s_lp_root);
    s_lp_root = NULL;
    s_lp_arc  = NULL;
    s_lp_value = -1;
}

static void create_lp_feedback(int16_t x, int16_t y)
{
    int half = LP_FEEDBACK_SIZE / 2;
    int px = clamp_int((int)x - half, 0, LCD_H_RES - LP_FEEDBACK_SIZE);
    int py = clamp_int((int)y - half, 0, LCD_V_RES - LP_FEEDBACK_SIZE);

    lv_obj_t *parent = lv_screen_active();
    s_lp_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_lp_root);
    lv_obj_set_size(s_lp_root, LP_FEEDBACK_SIZE, LP_FEEDBACK_SIZE);
    lv_obj_set_pos(s_lp_root, px, py);
    lv_obj_set_style_radius(s_lp_root, LP_FEEDBACK_SIZE / 2, 0);
    lv_obj_set_style_bg_color(s_lp_root, lv_color_hex(0x858c96), 0);
    lv_obj_set_style_bg_opa(s_lp_root, 72, 0);
    lv_obj_set_style_border_width(s_lp_root, 2, 0);
    lv_obj_set_style_border_color(s_lp_root, lv_color_hex(0xb8bec8), 0);
    lv_obj_set_style_border_opa(s_lp_root, 110, 0);
    lv_obj_remove_flag(s_lp_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_lp_root, LV_OBJ_FLAG_CLICKABLE);

    s_lp_arc = lv_arc_create(s_lp_root);
    lv_obj_set_size(s_lp_arc, LP_FEEDBACK_ARC_SIZE, LP_FEEDBACK_ARC_SIZE);
    lv_obj_center(s_lp_arc);
    lv_arc_set_rotation(s_lp_arc, 270);
    lv_arc_set_bg_angles(s_lp_arc, 0, 360);
    lv_arc_set_range(s_lp_arc, 0, 100);
    lv_arc_set_value(s_lp_arc, 0);
    lv_obj_remove_style(s_lp_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_lp_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_lp_arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_arc_width(s_lp_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_lp_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_lp_arc, lv_color_hex(0x4e5868), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_lp_arc, lv_color_hex(0xd7dce4), LV_PART_INDICATOR);
    lv_obj_move_foreground(s_lp_root);
}

static void update_lp_feedback(int64_t elapsed_ms)
{
    if (elapsed_ms < LONG_PRESS_FEEDBACK_MS) return;
    if (!s_lp_root) create_lp_feedback(s_touch_start_x, s_touch_start_y);
    if (!s_lp_arc) return;
    int val = (int)((elapsed_ms * 100) / LONG_PRESS_MS);
    val = clamp_int(val, 0, 100);
    if (val == s_lp_value) return;
    s_lp_value = val;
    lv_arc_set_value(s_lp_arc, val);
}

static uint32_t current_screen_interval_ms(void)
{
    return ui_current_screen() == SCREEN_TIME ? SCREEN_TIME_MS : SCREEN_DEFAULT_MS;
}

static bool wifi_is_connected(void)
{
    bool connected = false;
    app_lock();
    connected = g_app.wifi_connected;
    app_unlock();
    return connected;
}

static bool ui_control_screen_is_open(void)
{
    return ui_popup_is_open() || ui_display_settings_is_open() ||
           ui_settings_menu_is_open() || ui_wifi_setup_is_open() ||
           ui_screen_settings_is_open() || ui_crypto_settings_is_open() ||
           touch_calibration_is_open();
}

static void dashboard_refresh_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (ui_control_screen_is_open()) return;
    // Bei WLAN-Verlust sofort zurueck auf den Verbindet-Screen.
    if (!wifi_is_connected() && ui_current_screen() != SCREEN_TIME) {
        ui_switch_screen(SCREEN_TIME);
        return;
    }
    if (!ui_screen_is_available(ui_current_screen())) {
        ui_switch_screen(ui_next_screen(ui_current_screen()));
        return;
    }
    ui_refresh_current();
    if (ui_current_screen() == SCREEN_BTC_DAY) {
        ui_chart_draw();
    }
}

static void brightness_timer_cb(lv_timer_t *t)
{
    (void)t;
    display_brightness_update_by_sun();
}

static void dashboard_rotate_timer_cb(lv_timer_t *t)
{
    (void)t;
    int64_t now_us = esp_timer_get_time();
    if (!wifi_is_connected() || s_touch_was_down || ui_control_screen_is_open()) {
        s_last_auto_switch_us = now_us;
        return;
    }
    uint32_t interval_ms = current_screen_interval_ms();
    if ((now_us - s_last_auto_switch_us) / 1000 < interval_ms) {
        return;
    }
    s_last_auto_switch_us = now_us;
    ui_switch_screen(ui_next_screen(ui_current_screen()));
}

static void touch_poll_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (touch_calibration_is_open()) {
        destroy_lp_feedback();
        s_touch_was_down = false;
        s_long_press_handled = true;
        return;
    }

    esp_lcd_touch_point_data_t pts[1];
    uint8_t count = 0;
    esp_lcd_touch_read_data(s_tp);
    bool down = esp_lcd_touch_get_data(s_tp, pts, &count, 1) == ESP_OK && count > 0;
    int64_t now_us = esp_timer_get_time();

    // Wenn Popup offen ist: Routing uebernimmt LVGL ueber lvgl_port_add_touch.
    // Lokales Long-Press- und Screen-Switch-Tracking pausiert. Markieren als
    // "handled", damit ein Release (das das Popup gerade geschlossen hat) keinen
    // Screen-Wechsel ausloest.
    if (ui_popup_is_open() || ui_display_settings_is_open() || ui_settings_menu_is_open() ||
        ui_wifi_setup_is_open() || ui_screen_settings_is_open() || ui_crypto_settings_is_open()) {
        destroy_lp_feedback();
        if (down) s_long_press_handled = true;
        s_touch_was_down = down;
        return;
    }

    // pts[0] kommt aus touch_process_coords im Panel-Pixel-Raum (ohne 180-Invert).
    // Fuer Feedback-Arc und Screen-Switch brauchen wir User-Space. LVGL selber
    // bekommt die Roh-Koordinaten und macht die Rotation intern (lv_indev.c).
    int16_t ux = (int16_t)pts[0].x;
    int16_t uy = (int16_t)pts[0].y;
    if (s_rotate_180 && down) {
        ux = (int16_t)((LCD_H_RES - 1) - ux);
        uy = (int16_t)((LCD_V_RES - 1) - uy);
    }

    if (down) {
        if (!s_touch_was_down) {
            s_touch_press_start_us = now_us;
            s_touch_start_x = ux;
            s_touch_start_y = uy;
            s_long_press_handled = false;
        }
        if (!s_long_press_handled) {
            int64_t elapsed_ms = (now_us - s_touch_press_start_us) / 1000;
            update_lp_feedback(elapsed_ms);
            if (elapsed_ms >= LONG_PRESS_MS) {
                s_long_press_handled = true;
                destroy_lp_feedback();
                s_last_touch_switch_us = now_us;
                s_last_auto_switch_us  = now_us;
                ui_popup_open();
            }
        }
    } else if (s_touch_was_down) {
        destroy_lp_feedback();
        // Auf Release Screen wechseln, falls kein Long-Press ausgeloest hat.
        if (!s_long_press_handled &&
            (now_us - s_last_touch_switch_us) / 1000 >= TOUCH_MIN_GAP_MS) {
            screen_state_t current = ui_current_screen();
            screen_state_t target = s_touch_start_x < (LCD_H_RES / 2)
                ? ui_previous_screen(current)
                : ui_next_screen(current);
            ui_switch_screen(target);
            s_last_touch_switch_us = now_us;
            s_last_auto_switch_us  = now_us;
        }
        s_long_press_handled = false;
    }

    s_touch_was_down = down;
}

static void init_app_state(void)
{
    memset(&g_app, 0, sizeof(g_app));
    g_app.mutex = xSemaphoreCreateMutex();
    assert(g_app.mutex);
    g_app.candles = heap_caps_calloc(BTC_CANDLE_CAPACITY, sizeof(btc_candle_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_app.candles) {
        g_app.candles = heap_caps_calloc(BTC_CANDLE_CAPACITY, sizeof(btc_candle_t),
                                         MALLOC_CAP_8BIT);
    }

    snprintf(g_app.current_temp, sizeof(g_app.current_temp), "--");
    snprintf(g_app.weather_status, sizeof(g_app.weather_status), "WETTER: --");
    snprintf(g_app.weather_location, sizeof(g_app.weather_location), "DWD Station");
    g_app.weather_code = -1;

    snprintf(g_app.crypto_price, sizeof(g_app.crypto_price), "Laden...");
    snprintf(g_app.crypto_status, sizeof(g_app.crypto_status), "%s %s",
             g_crypto.service, g_crypto.quote);
    snprintf(g_app.btc_day_change, sizeof(g_app.btc_day_change), "%s --",
             crypto_chart_timeframe_label());
    snprintf(g_app.btc_day_time_range, sizeof(g_app.btc_day_time_range), "--");
    snprintf(g_app.btc_day_volume, sizeof(g_app.btc_day_volume), "VOL --");
    snprintf(g_app.btc_candle_status, sizeof(g_app.btc_candle_status), "CANDLE --");
    g_app.btc_day_change_positive = true;

    snprintf(g_app.klipper_connection_state, sizeof(g_app.klipper_connection_state), "--");
    snprintf(g_app.klipper_state, sizeof(g_app.klipper_state), "--");
    snprintf(g_app.klipper_file, sizeof(g_app.klipper_file), "Kein Job");
    snprintf(g_app.klipper_progress, sizeof(g_app.klipper_progress), "--");
    snprintf(g_app.klipper_nozzle, sizeof(g_app.klipper_nozzle), "--");
    snprintf(g_app.klipper_bed, sizeof(g_app.klipper_bed), "--");
    snprintf(g_app.klipper_duration, sizeof(g_app.klipper_duration), "--");
    snprintf(g_app.klipper_status, sizeof(g_app.klipper_status), "KLIPPER --");
    snprintf(g_app.klipper_printer_name, sizeof(g_app.klipper_printer_name), "KLIPPER");
    snprintf(g_app.klipper_mmu_info, sizeof(g_app.klipper_mmu_info), "MMU --");
    g_app.klipper_mmu_tool = -1;
    g_app.klipper_mmu_gate = -1;
    for (int i = 0; i < MMU_GATE_MAX; i++) {
        // Defaults wie im Arduino-Sketch: keine Gate-Daten, neutrale Farbe.
        g_app.klipper_mmu_gate_colors[i] = COLOR_DIM;
        g_app.klipper_mmu_gate_status[i] = -1;
    }
}

// ---------- main -----------------------------------------------------------

void app_main(void)
{
    // NVS hier initialisieren, damit display_brightness_init() den gespeicherten
    // Tag-Wert lesen kann. net_start() ruft nvs_flash_init() ebenfalls (idempotent).
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs_err);
    }

    s_rotate_180 = display_rotate_180_load();
    crypto_settings_load();
    screen_settings_load();
    touch_calibration_set_rotation(s_rotate_180);
    touch_calibration_load();
    ESP_LOGI(TAG, "Rotate 180: %s", s_rotate_180 ? "an" : "aus");

    ESP_LOGI(TAG, "Backlight init");
    display_brightness_init();

    ESP_LOGI(TAG, "Init RGB panel");
    esp_lcd_panel_handle_t panel = panel_init();

    ESP_LOGI(TAG, "Init I2C + GT911");
    i2c_master_bus_handle_t i2c_bus = i2c_init();
    s_tp = touch_init(i2c_bus);
    touch_calibration_set_handle(s_tp);

    ESP_LOGI(TAG, "Init LVGL port");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // 180-Rotation laeuft per lv_display_set_rotation und braucht Partial-Buffer
    // ohne direct_mode/avoid_tearing. Ohne Rotation bringen direct_mode + Tearing-
    // Vermeidung mehr Performance und nutzen beide PSRAM-Framebuffer.
    const uint32_t lvgl_buffer_size  = s_rotate_180 ? (LCD_H_RES * LVGL_PARTIAL_BUFFER_ROWS)
                                                    : (LCD_H_RES * LCD_V_RES);
    const bool lvgl_direct_mode      = !s_rotate_180;
    const bool lvgl_avoid_tearing    = !s_rotate_180;

    lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel,
        .buffer_size  = lvgl_buffer_size,
        .double_buffer = false,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .direct_mode = lvgl_direct_mode,
        },
    };
    lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = lvgl_avoid_tearing,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    assert(disp);
    if (s_rotate_180) {
        if (lvgl_port_lock(0)) {
            lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);
            lvgl_port_unlock();
        }
    }

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp   = disp,
        .handle = s_tp,
    };
    lvgl_port_add_touch(&touch_cfg);

    ESP_LOGI(TAG, "Init app state");
    init_app_state();

    ESP_LOGI(TAG, "Build dashboard UI");
    if (lvgl_port_lock(0)) {
        ui_init();
        lv_timer_create(dashboard_refresh_timer_cb, UI_REFRESH_MS, NULL);
        lv_timer_create(dashboard_rotate_timer_cb, 1000, NULL);
        lv_timer_create(touch_poll_timer_cb, TOUCH_POLL_MS, NULL);
        // Tag/Nacht-Helligkeit alle 30 s neu auswerten (1:1 wie Arduino-Sketch).
        lv_timer_create(brightness_timer_cb, 30000, NULL);
        lvgl_port_unlock();
    }

    net_start();
    s_last_auto_switch_us = esp_timer_get_time();

    ESP_LOGI(TAG, "Running.");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
