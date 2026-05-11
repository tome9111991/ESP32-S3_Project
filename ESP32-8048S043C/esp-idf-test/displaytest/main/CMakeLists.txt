idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd
                                esp_driver_gpio
                                esp_driver_i2c
                                esp_timer
                                espressif__esp_lvgl_port
                                espressif__esp_lcd_touch_gt911
                                lvgl__lvgl)
