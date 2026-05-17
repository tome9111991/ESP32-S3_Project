// Board-Support fuer ESP32-8048S043C: RGB-LCD, GT911-Touch und LVGL-Port.

#include "board_8048s043c.h"
#include <assert.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "board";

#define LCD_PIXEL_CLOCK_HZ (16 * 1000 * 1000)

// Panel-Pins fuer ESP32-8048S043C.
#define PIN_BL     2
#define PIN_PCLK   42
#define PIN_HSYNC  39
#define PIN_VSYNC  41
#define PIN_DE     40

// GT911-Touch ueber I2C.
#define PIN_TP_SDA 19
#define PIN_TP_SCL 20
#define PIN_TP_RST 38

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
        .num_fbs = 2,
        // Bounce-Buffer entkoppelt LCD-DMA und PSRAM-Framebuffer.
        // 20 Zeilen statt 10 (~32 KB statt 16 KB Internal-SRAM): mehr Reserve
        // gegen PSRAM-Bus-Stalls waehrend Cover-Decode/JPEG-Download. Verhindert
        // duenne horizontale Streifen, wenn der LCD-DMA-Bounce kurz unterlaeuft.
        .bounce_buffer_size_px = BOARD_LCD_H_RES * 20,
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
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
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

static void touch_process_coords(esp_lcd_touch_handle_t tp,
                                 uint16_t *x, uint16_t *y, uint16_t *strength,
                                 uint8_t *point_num, uint8_t max_point_num)
{
    (void)tp;
    (void)strength;
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
        int sx = (int)(CAL_X_RX * rx + CAL_X_RY * ry + CAL_X_C + 0.5f);
        int sy = (int)(CAL_Y_RX * rx + CAL_Y_RY * ry + CAL_Y_C + 0.5f);
        if (sx < 0) sx = 0;
        if (sx > BOARD_LCD_H_RES - 1) sx = BOARD_LCD_H_RES - 1;
        if (sy < 0) sy = 0;
        if (sy > BOARD_LCD_V_RES - 1) sy = BOARD_LCD_V_RES - 1;
        x[i] = (uint16_t)sx;
        y[i] = (uint16_t)sy;
    }
}

static i2c_master_bus_handle_t i2c_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
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
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = PIN_TP_RST,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .process_coordinates = touch_process_coords,
    };
    esp_lcd_touch_handle_t tp = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp));
    return tp;
}

lv_display_t *board_display_init(void)
{
    ESP_LOGI(TAG, "Backlight on");
    backlight_init_on();

    ESP_LOGI(TAG, "Init RGB panel");
    esp_lcd_panel_handle_t panel = panel_init();

    ESP_LOGI(TAG, "Init I2C + GT911");
    i2c_master_bus_handle_t i2c_bus = i2c_init();
    esp_lcd_touch_handle_t tp = touch_init(i2c_bus);

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel,
        .buffer_size = BOARD_LCD_H_RES * BOARD_LCD_V_RES,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
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
        .disp = disp,
        .handle = tp,
    };
    lvgl_port_add_touch(&touch_cfg);
    return disp;
}
