idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd
                                esp_driver_spi
                                esp_driver_ledc
                                esp_timer
                                lvgl__lvgl
                                esp_lcd_nv3041a
                                espressif__esp_lvgl_port)
