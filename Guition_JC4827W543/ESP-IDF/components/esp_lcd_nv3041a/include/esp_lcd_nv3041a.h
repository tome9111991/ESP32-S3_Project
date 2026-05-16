/*
 * Minimal NV3041A QSPI panel driver for esp_lcd.
 *
 * The command framing matches the JC4827W543/LovyanGFX bring-up path:
 * 0x02 0x00 CMD 0x00 for register writes, 0x32 0x00 0x2c 0x00 for RAMWR.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NV3041A_QSPI_WRITE_CMD(cmd)   (0x02000000U | (((uint32_t)(cmd) & 0xffU) << 8))
#define NV3041A_QSPI_WRITE_COLOR(cmd) (0x32000000U | (((uint32_t)(cmd) & 0xffU) << 8))

#define NV3041A_PANEL_IO_QSPI_CONFIG(cs_gpio, trans_done_cb, user_ctx_) \
    {                                                                  \
        .cs_gpio_num = (gpio_num_t)(cs_gpio),                          \
        .dc_gpio_num = GPIO_NUM_NC,                                    \
        .spi_mode = 1,                                                 \
        .pclk_hz = 32000000,                                           \
        .trans_queue_depth = 10,                                       \
        .on_color_trans_done = (trans_done_cb),                        \
        .user_ctx = (user_ctx_),                                       \
        .lcd_cmd_bits = 32,                                            \
        .lcd_param_bits = 8,                                           \
        .cs_ena_pretrans = 0,                                          \
        .cs_ena_posttrans = 0,                                         \
        .flags = {                                                     \
            .dc_high_on_cmd = 0,                                       \
            .dc_low_on_data = 0,                                       \
            .dc_low_on_param = 0,                                      \
            .octal_mode = 0,                                           \
            .quad_mode = 1,                                            \
            .sio_mode = 0,                                             \
            .lsb_first = 0,                                            \
            .cs_high_active = 0,                                       \
        },                                                             \
    }

typedef struct {
    const void *init_cmds;     /*!< Reserved for future custom init tables */
    uint16_t init_cmds_size;   /*!< Reserved for future custom init tables */
    struct {
        uint32_t use_qspi_interface: 1; /*!< Kept for source compatibility */
    } flags;
} nv3041a_vendor_config_t;

esp_err_t esp_lcd_new_panel_nv3041a(const esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_panel_dev_config_t *panel_dev_config,
                                    esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif
