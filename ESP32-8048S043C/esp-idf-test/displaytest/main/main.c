// ESP32-8048S043C Dashboard-Skelett
// Panel (esp_lcd) + GT911-Touch (esp_lcd_touch_gt911) + LVGL via esp_lvgl_port.
// Ein Screen mit Titel, Tap-Zaehler, Button und Live-Touch-Koordinaten.

#include <stdio.h>
#include <string.h>
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
#include "lvgl.h"

static const char *TAG = "dashboard";

#define LCD_H_RES           800
#define LCD_V_RES           480
#define LCD_PIXEL_CLOCK_HZ  (16 * 1000 * 1000)

// Panel
#define PIN_BL     2
#define PIN_PCLK   42
#define PIN_HSYNC  39
#define PIN_VSYNC  41
#define PIN_DE     40

// Touch
#define PIN_TP_SDA 19
#define PIN_TP_SCL 20
#define PIN_TP_RST 38

// ---------- Backlight + Panel ---------------------------------------------

static void backlight_init_on(void)
{
    gpio_config_t cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_BL,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(PIN_BL, 1);
}

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
// GT911 auf diesem Board liefert Rohwerte ~0..458 / ~0..249 statt 0..800/0..480.
// Affine Map aus BOARD_CODING_NOTES (2026-05-08), in Bildschirm-Pixel ueberfuehrt.

static void touch_process_coords(esp_lcd_touch_handle_t tp,
                                 uint16_t *x, uint16_t *y, uint16_t *strength,
                                 uint8_t  *point_num, uint8_t max_point_num)
{
    static const float CAL_X_RX =  1.65867031f;
    static const float CAL_X_RY = -0.02261823f;
    static const float CAL_X_C  =  2.12817001f;
    static const float CAL_Y_RX =  0.02082564f;
    static const float CAL_Y_RY =  1.79517055f;
    static const float CAL_Y_C  = 10.62223816f;

    uint8_t n = *point_num;
    if (n > max_point_num) n = max_point_num;

    for (uint8_t i = 0; i < n; i++) {
        float rx = (float)x[i];
        float ry = (float)y[i];
        int sx = (int)(CAL_X_RX * rx + CAL_X_RY * ry + CAL_X_C  + 0.5f);
        int sy = (int)(CAL_Y_RX * rx + CAL_Y_RY * ry + CAL_Y_C  + 0.5f);
        if (sx < 0) sx = 0;
        if (sx > LCD_H_RES - 1) sx = LCD_H_RES - 1;
        if (sy < 0) sy = 0;
        if (sy > LCD_V_RES - 1) sy = LCD_V_RES - 1;
        x[i] = (uint16_t)sx;
        y[i] = (uint16_t)sy;
    }
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

// ---------- UI -------------------------------------------------------------

static lv_obj_t *s_tap_label;
static lv_obj_t *s_uptime_label;
static lv_obj_t *s_touch_label;
static int s_taps = 0;

static void btn_clicked_cb(lv_event_t *e)
{
    s_taps++;
    lv_label_set_text_fmt(s_tap_label, "Taps: %d", s_taps);
}

// LVGL-Timer: ein Tick pro Sekunde, aktualisiert Uptime + Touch-Koords.
static void update_timer_cb(lv_timer_t *t)
{
    static int sec = 0;
    sec++;
    lv_label_set_text_fmt(s_uptime_label, "Uptime: %02d:%02d:%02d",
                          sec / 3600, (sec / 60) % 60, sec % 60);
}

// Touch-Anzeige live aktualisieren ueber einen kuerzeren Timer.
static esp_lcd_touch_handle_t s_tp;
static void touch_poll_timer_cb(lv_timer_t *t)
{
    esp_lcd_touch_point_data_t pts[1];
    uint8_t count = 0;
    esp_lcd_touch_read_data(s_tp);
    if (esp_lcd_touch_get_data(s_tp, pts, &count, 1) == ESP_OK && count > 0) {
        lv_label_set_text_fmt(s_touch_label, "Touch: x=%d  y=%d",
                              pts[0].x, pts[0].y);
    }
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101828), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Titel
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ESP32-8048S043C  -  Touch + LVGL");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    // Tap-Zaehler gross in der Mitte
    s_tap_label = lv_label_create(scr);
    lv_label_set_text(s_tap_label, "Taps: 0");
    lv_obj_set_style_text_color(s_tap_label, lv_color_hex(0x90EE90), 0);
    lv_obj_set_style_text_font(s_tap_label, &lv_font_montserrat_48, 0);
    lv_obj_align(s_tap_label, LV_ALIGN_CENTER, 0, -60);

    // Button
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 240, 90);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_event_cb(btn, btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_radius(btn, 12, 0);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Tap me");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_lbl);

    // Uptime unten links
    s_uptime_label = lv_label_create(scr);
    lv_label_set_text(s_uptime_label, "Uptime: 00:00:00");
    lv_obj_set_style_text_color(s_uptime_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(s_uptime_label, LV_ALIGN_BOTTOM_LEFT, 20, -16);

    // Touch-Koords unten rechts
    s_touch_label = lv_label_create(scr);
    lv_label_set_text(s_touch_label, "Touch: -");
    lv_obj_set_style_text_color(s_touch_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(s_touch_label, LV_ALIGN_BOTTOM_RIGHT, -20, -16);

    // Timer: 1 Hz fuer Uptime, 20 Hz fuer Touch-Live-Anzeige
    lv_timer_create(update_timer_cb, 1000, NULL);
    lv_timer_create(touch_poll_timer_cb, 50, NULL);
}

// ---------- main -----------------------------------------------------------

void app_main(void)
{
    ESP_LOGI(TAG, "Backlight on");
    backlight_init_on();

    ESP_LOGI(TAG, "Init RGB panel");
    esp_lcd_panel_handle_t panel = panel_init();

    ESP_LOGI(TAG, "Init I2C + GT911");
    i2c_master_bus_handle_t i2c_bus = i2c_init();
    s_tp = touch_init(i2c_bus);

    ESP_LOGI(TAG, "Init LVGL port");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel,
        .buffer_size  = LCD_H_RES * LCD_V_RES,  // mit avoid_tearing intern eh auf voll gesetzt
        .double_buffer = true,
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
            .direct_mode = true,
        },
    };
    lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    assert(disp);

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp   = disp,
        .handle = s_tp,
    };
    lvgl_port_add_touch(&touch_cfg);

    ESP_LOGI(TAG, "Build UI");
    if (lvgl_port_lock(0)) {
        build_ui();
        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "Running.");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
