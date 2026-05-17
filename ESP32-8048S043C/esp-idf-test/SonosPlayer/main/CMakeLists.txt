file(GLOB UI_ASSET_SRCS "ui_assets/*.c")

idf_component_register(SRCS "main.c"
                            "board_8048s043c.c"
                            "wifi_sta.c"
                            "sonos_client.c"
                            "ui_sonos.c"
                            "ui_queue.c"
                            ${UI_ASSET_SRCS}
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd
                                esp_driver_gpio
                                esp_driver_i2c
                                esp_event
                                esp_http_client
                                esp_netif
                                esp_timer
                                esp_wifi
                                espressif__esp_lvgl_port
                                espressif__esp_lcd_touch_gt911
                                nvs_flash
                                lvgl__lvgl)
