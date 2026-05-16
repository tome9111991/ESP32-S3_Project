/*
 * Minimal NV3041A QSPI panel driver for esp_lcd.
 *
 * Init values are the tested NV3041A 480x272 sequence used by the existing
 * JC4827W543 Arduino/LovyanGFX sketches in this repository.
 */

#include <stdlib.h>
#include <sys/cdefs.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_nv3041a.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define NV3041A_CMD(cmd)       NV3041A_QSPI_WRITE_CMD(cmd)
#define NV3041A_RAMWR_QSPI_CMD NV3041A_QSPI_WRITE_COLOR(LCD_CMD_RAMWR)
#define NV3041A_MADCTL_RGB     0x00
#define NV3041A_MADCTL_BGR     LCD_CMD_BGR_BIT

static const char *TAG = "lcd_panel.nv3041a";

typedef struct {
    uint8_t cmd;
    uint8_t data;
} nv3041a_init_cmd_t;

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    gpio_num_t reset_gpio_num;
    bool reset_level;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val;
    uint8_t colmod_val;
} nv3041a_panel_t;

static const nv3041a_init_cmd_t nv3041a_default_init_cmds[] = {
    {0xff, 0xa5}, {0x36, 0xc0}, {0x3a, 0x01}, {0x41, 0x03},
    {0x44, 0x15}, {0x45, 0x15}, {0x7d, 0x03}, {0xc1, 0xbb},
    {0xc2, 0x05}, {0xc3, 0x10}, {0xc6, 0x3e}, {0xc7, 0x25},
    {0xc8, 0x11}, {0x7a, 0x5f}, {0x6f, 0x44}, {0x78, 0x70},
    {0xc9, 0x00}, {0x67, 0x21}, {0x51, 0x0a}, {0x52, 0x76},
    {0x53, 0x0a}, {0x54, 0x76}, {0x46, 0x0a}, {0x47, 0x2a},
    {0x48, 0x0a}, {0x49, 0x1a}, {0x56, 0x43}, {0x57, 0x42},
    {0x58, 0x3c}, {0x59, 0x64}, {0x5a, 0x41}, {0x5b, 0x3c},
    {0x5c, 0x02}, {0x5d, 0x3c}, {0x5e, 0x1f}, {0x60, 0x80},
    {0x61, 0x3f}, {0x62, 0x21}, {0x63, 0x07}, {0x64, 0xe0},
    {0x65, 0x02}, {0xca, 0x20}, {0xcb, 0x52}, {0xcc, 0x10},
    {0xcd, 0x42}, {0xd0, 0x20}, {0xd1, 0x52}, {0xd2, 0x10},
    {0xd3, 0x42}, {0xd4, 0x0a}, {0xd5, 0x32}, {0x80, 0x00},
    {0xa0, 0x00}, {0x81, 0x07}, {0xa1, 0x06}, {0x82, 0x02},
    {0xa2, 0x01}, {0x86, 0x11}, {0xa6, 0x10}, {0x87, 0x27},
    {0xa7, 0x27}, {0x83, 0x37}, {0xa3, 0x37}, {0x84, 0x35},
    {0xa4, 0x35}, {0x85, 0x3f}, {0xa5, 0x3f}, {0x88, 0x0b},
    {0xa8, 0x0b}, {0x89, 0x14}, {0xa9, 0x14}, {0x8a, 0x1a},
    {0xaa, 0x1a}, {0x8b, 0x0a}, {0xab, 0x0a}, {0x8c, 0x14},
    {0xac, 0x08}, {0x8d, 0x17}, {0xad, 0x07}, {0x8e, 0x16},
    {0xae, 0x06}, {0x8f, 0x1b}, {0xaf, 0x07}, {0x90, 0x04},
    {0xb0, 0x04}, {0x91, 0x0a}, {0xb1, 0x0a}, {0x92, 0x16},
    {0xb2, 0x15}, {0xff, 0x00}, {0x11, 0x00},
};

static esp_err_t panel_nv3041a_del(esp_lcd_panel_t *panel);
static esp_err_t panel_nv3041a_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_nv3041a_init(esp_lcd_panel_t *panel);
static esp_err_t panel_nv3041a_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start,
                                           int x_end, int y_end, const void *color_data);
static esp_err_t panel_nv3041a_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_nv3041a_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_nv3041a_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_nv3041a_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_nv3041a_disp_on_off(esp_lcd_panel_t *panel, bool on_off);
static esp_err_t panel_nv3041a_sleep(esp_lcd_panel_t *panel, bool sleep);

static esp_err_t nv3041a_tx_u8(esp_lcd_panel_io_handle_t io, uint8_t cmd, uint8_t data)
{
    return esp_lcd_panel_io_tx_param(io, NV3041A_CMD(cmd), &data, 1);
}

esp_err_t esp_lcd_new_panel_nv3041a(const esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_panel_dev_config_t *panel_dev_config,
                                    esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret = ESP_OK;
    nv3041a_panel_t *nv3041a = NULL;

    ESP_GOTO_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");

    nv3041a = calloc(1, sizeof(nv3041a_panel_t));
    ESP_GOTO_ON_FALSE(nv3041a, ESP_ERR_NO_MEM, err, TAG, "no mem for nv3041a panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure reset GPIO failed");
    }

    switch (panel_dev_config->rgb_ele_order) {
    case LCD_RGB_ELEMENT_ORDER_RGB:
        nv3041a->madctl_val = NV3041A_MADCTL_RGB;
        break;
    case LCD_RGB_ELEMENT_ORDER_BGR:
        nv3041a->madctl_val = NV3041A_MADCTL_BGR;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported RGB element order");
    }

    switch (panel_dev_config->bits_per_pixel) {
    case 16:
        // NV3041A COLMOD: 0x01 = 16bpp RGB565 (matches the {0x3a,0x01} entry in the
        // init table). 0x00 leaves the panel in an undefined colour mapping and shows
        // up as a slight green tint on dark backgrounds.
        nv3041a->colmod_val = 0x01;
        nv3041a->fb_bits_per_pixel = 16;
        break;
    case 18:
        nv3041a->colmod_val = 0x01;
        nv3041a->fb_bits_per_pixel = 24;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
    }

    nv3041a->io = io;
    nv3041a->reset_gpio_num = panel_dev_config->reset_gpio_num;
    nv3041a->reset_level = panel_dev_config->flags.reset_active_high;
    nv3041a->base.del = panel_nv3041a_del;
    nv3041a->base.reset = panel_nv3041a_reset;
    nv3041a->base.init = panel_nv3041a_init;
    nv3041a->base.draw_bitmap = panel_nv3041a_draw_bitmap;
    nv3041a->base.invert_color = panel_nv3041a_invert_color;
    nv3041a->base.mirror = panel_nv3041a_mirror;
    nv3041a->base.swap_xy = panel_nv3041a_swap_xy;
    nv3041a->base.set_gap = panel_nv3041a_set_gap;
    nv3041a->base.disp_on_off = panel_nv3041a_disp_on_off;
    nv3041a->base.disp_sleep = panel_nv3041a_sleep;
    *ret_panel = &nv3041a->base;
    return ESP_OK;

err:
    if (nv3041a) {
        if (panel_dev_config && panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(nv3041a);
    }
    return ret;
}

static esp_err_t panel_nv3041a_del(esp_lcd_panel_t *panel)
{
    nv3041a_panel_t *nv3041a = __containerof(panel, nv3041a_panel_t, base);
    if (nv3041a->reset_gpio_num >= 0) {
        gpio_reset_pin(nv3041a->reset_gpio_num);
    }
    free(nv3041a);
    return ESP_OK;
}

static esp_err_t panel_nv3041a_reset(esp_lcd_panel_t *panel)
{
    nv3041a_panel_t *nv3041a = __containerof(panel, nv3041a_panel_t, base);

    if (nv3041a->reset_gpio_num >= 0) {
        gpio_set_level(nv3041a->reset_gpio_num, !nv3041a->reset_level);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(nv3041a->reset_gpio_num, nv3041a->reset_level);
        vTaskDelay(pdMS_TO_TICKS(120));
        gpio_set_level(nv3041a->reset_gpio_num, !nv3041a->reset_level);
        vTaskDelay(pdMS_TO_TICKS(120));
    } else {
        uint8_t nop = 0x00;
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(nv3041a->io, NV3041A_CMD(LCD_CMD_SWRESET), &nop, 1),
                            TAG, "software reset failed");
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    return ESP_OK;
}

static esp_err_t panel_nv3041a_init(esp_lcd_panel_t *panel)
{
    nv3041a_panel_t *nv3041a = __containerof(panel, nv3041a_panel_t, base);
    esp_lcd_panel_io_handle_t io = nv3041a->io;
    uint8_t nop = 0x00;

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, NV3041A_CMD(LCD_CMD_SWRESET), &nop, 1),
                        TAG, "software reset failed");
    vTaskDelay(pdMS_TO_TICKS(150));

    for (size_t i = 0; i < sizeof(nv3041a_default_init_cmds) / sizeof(nv3041a_default_init_cmds[0]); i++) {
        ESP_RETURN_ON_ERROR(nv3041a_tx_u8(io, nv3041a_default_init_cmds[i].cmd, nv3041a_default_init_cmds[i].data),
                            TAG, "init command failed");
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    vTaskDelay(pdMS_TO_TICKS(120));

    ESP_RETURN_ON_ERROR(nv3041a_tx_u8(io, LCD_CMD_COLMOD, nv3041a->colmod_val), TAG, "set color depth failed");
    ESP_RETURN_ON_ERROR(nv3041a_tx_u8(io, LCD_CMD_MADCTL, nv3041a->madctl_val), TAG, "set madctl failed");

    return ESP_OK;
}

static esp_err_t panel_nv3041a_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start,
                                           int x_end, int y_end, const void *color_data)
{
    nv3041a_panel_t *nv3041a = __containerof(panel, nv3041a_panel_t, base);
    esp_lcd_panel_io_handle_t io = nv3041a->io;

    x_start += nv3041a->x_gap;
    x_end += nv3041a->x_gap;
    y_start += nv3041a->y_gap;
    y_end += nv3041a->y_gap;

    uint8_t caset[] = {
        (uint8_t)((x_start >> 8) & 0xff), (uint8_t)(x_start & 0xff),
        (uint8_t)(((x_end - 1) >> 8) & 0xff), (uint8_t)((x_end - 1) & 0xff),
    };
    uint8_t raset[] = {
        (uint8_t)((y_start >> 8) & 0xff), (uint8_t)(y_start & 0xff),
        (uint8_t)(((y_end - 1) >> 8) & 0xff), (uint8_t)((y_end - 1) & 0xff),
    };

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, NV3041A_CMD(LCD_CMD_CASET), caset, sizeof(caset)),
                        TAG, "set column failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, NV3041A_CMD(LCD_CMD_RASET), raset, sizeof(raset)),
                        TAG, "set row failed");

    size_t len = (size_t)(x_end - x_start) * (size_t)(y_end - y_start) * nv3041a->fb_bits_per_pixel / 8;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(io, NV3041A_RAMWR_QSPI_CMD, color_data, len),
                        TAG, "write color failed");
    return ESP_OK;
}

static esp_err_t panel_nv3041a_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    nv3041a_panel_t *nv3041a = __containerof(panel, nv3041a_panel_t, base);
    uint8_t nop = 0x00;
    int cmd = invert_color_data ? LCD_CMD_INVON : LCD_CMD_INVOFF;
    return esp_lcd_panel_io_tx_param(nv3041a->io, NV3041A_CMD(cmd), &nop, 1);
}

static esp_err_t panel_nv3041a_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    nv3041a_panel_t *nv3041a = __containerof(panel, nv3041a_panel_t, base);
    if (mirror_x) {
        nv3041a->madctl_val |= LCD_CMD_MX_BIT;
    } else {
        nv3041a->madctl_val &= (uint8_t)~LCD_CMD_MX_BIT;
    }
    if (mirror_y) {
        nv3041a->madctl_val |= LCD_CMD_MY_BIT;
    } else {
        nv3041a->madctl_val &= (uint8_t)~LCD_CMD_MY_BIT;
    }
    return nv3041a_tx_u8(nv3041a->io, LCD_CMD_MADCTL, nv3041a->madctl_val);
}

static esp_err_t panel_nv3041a_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    nv3041a_panel_t *nv3041a = __containerof(panel, nv3041a_panel_t, base);
    if (swap_axes) {
        nv3041a->madctl_val |= LCD_CMD_MV_BIT;
    } else {
        nv3041a->madctl_val &= (uint8_t)~LCD_CMD_MV_BIT;
    }
    return nv3041a_tx_u8(nv3041a->io, LCD_CMD_MADCTL, nv3041a->madctl_val);
}

static esp_err_t panel_nv3041a_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    nv3041a_panel_t *nv3041a = __containerof(panel, nv3041a_panel_t, base);
    nv3041a->x_gap = x_gap;
    nv3041a->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_nv3041a_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    nv3041a_panel_t *nv3041a = __containerof(panel, nv3041a_panel_t, base);
    uint8_t nop = 0x00;
    int cmd = on_off ? LCD_CMD_DISPON : LCD_CMD_DISPOFF;
    return esp_lcd_panel_io_tx_param(nv3041a->io, NV3041A_CMD(cmd), &nop, 1);
}

static esp_err_t panel_nv3041a_sleep(esp_lcd_panel_t *panel, bool sleep)
{
    nv3041a_panel_t *nv3041a = __containerof(panel, nv3041a_panel_t, base);
    uint8_t nop = 0x00;
    int cmd = sleep ? LCD_CMD_SLPIN : LCD_CMD_SLPOUT;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(nv3041a->io, NV3041A_CMD(cmd), &nop, 1),
                        TAG, "sleep command failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}
