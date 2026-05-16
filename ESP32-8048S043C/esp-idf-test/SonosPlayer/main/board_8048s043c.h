#pragma once

#include "lvgl.h"

#define BOARD_LCD_H_RES 800
#define BOARD_LCD_V_RES 480

// Initialisiert Backlight, RGB-Panel, GT911 und den LVGL-Port.
lv_display_t *board_display_init(void);
