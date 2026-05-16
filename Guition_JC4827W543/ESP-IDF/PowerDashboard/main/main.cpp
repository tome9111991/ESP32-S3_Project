// Guition JC4827W543 PowerDashboard
// Shelly Plug (Gen2 RPC ueber HTTP) -> LVGL-Dashboard mit Live-Leistung,
// Heute/Session-Verbrauch und Kosten anhand des konfigurierten Tarifs.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "lvgl.h"

#define M5GFX_USING_REAL_LVGL
#define M5GFX_LVGL_FONT_COMPAT_H
#define M5GFX_LVGL_COLOR_H
#define M5GFX_LVGL_AREA_H
#define M5GFX_LVGL_FONT_H
#define M5GFX_LVGL_DRAW_BUF_H
#define M5GFX_LVGL_FONT_FMT_TXT_H

#include <LovyanGFX.hpp>
#include <lgfx/v1/panel/Panel_NV3041A.hpp>

#include "provisioning.h"
#include "qr_screen.h"
#include "settings.h"
#include "ui_assets.h"

static const char *TAG = "powerdash";

// =============================================================================
// Runtime-Config (aus NVS-Settings beim Boot gecached)
// =============================================================================

static char     g_shelly_ip[20] = "";
static uint32_t g_poll_ms       = 2000;
static float    g_tariff_ct     = 30.86f;
static char     g_tz[64]        = "CET-1CEST,M3.5.0,M10.5.0/3";
static char     g_ntp[64]       = "pool.ntp.org";
static uint8_t  g_rotation      = 2;
static uint8_t  g_brightness    = 160;

static void load_runtime_config(void)
{
    settings_get_str(SETTING_SHELLY_IP, g_shelly_ip, sizeof(g_shelly_ip));
    g_poll_ms      = (uint32_t)settings_get_int(SETTING_SHELLY_POLL);
    g_tariff_ct    = settings_get_float(SETTING_TARIFF);
    settings_get_str(SETTING_TZ,  g_tz,  sizeof(g_tz));
    settings_get_str(SETTING_NTP, g_ntp, sizeof(g_ntp));
    g_rotation     = (uint8_t)settings_get_int(SETTING_ROTATION);
    g_brightness   = (uint8_t)settings_get_int(SETTING_BRIGHTNESS);
    ESP_LOGI(TAG, "Config: shelly=%s poll=%lu tariff=%.2f rot=%u brt=%u",
             g_shelly_ip, (unsigned long)g_poll_ms, (double)g_tariff_ct,
             g_rotation, g_brightness);
}

// =============================================================================
// Display + LVGL
// =============================================================================

static constexpr int LCD_H_RES = 480;
static constexpr int LCD_V_RES = 272;

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_NV3041A _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;

public:
    LGFX()
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI3_HOST;
            cfg.spi_mode = 1;
            cfg.freq_write = 32000000UL;
            cfg.freq_read = 16000000UL;
            cfg.spi_3wire = true;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 47;
            cfg.pin_io0 = 21;
            cfg.pin_io1 = 48;
            cfg.pin_io2 = 40;
            cfg.pin_io3 = 39;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = 45;
            cfg.pin_rst = 4;
            cfg.pin_busy = -1;
            cfg.panel_width = LCD_H_RES;
            cfg.panel_height = LCD_V_RES;
            cfg.memory_width = LCD_H_RES;
            cfg.memory_height = LCD_V_RES;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = true;
            cfg.invert = true;
            cfg.rgb_order = true;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = 1;
            cfg.invert = false;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};

static LGFX display;
static lv_color_t *draw_buf = nullptr;

static uint32_t lv_tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void lv_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    if (x2 < 0 || y2 < 0 || x1 >= display.width() || y1 >= display.height()) {
        lv_display_flush_ready(disp);
        return;
    }
    const int32_t src_width = lv_area_get_width(area);
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= display.width()) x2 = display.width() - 1;
    if (y2 >= display.height()) y2 = display.height() - 1;
    const uint32_t w = x2 - x1 + 1;
    const uint32_t h = y2 - y1 + 1;
    const lgfx::swap565_t *pixels = reinterpret_cast<const lgfx::swap565_t *>(px_map)
        + ((y1 - area->y1) * src_width) + (x1 - area->x1);
    const bool contiguous = (src_width == (int32_t)w);
    const bool started_write = (display.getStartCount() == 0);
    display.waitDMA();
    if (started_write) display.startWrite();
    if (contiguous) {
        display.pushImage(x1, y1, w, h, pixels);
    } else {
        for (uint32_t row = 0; row < h; row++) {
            display.pushImage(x1, y1 + row, w, 1, pixels + (row * src_width));
        }
    }
    if (started_write) display.endWrite();
    display.waitDMA();
    lv_display_flush_ready(disp);
}

static void init_lgfx(void)
{
    if (!display.init()) {
        ESP_LOGE(TAG, "LovyanGFX init fehlgeschlagen");
        abort();
    }
    display.initDMA();
    display.setColorDepth(16);
    display.invertDisplay(true);
    display.setRotation(g_rotation);
    display.setBrightness(g_brightness);
}

static void init_lvgl(void)
{
    lv_init();
    lv_tick_set_cb(lv_tick_ms);
    const uint32_t buffer_bytes = LCD_H_RES * LCD_V_RES * sizeof(lv_color_t);
    draw_buf = static_cast<lv_color_t *>(
        heap_caps_malloc(buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (draw_buf == nullptr) {
        ESP_LOGE(TAG, "LVGL Fullscreen-Buffer in PSRAM fehlgeschlagen");
        abort();
    }
    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_flush_cb(disp, lv_flush);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_buffers(disp, draw_buf, nullptr, buffer_bytes,
                           LV_DISPLAY_RENDER_MODE_FULL);
}

// =============================================================================
// Shared State
// =============================================================================

struct PlugState {
    bool data_valid = false;
    bool wifi_connected = false;
    bool sntp_ready = false;
    float power_w = 0.0f;
    float voltage_v = 0.0f;
    float current_a = 0.0f;
    float temp_c = 0.0f;
    double total_wh = 0.0;          // kumulativ vom Plug
    double session_start_wh = -1.0; // erster Messwert nach Boot
    double today_start_wh = -1.0;   // erster Messwert nach Tageswechsel
    int today_yday = -1;            // localtime tm_yday des letzten Updates
    uint32_t fail_count = 0;
    uint32_t poll_count = 0;
    int64_t last_ok_us = 0;
    char ip_str[16] = "---";
    int8_t wifi_rssi = 0;           // 0 = unbekannt, sonst dBm (negativ)
};

static PlugState g_state;
static SemaphoreHandle_t g_state_mutex = nullptr;

static inline void state_lock()   { xSemaphoreTake(g_state_mutex, portMAX_DELAY); }
static inline void state_unlock() { xSemaphoreGive(g_state_mutex); }

// =============================================================================
// WiFi + SNTP
// =============================================================================

static EventGroupHandle_t g_wifi_events = nullptr;
static const int WIFI_CONNECTED_BIT = BIT0;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id,
                               void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        state_lock();
        g_state.wifi_connected = false;
        g_state.wifi_rssi = 0;
        strcpy(g_state.ip_str, "---");
        state_unlock();
        xEventGroupClearBits(g_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "WiFi disconnected, reconnect");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        state_lock();
        g_state.wifi_connected = true;
        snprintf(g_state.ip_str, sizeof(g_state.ip_str), IPSTR,
                 IP2STR(&evt->ip_info.ip));
        state_unlock();
        xEventGroupSetBits(g_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Got IP " IPSTR, IP2STR(&evt->ip_info.ip));
    }
}

static void wifi_init_sta(void)
{
    g_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    // Hostname fuer DHCP/Fritzbox-Geraeteliste setzen.
    ESP_ERROR_CHECK(esp_netif_set_hostname(sta_netif, "powerdashboard"));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr));

    char ssid[33], pass[65];
    settings_get_str(SETTING_WIFI_SSID, ssid, sizeof(ssid));
    settings_get_str(SETTING_WIFI_PASS, pass, sizeof(pass));

    wifi_config_t wifi_cfg = {};
    memcpy(wifi_cfg.sta.ssid,     ssid,
           strnlen(ssid, sizeof(wifi_cfg.sta.ssid)));
    memcpy(wifi_cfg.sta.password, pass,
           strnlen(pass, sizeof(wifi_cfg.sta.password)));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void sntp_init_once(void)
{
    if (esp_sntp_enabled()) return;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, g_ntp);
    esp_sntp_init();
    setenv("TZ", g_tz, 1);
    tzset();
}

static void mdns_start_http_service(void)
{
    // Webinterface im LAN als http://powerdashboard.local/ bekannt machen.
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS init fehlgeschlagen: %s", esp_err_to_name(err));
        return;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(mdns_hostname_set("powerdashboard"));
    ESP_ERROR_CHECK_WITHOUT_ABORT(mdns_instance_name_set("PowerDashboard"));

    mdns_txt_item_t txt[] = {
        { "path", "/" },
    };
    err = mdns_service_add("PowerDashboard Web", "_http", "_tcp", 80,
                           txt, sizeof(txt) / sizeof(txt[0]));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "mDNS aktiv: http://powerdashboard.local/");
    } else {
        ESP_LOGW(TAG, "mDNS HTTP-Service fehlgeschlagen: %s",
                 esp_err_to_name(err));
    }
}

// =============================================================================
// Shelly Gen2 RPC Polling
// =============================================================================

struct HttpBuf {
    char *data;
    int len;
    int cap;
};

static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        HttpBuf *buf = (HttpBuf *)evt->user_data;
        if (buf->len + evt->data_len + 1 > buf->cap) return ESP_OK;
        memcpy(buf->data + buf->len, evt->data, evt->data_len);
        buf->len += evt->data_len;
        buf->data[buf->len] = '\0';
    }
    return ESP_OK;
}

static bool fetch_shelly_status(char *body, int cap)
{
    HttpBuf buf = { body, 0, cap };
    body[0] = '\0';

    char url[128];
    snprintf(url, sizeof(url),
             "http://%s/rpc/Switch.GetStatus?id=0", g_shelly_ip);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 3000;
    cfg.event_handler = http_event_cb;
    cfg.user_data = &buf;
    cfg.disable_auto_redirect = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "Shelly HTTP err=%d status=%d", err, status);
        return false;
    }
    return buf.len > 0;
}

static bool parse_shelly_status(const char *json, float &power, float &voltage,
                                float &current, float &temp, double &total_wh)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    bool ok = true;

    cJSON *p = cJSON_GetObjectItemCaseSensitive(root, "apower");
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "voltage");
    cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "current");
    cJSON *ae = cJSON_GetObjectItemCaseSensitive(root, "aenergy");
    cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "temperature");

    if (cJSON_IsNumber(p))      power   = (float)p->valuedouble; else ok = false;
    if (cJSON_IsNumber(v))      voltage = (float)v->valuedouble;
    if (cJSON_IsNumber(c))      current = (float)c->valuedouble;
    if (cJSON_IsObject(ae)) {
        cJSON *tot = cJSON_GetObjectItemCaseSensitive(ae, "total");
        if (cJSON_IsNumber(tot)) total_wh = tot->valuedouble; else ok = false;
    } else {
        ok = false;
    }
    if (cJSON_IsObject(t)) {
        cJSON *tc = cJSON_GetObjectItemCaseSensitive(t, "tC");
        if (cJSON_IsNumber(tc)) temp = (float)tc->valuedouble;
    }
    cJSON_Delete(root);
    return ok;
}

static void poll_task(void *arg)
{
    (void)arg;
    char *body = (char *)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) {
        ESP_LOGE(TAG, "Kein PSRAM fuer HTTP-Body");
        vTaskDelete(nullptr);
    }

    xEventGroupWaitBits(g_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                        portMAX_DELAY);
    sntp_init_once();

    while (true) {
        g_state.poll_count++;

        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            state_lock();
            g_state.wifi_rssi = ap.rssi;
            state_unlock();
        }
        if (!fetch_shelly_status(body, 4096)) {
            state_lock();
            g_state.fail_count++;
            state_unlock();
            vTaskDelay(pdMS_TO_TICKS(g_poll_ms));
            continue;
        }
        float power = 0, voltage = 0, current = 0, temp = 0;
        double total_wh = 0;
        if (!parse_shelly_status(body, power, voltage, current, temp, total_wh)) {
            ESP_LOGW(TAG, "JSON parse fehlgeschlagen");
            state_lock();
            g_state.fail_count++;
            state_unlock();
            vTaskDelay(pdMS_TO_TICKS(g_poll_ms));
            continue;
        }

        time_t now = time(nullptr);
        struct tm lt;
        localtime_r(&now, &lt);
        bool have_time = (now > 1700000000); // SNTP geliefert wenn Jahr >= 2023

        state_lock();
        g_state.data_valid = true;
        g_state.power_w = power;
        g_state.voltage_v = voltage;
        g_state.current_a = current;
        g_state.temp_c = temp;
        g_state.total_wh = total_wh;
        g_state.last_ok_us = esp_timer_get_time();
        g_state.sntp_ready = have_time;

        // Session-Referenz beim ersten gueltigen Wert setzen.
        if (g_state.session_start_wh < 0 || total_wh < g_state.session_start_wh) {
            g_state.session_start_wh = total_wh;
        }
        // Tages-Referenz bei Tageswechsel (oder erstem Lauf mit gueltiger Zeit)
        // setzen. Vorher schon Wert merken, damit Tag 1 wenigstens "seit Boot"
        // zaehlt.
        if (have_time) {
            if (g_state.today_yday != lt.tm_yday ||
                g_state.today_start_wh < 0 ||
                total_wh < g_state.today_start_wh) {
                g_state.today_start_wh = total_wh;
                g_state.today_yday = lt.tm_yday;
            }
        } else {
            if (g_state.today_start_wh < 0) {
                g_state.today_start_wh = total_wh;
            }
        }
        state_unlock();

        vTaskDelay(pdMS_TO_TICKS(g_poll_ms));
    }
}

// =============================================================================
// UI
// =============================================================================

static lv_obj_t *lbl_title;
static lv_obj_t *lbl_ip;
static lv_obj_t *img_wifi;
static lv_obj_t *lbl_clock;
static lv_obj_t *lbl_power;
static lv_obj_t *lbl_power_unit;
static lv_obj_t *lbl_voltage;
static lv_obj_t *lbl_current;
static lv_obj_t *lbl_today_kwh;
static lv_obj_t *lbl_today_eur;
static lv_obj_t *lbl_session_kwh;
static lv_obj_t *lbl_session_eur;
static lv_obj_t *lbl_tariff;
static lv_obj_t *lbl_total_kwh;

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h,
                           const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x101a24), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x1e2c3a), 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_hex(0x7a8793), 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
    return card;
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x071018), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    // ---- Topbar ----------------------------------------------------------
    lv_obj_t *plug_img = lv_image_create(scr);
    lv_image_set_src(plug_img, &icon_plug);
    lv_obj_align(plug_img, LV_ALIGN_TOP_LEFT, 8, 4);

    lbl_title = lv_label_create(scr);
    lv_label_set_text(lbl_title, "PowerDashboard");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_18, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 42, 6);

    img_wifi = lv_image_create(scr);
    lv_image_set_src(img_wifi, &icon_wifi_off);
    lv_obj_align(img_wifi, LV_ALIGN_TOP_RIGHT, -80, 0);

    lbl_ip = lv_label_create(scr);
    lv_label_set_text(lbl_ip, "IP ---");
    lv_obj_set_style_text_color(lbl_ip, lv_color_hex(0x7a8793), 0);
    lv_obj_set_style_text_font(lbl_ip, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_ip, LV_ALIGN_TOP_MID, 24, 8);

    lbl_clock = lv_label_create(scr);
    lv_label_set_text(lbl_clock, "--:--:--");
    lv_obj_set_style_text_color(lbl_clock, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_18, 0);
    lv_obj_align(lbl_clock, LV_ALIGN_TOP_RIGHT, -10, 6);

    // ---- Power gross -----------------------------------------------------
    lv_obj_t *power_card = make_card(scr, 8, 36, 464, 96, "AKTUELLE LEISTUNG");

    lv_obj_t *bolt_img = lv_image_create(power_card);
    lv_image_set_src(bolt_img, &icon_bolt);
    lv_obj_align(bolt_img, LV_ALIGN_LEFT_MID, 4, 6);

    lbl_power = lv_label_create(power_card);
    lv_label_set_text(lbl_power, "--");
    lv_obj_set_style_text_color(lbl_power, lv_color_hex(0x2EE59D), 0);
    lv_obj_set_style_text_font(lbl_power, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_power, LV_ALIGN_LEFT_MID, 54, 6);

    lbl_power_unit = lv_label_create(power_card);
    lv_label_set_text(lbl_power_unit, "W");
    lv_obj_set_style_text_color(lbl_power_unit, lv_color_hex(0x2EE59D), 0);
    lv_obj_set_style_text_font(lbl_power_unit, &lv_font_montserrat_24, 0);
    lv_obj_align_to(lbl_power_unit, lbl_power, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -6);

    lbl_voltage = lv_label_create(power_card);
    lv_label_set_text(lbl_voltage, "--- V");
    lv_obj_set_style_text_color(lbl_voltage, lv_color_hex(0xA9B4C0), 0);
    lv_obj_set_style_text_font(lbl_voltage, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_voltage, LV_ALIGN_RIGHT_MID, -8, -14);

    lbl_current = lv_label_create(power_card);
    lv_label_set_text(lbl_current, "--- A");
    lv_obj_set_style_text_color(lbl_current, lv_color_hex(0xA9B4C0), 0);
    lv_obj_set_style_text_font(lbl_current, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_current, LV_ALIGN_RIGHT_MID, -8, 14);

    // ---- Heute -----------------------------------------------------------
    lv_obj_t *today_card = make_card(scr, 8, 140, 228, 96, "HEUTE");

    lv_obj_t *today_img = lv_image_create(today_card);
    lv_image_set_src(today_img, &icon_today);
    lv_obj_align(today_img, LV_ALIGN_TOP_RIGHT, 0, 0);

    lbl_today_kwh = lv_label_create(today_card);
    lv_label_set_text(lbl_today_kwh, "--- kWh");
    lv_obj_set_style_text_color(lbl_today_kwh, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_today_kwh, &lv_font_montserrat_28, 0);
    lv_obj_align(lbl_today_kwh, LV_ALIGN_LEFT_MID, 0, -4);

    lbl_today_eur = lv_label_create(today_card);
    lv_label_set_text(lbl_today_eur, "--- EUR");
    lv_obj_set_style_text_color(lbl_today_eur, lv_color_hex(0xFFC857), 0);
    lv_obj_set_style_text_font(lbl_today_eur, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_today_eur, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // ---- Session ---------------------------------------------------------
    lv_obj_t *session_card = make_card(scr, 244, 140, 228, 96, "SEIT BOOT");

    lv_obj_t *session_img = lv_image_create(session_card);
    lv_image_set_src(session_img, &icon_session);
    lv_obj_align(session_img, LV_ALIGN_TOP_RIGHT, 0, 0);

    lbl_session_kwh = lv_label_create(session_card);
    lv_label_set_text(lbl_session_kwh, "--- kWh");
    lv_obj_set_style_text_color(lbl_session_kwh, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_session_kwh, &lv_font_montserrat_28, 0);
    lv_obj_align(lbl_session_kwh, LV_ALIGN_LEFT_MID, 0, -4);

    lbl_session_eur = lv_label_create(session_card);
    lv_label_set_text(lbl_session_eur, "--- EUR");
    lv_obj_set_style_text_color(lbl_session_eur, lv_color_hex(0xFFC857), 0);
    lv_obj_set_style_text_font(lbl_session_eur, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_session_eur, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // ---- Footer ----------------------------------------------------------
    lv_obj_t *euro_img = lv_image_create(scr);
    lv_image_set_src(euro_img, &icon_euro);
    lv_image_set_scale(euro_img, 180);  // 28px -> ~20px
    lv_obj_align(euro_img, LV_ALIGN_BOTTOM_LEFT, 8, -2);

    char tariff_buf[64];
    snprintf(tariff_buf, sizeof(tariff_buf), "Tarif: %.2f ct/kWh",
             (double)g_tariff_ct);
    lbl_tariff = lv_label_create(scr);
    lv_label_set_text(lbl_tariff, tariff_buf);
    lv_obj_set_style_text_color(lbl_tariff, lv_color_hex(0x7a8793), 0);
    lv_obj_set_style_text_font(lbl_tariff, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_tariff, LV_ALIGN_BOTTOM_LEFT, 32, -6);

    lbl_total_kwh = lv_label_create(scr);
    lv_label_set_text(lbl_total_kwh, "Total: --- kWh");
    lv_obj_set_style_text_color(lbl_total_kwh, lv_color_hex(0x7a8793), 0);
    lv_obj_set_style_text_font(lbl_total_kwh, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_total_kwh, LV_ALIGN_BOTTOM_RIGHT, -10, -6);
}

static void ui_update_cb(lv_timer_t *t)
{
    (void)t;

    PlugState snap;
    state_lock();
    snap = g_state;
    state_unlock();

    // Topbar: WiFi-Icon, RSSI, IP
    const lv_image_dsc_t *wifi_src = &icon_wifi_off;
    if (snap.wifi_connected && snap.wifi_rssi != 0) {
        int r = snap.wifi_rssi;
        if      (r >= -55) wifi_src = &icon_wifi_4;
        else if (r >= -65) wifi_src = &icon_wifi_3;
        else if (r >= -75) wifi_src = &icon_wifi_2;
        else               wifi_src = &icon_wifi_1;
    }
    lv_image_set_src(img_wifi, wifi_src);

    // Eigene ESP-IP in der Topbar anzeigen.
    char ip_buf[28];
    snprintf(ip_buf, sizeof(ip_buf), "IP %s", snap.ip_str);
    lv_label_set_text(lbl_ip, ip_buf);

    // Uhr
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    if (snap.sntp_ready) {
        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", lt.tm_hour, lt.tm_min,
                 lt.tm_sec);
        lv_label_set_text(lbl_clock, tbuf);
    } else {
        lv_label_set_text(lbl_clock, "sync...");
    }

    if (!snap.data_valid) {
        lv_label_set_text(lbl_power, "--");
        return;
    }

    // Stale-Indikator (>15 s ohne Erfolg) - Power dimmen
    int64_t age_us = esp_timer_get_time() - snap.last_ok_us;
    bool stale = age_us > 15LL * 1000 * 1000;
    lv_color_t accent = stale ? lv_color_hex(0xff5050) : lv_color_hex(0x2EE59D);
    lv_obj_set_style_text_color(lbl_power, accent, 0);
    lv_obj_set_style_text_color(lbl_power_unit, accent, 0);

    char buf[32];
    if (snap.power_w >= 1000.0f) {
        snprintf(buf, sizeof(buf), "%.2f", snap.power_w / 1000.0f);
        lv_label_set_text(lbl_power, buf);
        lv_label_set_text(lbl_power_unit, "kW");
    } else {
        snprintf(buf, sizeof(buf), "%.1f", snap.power_w);
        lv_label_set_text(lbl_power, buf);
        lv_label_set_text(lbl_power_unit, "W");
    }
    lv_obj_align_to(lbl_power_unit, lbl_power, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -6);

    snprintf(buf, sizeof(buf), "%.1f V", snap.voltage_v);
    lv_label_set_text(lbl_voltage, buf);
    snprintf(buf, sizeof(buf), "%.3f A", snap.current_a);
    lv_label_set_text(lbl_current, buf);

    const float eur_per_kwh = g_tariff_ct / 100.0f;

    double today_wh = 0;
    if (snap.today_start_wh >= 0) {
        today_wh = snap.total_wh - snap.today_start_wh;
        if (today_wh < 0) today_wh = 0;
    }
    double today_kwh = today_wh / 1000.0;
    double today_eur = today_kwh * eur_per_kwh;
    snprintf(buf, sizeof(buf), "%.3f kWh", today_kwh);
    lv_label_set_text(lbl_today_kwh, buf);
    snprintf(buf, sizeof(buf), "%.2f EUR", today_eur);
    lv_label_set_text(lbl_today_eur, buf);

    double sess_wh = 0;
    if (snap.session_start_wh >= 0) {
        sess_wh = snap.total_wh - snap.session_start_wh;
        if (sess_wh < 0) sess_wh = 0;
    }
    double sess_kwh = sess_wh / 1000.0;
    double sess_eur = sess_kwh * eur_per_kwh;
    snprintf(buf, sizeof(buf), "%.3f kWh", sess_kwh);
    lv_label_set_text(lbl_session_kwh, buf);
    snprintf(buf, sizeof(buf), "%.2f EUR", sess_eur);
    lv_label_set_text(lbl_session_eur, buf);

    snprintf(buf, sizeof(buf), "Total: %.2f kWh", snap.total_wh / 1000.0);
    lv_label_set_text(lbl_total_kwh, buf);
}

// =============================================================================
// app_main
// =============================================================================

// --- Boot-Flow Helfer --------------------------------------------------------

static void run_provisioning_mode(void)
{
    // Display + LVGL hochziehen, danach SoftAP + HTTP-Server starten und
    // QR-Codes anzeigen. Wird verlassen erst durch Reboot vom User.
    load_runtime_config();   // Defaults fuer Rotation/Brightness reichen
    init_lgfx();
    init_lvgl();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    char ap_ssid[33] = "";
    char ap_pass[33] = "";
    provisioning_start_ap(ap_ssid, sizeof(ap_ssid),
                          ap_pass, sizeof(ap_pass));
    qr_screen_show(ap_ssid, ap_pass, "http://192.168.4.1/");

    ESP_LOGI(TAG, "Provisioning aktiv - Browser auf 192.168.4.1");
    while (true) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static bool wait_for_wifi(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(g_wifi_events, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

// Bei fehlgeschlagenem WLAN-Verbinden: QR-Setup fuer den naechsten Boot erzwingen.
static void fallback_to_provisioning(void)
{
    ESP_LOGW(TAG, "WLAN-Verbindung in 30s nicht aufgebaut - Fallback in AP");
    settings_set_force_provisioning(true);
    settings_set_str(SETTING_WIFI_SSID, "");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

extern "C" void app_main(void)
{
    g_state_mutex = xSemaphoreCreateMutex();

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    settings_init();

    if (settings_force_provisioning_enabled()) {
        ESP_LOGW(TAG, "Provisioning per Flag erzwungen");
        run_provisioning_mode();
        return;  // nie erreicht
    }

    if (!settings_is_configured()) {
        ESP_LOGW(TAG, "Keine Settings - starte Provisioning-Modus");
        run_provisioning_mode();
        return;  // nie erreicht
    }

    load_runtime_config();

    init_lgfx();
    init_lvgl();
    build_ui();

    wifi_init_sta();

    if (!wait_for_wifi(30000)) {
        fallback_to_provisioning();
        return;
    }

    // HTTP-Server laeuft auch im Normalbetrieb -> Settings ueber Browser
    // an STA-IP weiter erreichbar.
    provisioning_start_http_only();
    mdns_start_http_service();

    char sta_ip[16];
    state_lock();
    strncpy(sta_ip, g_state.ip_str, sizeof(sta_ip));
    sta_ip[sizeof(sta_ip) - 1] = '\0';
    state_unlock();
    provisioning_start_ssdp(sta_ip);

    xTaskCreatePinnedToCore(poll_task, "shelly_poll", 8192, nullptr, 4, nullptr, 0);

    lv_timer_create(ui_update_cb, 500, nullptr);

    ESP_LOGI(TAG, "PowerDashboard running");
    while (true) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
