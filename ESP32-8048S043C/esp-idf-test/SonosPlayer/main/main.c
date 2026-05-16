// Einstiegspunkt fuer das Sonos-Panel.
// Details liegen bewusst in getrennten Modulen: Board, WLAN, Sonos und UI.

#include "board_8048s043c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sonos_client.h"
#include "ui_sonos.h"
#include "wifi_sta.h"

static const char *TAG = "sonos_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Init Sonos state");
    sonos_service_init();

    ESP_LOGI(TAG, "Init display + touch");
    board_display_init();

    ESP_LOGI(TAG, "Build Sonos UI");
    ui_sonos_build();

    ESP_LOGI(TAG, "Start WiFi + Sonos polling");
    wifi_sta_start();
    sonos_service_start();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
