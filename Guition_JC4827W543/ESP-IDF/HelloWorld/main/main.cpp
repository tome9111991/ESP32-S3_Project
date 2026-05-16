// Guition JC4827W543 HelloWorld
// NV3041A ueber LovyanGFX/QSPI + LVGL v9 mit Fullscreen-Buffer in PSRAM.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

// LovyanGFX soll LVGL v9 aus dem Projekt verwenden und keine alten LVGL-Typen liefern.
#define M5GFX_USING_REAL_LVGL
#define M5GFX_LVGL_FONT_COMPAT_H
#define M5GFX_LVGL_COLOR_H
#define M5GFX_LVGL_AREA_H
#define M5GFX_LVGL_FONT_H
#define M5GFX_LVGL_DRAW_BUF_H
#define M5GFX_LVGL_FONT_FMT_TXT_H

#include <LovyanGFX.hpp>
#include <lgfx/v1/panel/Panel_NV3041A.hpp>

static const char *TAG = "guition_hello";

static constexpr int LCD_H_RES = 480;
static constexpr int LCD_V_RES = 272;
static constexpr uint8_t LCD_BRIGHTNESS = 160;

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_NV3041A _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;

public:
    LGFX()
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI3_HOST;
            cfg.spi_mode = 1;
            cfg.freq_write = 32000000UL;
            cfg.freq_read = 16000000UL;
            cfg.spi_3wire = true;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 47;
            cfg.pin_io0 = 21;
            cfg.pin_io1 = 48;
            cfg.pin_io2 = 40;
            cfg.pin_io3 = 39;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }

        {
            auto cfg = _panel.config();
            cfg.pin_cs = 45;
            cfg.pin_rst = 4;
            cfg.pin_busy = -1;
            cfg.panel_width = LCD_H_RES;
            cfg.panel_height = LCD_V_RES;
            cfg.memory_width = LCD_H_RES;
            cfg.memory_height = LCD_V_RES;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = true;
            cfg.invert = true;
            cfg.rgb_order = true;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
            _panel.config(cfg);
        }

        {
            auto cfg = _light.config();
            cfg.pin_bl = 1;
            cfg.invert = false;
            _light.config(cfg);
            _panel.setLight(&_light);
        }

        setPanel(&_panel);
    }
};

static LGFX display;
static lv_color_t *draw_buf = nullptr;
static lv_obj_t *uptime_label = nullptr;

static uint32_t lv_tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void lv_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;

    if (x2 < 0 || y2 < 0 || x1 >= display.width() || y1 >= display.height()) {
        lv_display_flush_ready(disp);
        return;
    }

    const int32_t src_width = lv_area_get_width(area);
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= display.width()) x2 = display.width() - 1;
    if (y2 >= display.height()) y2 = display.height() - 1;

    const uint32_t w = x2 - x1 + 1;
    const uint32_t h = y2 - y1 + 1;
    const lgfx::swap565_t *pixels = reinterpret_cast<const lgfx::swap565_t *>(px_map)
        + ((y1 - area->y1) * src_width)
        + (x1 - area->x1);
    const bool contiguous = (src_width == (int32_t)w);
    const bool started_write = (display.getStartCount() == 0);

    // Diese Reihenfolge ist fuer das NV3041A/QSPI-Panel stabiler als Partial-DMA.
    display.waitDMA();
    if (started_write) {
        display.startWrite();
    }

    if (contiguous) {
        display.pushImage(x1, y1, w, h, pixels);
    } else {
        for (uint32_t row = 0; row < h; row++) {
            display.pushImage(x1, y1 + row, w, 1, pixels + (row * src_width));
        }
    }

    if (started_write) {
        display.endWrite();
    }
    display.waitDMA();
    lv_display_flush_ready(disp);
}

static void init_lgfx(void)
{
    ESP_LOGI(TAG, "Init LovyanGFX NV3041A");
    if (!display.init()) {
        ESP_LOGE(TAG, "LovyanGFX init fehlgeschlagen");
        abort();
    }
    display.initDMA();
    display.setColorDepth(16);
    display.invertDisplay(true);
    display.setRotation(2);
    display.setBrightness(LCD_BRIGHTNESS);
}

static void init_lvgl(void)
{
    lv_init();
    lv_tick_set_cb(lv_tick_ms);

    const uint32_t buffer_bytes = LCD_H_RES * LCD_V_RES * sizeof(lv_color_t);
    draw_buf = static_cast<lv_color_t *>(
        heap_caps_malloc(buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (draw_buf == nullptr) {
        ESP_LOGE(TAG, "LVGL Fullscreen-Buffer in PSRAM fehlgeschlagen");
        abort();
    }

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_flush_cb(disp, lv_flush);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_buffers(disp, draw_buf, nullptr, buffer_bytes, LV_DISPLAY_RENDER_MODE_FULL);
}

static void uptime_timer_cb(lv_timer_t *timer)
{
    static uint32_t sec = 0;
    (void)timer;

    sec++;
    lv_label_set_text_fmt(uptime_label, "Uptime %02lu:%02lu:%02lu",
                          (unsigned long)(sec / 3600),
                          (unsigned long)((sec / 60) % 60),
                          (unsigned long)(sec % 60));
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x071018), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Guition JC4827W543");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);

    lv_obj_t *hello = lv_label_create(scr);
    lv_label_set_text(hello, "HelloWorld");
    lv_obj_set_style_text_color(hello, lv_color_hex(0x2EE59D), 0);
    lv_obj_set_style_text_font(hello, &lv_font_montserrat_48, 0);
    lv_obj_align(hello, LV_ALIGN_CENTER, 0, -22);

    lv_obj_t *info = lv_label_create(scr);
    lv_label_set_text(info, "LovyanGFX NV3041A  |  480x272  |  no touch");
    lv_obj_set_style_text_color(info, lv_color_hex(0xA9B4C0), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_18, 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, 38);

    uptime_label = lv_label_create(scr);
    lv_label_set_text(uptime_label, "Uptime 00:00:00");
    lv_obj_set_style_text_color(uptime_label, lv_color_hex(0x7A8793), 0);
    lv_obj_align(uptime_label, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_timer_create(uptime_timer_cb, 1000, nullptr);
}

extern "C" void app_main(void)
{
    init_lgfx();
    init_lvgl();
    build_ui();

    ESP_LOGI(TAG, "Running Guition JC4827W543 HelloWorld");
    while (true) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
