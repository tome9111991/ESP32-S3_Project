# ESP-IDF liest diese Datei auch im Script-Modus fuer Requirements ein.
# CONFIGURE_DEPENDS ist dort ungueltig, hilft aber beim normalen Configure.
if(CMAKE_SCRIPT_MODE_FILE)
    file(GLOB UI_ASSET_SRCS "ui_assets/*.c")
else()
    file(GLOB UI_ASSET_SRCS CONFIGURE_DEPENDS "ui_assets/*.c")
endif()

idf_component_register(
    SRCS
        "main.c"
        "i18n.c"
        "ui_screens.c"
        "ui_chart.c"
        "ui_popup.c"
        "ui_settings_menu.c"
        "ui_screen_settings.c"
        "ui_crypto_settings.c"
        "ui_display_settings.c"
        "ui_location_settings.c"
        "ui_weather_detail.c"
        "ui_wifi_setup.c"
        "ui_keyboard.c"
        "touch_calibration.c"
        "display_brightness.c"
        "net_fetcher.c"
        "ui_font_price_digits.cpp"
        "ui_font_time_digits.cpp"
        ${UI_ASSET_SRCS}
    INCLUDE_DIRS "."
    REQUIRES
        esp_lcd
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_ledc
        esp_timer
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_http_client
        esp-tls
        mbedtls
        espressif__cjson
        lwip
        espressif__esp_lvgl_port
        espressif__esp_lcd_touch_gt911
        lvgl__lvgl
)
