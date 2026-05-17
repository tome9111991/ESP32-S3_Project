// Kleine WLAN-Schicht fuer lokale Sonos-Abfragen.

#include "wifi_sta.h"
#include <stdint.h>
#include <stdio.h>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#if __has_include("config_private.h")
  #include "config_private.h"
#endif

#ifndef WIFI_SSID
  #define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
  #define WIFI_PASSWORD ""
#endif

#define WIFI_READY_STABLE_MS 2000

static const char *TAG = "wifi";
static volatile bool s_connected;
static volatile uint32_t s_connected_since_ms;
static bool s_started;

static const char *wifi_reason_name(uint8_t reason)
{
    // Kurze Klartexte fuer die haeufigsten Router-/Passwort-Probleme.
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
    case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
    case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";
    default: return "UNKNOWN";
    }
}

bool wifi_sta_is_connected(void)
{
    return s_connected;
}

bool wifi_sta_is_ready(void)
{
    if (!s_connected || s_connected_since_ms == 0) return false;
    // Nach GOT_IP kurz warten, bis DHCP/ARP/Router wirklich ruhig sind.
    return (uint32_t)(esp_log_timestamp() - s_connected_since_ms) >= WIFI_READY_STABLE_MS;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Verbinde mit WLAN SSID '%s'", WIFI_SSID);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)data;
        s_connected = false;
        s_connected_since_ms = 0;
        ESP_LOGW(TAG, "WLAN getrennt, reason=%u (%s), neuer Versuch",
                 event->reason, wifi_reason_name(event->reason));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        s_connected = true;
        s_connected_since_ms = (uint32_t)esp_log_timestamp();
        ESP_LOGI(TAG, "WLAN verbunden, IP=" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

bool wifi_sta_start(void)
{
    if (s_started) return true;
    if (WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "WIFI_SSID fehlt; main/config_private.h aus Example anlegen");
        return false;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t wifi_cfg = {0};
    snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), "%s", WIFI_SSID);
    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), "%s", WIFI_PASSWORD);
    // Niedrige Schwelle hilft bei WPA/WPA2-Mixed; WPA2/WPA3-Transition bleibt erlaubt.
    wifi_cfg.sta.threshold.authmode = WIFI_PASSWORD[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wifi_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_started = true;
    return true;
}
