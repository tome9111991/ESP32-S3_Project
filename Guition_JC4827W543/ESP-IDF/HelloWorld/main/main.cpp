// Guition JC4827W543 HelloWorld
// NV3041A ueber esp_lcd (QSPI) + esp_lvgl_port + LVGL v9, Fullscreen-Buffer in PSRAM.

#include <stdint.h>
#include <stdio.h>

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_nv3041a.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "guition_hello";

static constexpr int LCD_H_RES = 480;
static constexpr int LCD_V_RES = 272;
static constexpr int LCD_BITS_PER_PIXEL = 16;

static constexpr int PIN_LCD_SCLK = 47;
static constexpr int PIN_LCD_IO0  = 21;
static constexpr int PIN_LCD_IO1  = 48;
static constexpr int PIN_LCD_IO2  = 40;
static constexpr int PIN_LCD_IO3  = 39;
static constexpr int PIN_LCD_CS   = 45;
static constexpr int PIN_LCD_RST  = 4;
static constexpr int PIN_LCD_BL   = 1;

static constexpr int LCD_PIXEL_CLOCK_HZ = 32 * 1000 * 1000;
static constexpr spi_host_device_t LCD_HOST = SPI3_HOST;
static constexpr ledc_timer_bit_t LCD_BL_DUTY_RES = LEDC_TIMER_10_BIT;
static constexpr uint32_t LCD_BL_DUTY = 640;  // ~62 % von 1024, entspricht dem alten 160/255

static lv_obj_t *uptime_label = nullptr;
static lv_obj_t *test_panel = nullptr;
static lv_obj_t *test_phase_label = nullptr;
static uint32_t test_phase = 0;
static esp_lcd_panel_handle_t s_panel_handle = nullptr;
static constexpr uint32_t TEST_PHASE_COUNT = 10;
static constexpr uint32_t TEST_PHASE_MS = 2000;
static constexpr uint32_t TEST_START_DELAY_MS = 10000;
static constexpr uint32_t LCD_BL_DUTY_DIM    = 80;    // ~8 %
static constexpr uint32_t LCD_BL_DUTY_BRIGHT = 1023;  // ~100 %

static void backlight_init(void)
{
    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode      = LEDC_LOW_SPEED_MODE;
    timer_cfg.duty_resolution = LCD_BL_DUTY_RES;
    timer_cfg.timer_num       = LEDC_TIMER_0;
    timer_cfg.freq_hz         = 5000;
    timer_cfg.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {};
    ch_cfg.gpio_num   = PIN_LCD_BL;
    ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_cfg.channel    = LEDC_CHANNEL_0;
    ch_cfg.timer_sel  = LEDC_TIMER_0;
    ch_cfg.duty       = LCD_BL_DUTY;
    ch_cfg.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
}

static void lcd_init(esp_lcd_panel_io_handle_t *io_out,
                     esp_lcd_panel_handle_t *panel_out)
{
    ESP_LOGI(TAG, "Init QSPI bus for NV3041A");
    spi_bus_config_t bus_cfg = {};
    bus_cfg.sclk_io_num     = PIN_LCD_SCLK;
    bus_cfg.mosi_io_num     = GPIO_NUM_NC;
    bus_cfg.miso_io_num     = GPIO_NUM_NC;
    bus_cfg.quadwp_io_num   = GPIO_NUM_NC;
    bus_cfg.quadhd_io_num   = GPIO_NUM_NC;
    bus_cfg.data0_io_num    = PIN_LCD_IO0;
    bus_cfg.data1_io_num    = PIN_LCD_IO1;
    bus_cfg.data2_io_num    = PIN_LCD_IO2;
    bus_cfg.data3_io_num    = PIN_LCD_IO3;
    bus_cfg.data4_io_num    = GPIO_NUM_NC;
    bus_cfg.data5_io_num    = GPIO_NUM_NC;
    bus_cfg.data6_io_num    = GPIO_NUM_NC;
    bus_cfg.data7_io_num    = GPIO_NUM_NC;
    bus_cfg.max_transfer_sz = LCD_H_RES * LCD_V_RES * (LCD_BITS_PER_PIXEL / 8);
    bus_cfg.flags           = SPICOMMON_BUSFLAG_QUAD;
    ESP_LOGI(TAG, "spi_bus_initialize host=%d sclk=%d io0=%d io1=%d io2=%d io3=%d",
             LCD_HOST, PIN_LCD_SCLK, PIN_LCD_IO0, PIN_LCD_IO1, PIN_LCD_IO2, PIN_LCD_IO3);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install QSPI panel IO");
    esp_lcd_panel_io_spi_config_t io_cfg = NV3041A_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, nullptr, nullptr);
    io_cfg.pclk_hz = LCD_PIXEL_CLOCK_HZ;
    esp_lcd_panel_io_handle_t io_handle = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(LCD_HOST),
                                             &io_cfg, &io_handle));

    ESP_LOGI(TAG, "Install NV3041A panel driver");
    nv3041a_vendor_config_t vendor_cfg = {};
    vendor_cfg.flags.use_qspi_interface = 1;

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = static_cast<gpio_num_t>(PIN_LCD_RST);
    panel_cfg.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.bits_per_pixel = LCD_BITS_PER_PIXEL;
    panel_cfg.vendor_config  = &vendor_cfg;

    esp_lcd_panel_handle_t panel_handle = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_nv3041a(io_handle, &panel_cfg, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    // Entspricht dem alten LovyanGFX setRotation(2): Panel auf JC4827W543 ist um 180°
    // verbaut, daher beide Achsen spiegeln (MADCTL MX|MY).
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    *io_out    = io_handle;
    *panel_out = panel_handle;
}

static void lvgl_setup(esp_lcd_panel_io_handle_t io_handle,
                       esp_lcd_panel_handle_t panel_handle)
{
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle    = io_handle;
    disp_cfg.panel_handle = panel_handle;
    disp_cfg.buffer_size  = LCD_H_RES * LCD_V_RES;     // Fullscreen, in Pixeln
    disp_cfg.double_buffer = false;
    disp_cfg.hres         = LCD_H_RES;
    disp_cfg.vres         = LCD_V_RES;
    disp_cfg.monochrome   = false;
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
    disp_cfg.flags.buff_dma     = false;
    disp_cfg.flags.buff_spiram  = true;
    disp_cfg.flags.swap_bytes   = true;  // ersetzt das alte LV_COLOR_FORMAT_RGB565_SWAPPED

    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (disp == nullptr) {
        ESP_LOGE(TAG, "lvgl_port_add_disp failed");
        abort();
    }
}

static void uptime_timer_cb(lv_timer_t *timer)
{
    static uint32_t sec = 0;
    (void)timer;

    sec++;
    if (uptime_label == nullptr) {
        return;
    }
    lv_label_set_text_fmt(uptime_label, "Uptime %02lu:%02lu:%02lu",
                          (unsigned long)(sec / 3600),
                          (unsigned long)((sec / 60) % 60),
                          (unsigned long)(sec % 60));
}

static void backlight_set_duty(uint32_t duty)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static lv_obj_t *test_make_block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    return o;
}

static void test_apply_pattern(uint32_t phase)
{
    lv_obj_clean(test_panel);
    test_phase_label = nullptr;

    // Each phase enters with the board's default state. Phases that change
    // brightness or orientation override below; the next phase tick restores.
    backlight_set_duty(LCD_BL_DUTY);
    if (s_panel_handle != nullptr) {
        esp_lcd_panel_mirror(s_panel_handle, true, true);
    }

    lv_obj_set_style_bg_opa(test_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(test_panel, LV_GRAD_DIR_NONE, 0);

    const char *name = "";

    switch (phase) {
    case 0:
        lv_obj_set_style_bg_color(test_panel, lv_color_hex(0xFF0000), 0);
        name = "RED";
        break;
    case 1:
        lv_obj_set_style_bg_color(test_panel, lv_color_hex(0x00FF00), 0);
        name = "GREEN";
        break;
    case 2:
        lv_obj_set_style_bg_color(test_panel, lv_color_hex(0x0000FF), 0);
        name = "BLUE";
        break;
    case 3:
        lv_obj_set_style_bg_color(test_panel, lv_color_hex(0xFFFFFF), 0);
        name = "WHITE";
        break;
    case 4: {
        lv_obj_set_style_bg_color(test_panel, lv_color_hex(0x000000), 0);
        const int bar_h = LCD_V_RES / 3;
        const uint32_t grad_colors[3] = {0xFF0000, 0x00FF00, 0x0000FF};
        for (int i = 0; i < 3; i++) {
            lv_obj_t *bar = lv_obj_create(test_panel);
            lv_obj_remove_style_all(bar);
            lv_obj_set_pos(bar, 0, i * bar_h);
            lv_obj_set_size(bar, LCD_H_RES, (i == 2) ? (LCD_V_RES - 2 * bar_h) : bar_h);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_grad_color(bar, lv_color_hex(grad_colors[i]), 0);
            lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, 0);
        }
        name = "RGB GRAD";
        break;
    }
    case 5:
        lv_obj_set_style_bg_color(test_panel, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_grad_color(test_panel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_grad_dir(test_panel, LV_GRAD_DIR_HOR, 0);
        name = "GRAY GRAD";
        break;
    case 6: {
        lv_obj_set_style_bg_color(test_panel, lv_color_hex(0x000000), 0);
        // 1-pixel corner markers to verify no edge cropping.
        test_make_block(test_panel, 0,              0,              4, 4, 0xFF0000);
        test_make_block(test_panel, LCD_H_RES - 4,  0,              4, 4, 0x00FF00);
        test_make_block(test_panel, 0,              LCD_V_RES - 4,  4, 4, 0x0000FF);
        test_make_block(test_panel, LCD_H_RES - 4,  LCD_V_RES - 4,  4, 4, 0xFFFFFF);
        // crosshair lines through the geometric centre.
        test_make_block(test_panel, LCD_H_RES / 2 - 1, 0,            2, LCD_V_RES, 0x404040);
        test_make_block(test_panel, 0,                 LCD_V_RES / 2 - 1, LCD_H_RES, 2, 0x404040);
        name = "MARKERS";
        break;
    }
    case 7:
        backlight_set_duty(LCD_BL_DUTY_DIM);
        lv_obj_set_style_bg_color(test_panel, lv_color_hex(0xFFFFFF), 0);
        name = "BL DIM";
        break;
    case 8:
        backlight_set_duty(LCD_BL_DUTY_BRIGHT);
        lv_obj_set_style_bg_color(test_panel, lv_color_hex(0xFFFFFF), 0);
        name = "BL BRIGHT";
        break;
    case 9:
        if (s_panel_handle != nullptr) {
            // Flip back to MADCTL=0x00; whole panel content (including the label)
            // must show up upside down. Next phase restores mirror(true,true).
            esp_lcd_panel_mirror(s_panel_handle, false, false);
        }
        lv_obj_set_style_bg_color(test_panel, lv_color_hex(0x101820), 0);
        test_make_block(test_panel, 0,              0,              12, 12, 0xFF0000);
        test_make_block(test_panel, LCD_H_RES - 12, 0,              12, 12, 0x00FF00);
        test_make_block(test_panel, 0,              LCD_V_RES - 12, 12, 12, 0x0000FF);
        test_make_block(test_panel, LCD_H_RES - 12, LCD_V_RES - 12, 12, 12, 0xFFFFFF);
        name = "FLIP 180";
        break;
    default:
        break;
    }

    test_phase_label = lv_label_create(test_panel);
    lv_label_set_text_fmt(test_phase_label, "%lu/%lu  %s",
                          (unsigned long)(phase + 1),
                          (unsigned long)TEST_PHASE_COUNT,
                          name);
    // Yellow text reads on every test background, including white and pure colors.
    lv_obj_set_style_text_color(test_phase_label, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_bg_color(test_phase_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(test_phase_label, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(test_phase_label, 4, 0);
    lv_obj_set_style_text_font(test_phase_label, &lv_font_montserrat_18, 0);
    lv_obj_align(test_phase_label, LV_ALIGN_TOP_LEFT, 8, 8);
}

static void test_phase_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    test_phase = (test_phase + 1) % TEST_PHASE_COUNT;
    test_apply_pattern(test_phase);
}

[[maybe_unused]] static void test_start_cb(lv_timer_t *timer)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    uptime_label = nullptr;

    test_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(test_panel);
    lv_obj_clear_flag(test_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(test_panel, 0, 0);
    lv_obj_set_size(test_panel, LCD_H_RES, LCD_V_RES);

    test_phase = 0;
    test_apply_pattern(test_phase);

    lv_timer_create(test_phase_timer_cb, TEST_PHASE_MS, nullptr);
    lv_timer_set_repeat_count(timer, 0);
}

static void build_ui(void)
{
    // esp_lvgl_port besitzt die LVGL-Task, deshalb muessen UI-Aufrufe gelockt werden.
    lvgl_port_lock(0);

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
    lv_label_set_text(info, "esp_lcd NV3041A  |  480x272  |  no touch");
    lv_obj_set_style_text_color(info, lv_color_hex(0xA9B4C0), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_18, 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, 38);

    uptime_label = lv_label_create(scr);
    lv_label_set_text(uptime_label, "Uptime 00:00:00");
    lv_obj_set_style_text_color(uptime_label, lv_color_hex(0x7A8793), 0);
    lv_obj_align(uptime_label, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_timer_create(uptime_timer_cb, 1000, nullptr);

    // Test harness stays compiled in (test_*-Funktionen). Aktivieren durch
    // Auskommentieren der naechsten Zeile.
    // lv_timer_t *test_kickoff = lv_timer_create(test_start_cb, TEST_START_DELAY_MS, nullptr); lv_timer_set_repeat_count(test_kickoff, 1);

    lvgl_port_unlock();
}

extern "C" void app_main(void)
{
    backlight_init();

    esp_lcd_panel_io_handle_t io_handle = nullptr;
    esp_lcd_panel_handle_t panel_handle = nullptr;
    lcd_init(&io_handle, &panel_handle);
    s_panel_handle = panel_handle;
    lvgl_setup(io_handle, panel_handle);

    build_ui();

    ESP_LOGI(TAG, "Running Guition JC4827W543 HelloWorld (esp_lcd)");
    // esp_lvgl_port betreibt seine eigene LVGL-Task; app_main muss nichts mehr tun.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
