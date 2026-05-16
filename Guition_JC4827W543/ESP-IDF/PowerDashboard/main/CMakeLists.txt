file(GLOB UI_ASSET_SRCS "ui_assets/*.c")

idf_component_register(SRCS "main.cpp"
                            "settings.c"
                            "qr_screen.cpp"
                            "provisioning.cpp"
                            ${UI_ASSET_SRCS}
                       INCLUDE_DIRS "." "ui_assets"
                       EMBED_TXTFILES "web_ui.html"
                       REQUIRES esp_lcd
                                esp_lcd_nv3041a
                                esp_driver_spi
                                esp_driver_ledc
                                esp_timer
                                esp_wifi
                                esp_netif
                                esp_event
                                esp_http_client
                                esp_http_server
                                nvs_flash
                                lwip
                                lvgl__lvgl
                                espressif__cjson
                                espressif__qrcode
                                espressif__mdns)
