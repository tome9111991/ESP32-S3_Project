set(EEZ_UI_SRCS
    "../ui/ui.c"
    "../ui/screens.c"
    "../ui/styles.c"
    "../ui/ui_font_bahn196.c"
)

set(DASHBOARD_ASSET_SRCS
    "../ui_assets/icon_status_day_line.c"
    "../ui_assets/icon_status_night_line.c"
    "../ui_assets/icon_status_offline.c"
    "../ui_assets/icon_weather_clear.c"
    "../ui_assets/icon_weather_cloudy.c"
    "../ui_assets/icon_weather_fog.c"
    "../ui_assets/icon_weather_partly_cloudy.c"
    "../ui_assets/icon_weather_rain.c"
    "../ui_assets/icon_weather_snow.c"
    "../ui_assets/icon_weather_thunder.c"
)

idf_component_register(SRCS "main.c"
                            ${EEZ_UI_SRCS}
                            ${DASHBOARD_ASSET_SRCS}
                       INCLUDE_DIRS "."
                                    "../ui"
                                    "../ui_assets"
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
                                lvgl__lvgl
                                nvs_flash)
