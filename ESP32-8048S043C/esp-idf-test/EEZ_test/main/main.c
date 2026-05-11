// ESP32-8048S043C Wetter-Dashboard
// Panel + GT911-Touch + LVGL/EEZ-UI + WLAN + SNTP + DWD-BEOB per HTTP.

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"

#include "screens.h"
#include "ui.h"
#include "ui_assets.h"

// Private WLAN-/Standortdaten liegen lokal in main/config_private.h.
#include "config_private.h"

#ifndef WIFI_SSID
#error "WIFI_SSID fehlt in main/config_private.h"
#endif

#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD fehlt in main/config_private.h"
#endif

#ifndef DWD_LATITUDE
#define DWD_LATITUDE 51.3397f
#endif

#ifndef DWD_LONGITUDE
#define DWD_LONGITUDE 12.3731f
#endif

#ifndef DWD_LOCATION_NAME
#ifndef DWD_STATION_NAME
#define DWD_STATION_NAME "Leipzig"
#endif
#else
#define DWD_STATION_NAME DWD_LOCATION_NAME
#endif

#ifndef DWD_FALLBACK_STATION_ID
#ifdef DWD_STATION_ID
#define DWD_FALLBACK_STATION_ID DWD_STATION_ID
#else
#define DWD_FALLBACK_STATION_ID "10471"
#endif
#endif

static const char *TAG = "dashboard";

#define LCD_H_RES           800
#define LCD_V_RES           480
#define LCD_PIXEL_CLOCK_HZ  (16 * 1000 * 1000)

#define DWD_CURRENT_URL_BASE "http://opendata.dwd.de/weather/weather_reports/poi/"
#define DWD_STATION_LIST_URL "http://opendata.dwd.de/weather/weather_reports/stationlist_synoptic_germany.csv"
#define DWD_BUF_SIZE        12288
#define DWD_STATION_BUF_SIZE (160 * 1024)

// Panel
#define PIN_BL     2
#define PIN_PCLK   42
#define PIN_HSYNC  39
#define PIN_VSYNC  41
#define PIN_DE     40

// Touch
#define PIN_TP_SDA 19
#define PIN_TP_SCL 20
#define PIN_TP_RST 38

// ---------- Backlight + Panel ---------------------------------------------

static void backlight_init_on(void)
{
    gpio_config_t cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_BL,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(PIN_BL, 1);
}

static esp_lcd_panel_handle_t panel_init(void)
{
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 2,
        // Bounce-Buffer entkoppelt LCD-DMA und PSRAM-Framebuffer.
        .bounce_buffer_size_px = LCD_H_RES * 10,
        .dma_burst_size = 64,
        .disp_gpio_num = -1,
        .pclk_gpio_num = PIN_PCLK,
        .vsync_gpio_num = PIN_VSYNC,
        .hsync_gpio_num = PIN_HSYNC,
        .de_gpio_num = PIN_DE,
        .data_gpio_nums = {
            8, 3, 46, 9, 1,
            5, 6, 7, 15, 16, 4,
            45, 48, 47, 21, 14,
        },
        .timings = {
            .pclk_hz = LCD_PIXEL_CLOCK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 16,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 4,
            .vsync_front_porch = 4,
            .flags = {
                .pclk_active_neg = true,
            },
        },
        .flags = {
            .fb_in_psram = true,
        },
    };

    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    return panel;
}

// ---------- Touch-Kalibrierung --------------------------------------------

static void touch_process_coords(esp_lcd_touch_handle_t tp,
                                 uint16_t *x, uint16_t *y, uint16_t *strength,
                                 uint8_t *point_num, uint8_t max_point_num)
{
    (void)tp;
    (void)strength;

    static const float CAL_X_RX =  1.65867031f;
    static const float CAL_X_RY = -0.02261823f;
    static const float CAL_X_C  =  2.12817001f;
    static const float CAL_Y_RX =  0.02082564f;
    static const float CAL_Y_RY =  1.79517055f;
    static const float CAL_Y_C  = 10.62223816f;

    uint8_t n = *point_num;
    if (n > max_point_num) n = max_point_num;

    for (uint8_t i = 0; i < n; i++) {
        float rx = (float)x[i];
        float ry = (float)y[i];
        int sx = (int)(CAL_X_RX * rx + CAL_X_RY * ry + CAL_X_C + 0.5f);
        int sy = (int)(CAL_Y_RX * rx + CAL_Y_RY * ry + CAL_Y_C + 0.5f);
        if (sx < 0) sx = 0;
        if (sx > LCD_H_RES - 1) sx = LCD_H_RES - 1;
        if (sy < 0) sy = 0;
        if (sy > LCD_V_RES - 1) sy = LCD_V_RES - 1;
        x[i] = (uint16_t)sx;
        y[i] = (uint16_t)sy;
    }
}

// ---------- I2C + GT911 ----------------------------------------------------

static i2c_master_bus_handle_t i2c_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_TP_SDA,
        .scl_io_num = PIN_TP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));
    return bus;
}

static esp_lcd_touch_handle_t touch_init(i2c_master_bus_handle_t bus)
{
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &tp_io));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = PIN_TP_RST,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .process_coordinates = touch_process_coords,
    };
    esp_lcd_touch_handle_t tp = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp));
    return tp;
}

// ---------- WLAN -----------------------------------------------------------

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRY     8

static EventGroupHandle_t s_wifi_event_group;
static int s_wifi_retry_num;
static bool s_wifi_ok;
static bool s_time_ok;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry_num < WIFI_MAX_RETRY) {
            s_wifi_retry_num++;
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void nvs_init_once(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static bool wifi_connect_sta(void)
{
    if (WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "WIFI_SSID ist leer, WLAN wird uebersprungen");
        return false;
    }

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_PASSWORD[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(20000));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

// ---------- Uhrzeit --------------------------------------------------------

static void time_sync_start(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
}

static bool time_wait_valid(void)
{
    time_t now = 0;
    struct tm timeinfo = {0};

    for (int i = 0; i < 30; i++) {
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year >= (2024 - 1900)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

// ---------- DWD CSV --------------------------------------------------------

typedef struct {
    bool valid;
    char date[9];
    char utc_time[6];
    float temp_c;
    float pressure_hpa;
    int humidity_pct;
    int cloud_cover_pct;
    int weather_code;
} dwd_weather_t;

typedef struct {
    bool valid;
    char id[16];
    char name[64];
    float lat;
    float lon;
    float distance_score;
} dwd_station_t;

static dwd_station_t s_dwd_station = {
    .valid = true,
    .id = DWD_FALLBACK_STATION_ID,
    .name = DWD_STATION_NAME,
};

static bool csv_get_field(const char *line, int wanted_index, char *out, size_t out_size)
{
    int index = 0;
    const char *start = line;

    while (start && *start) {
        const char *end = strchr(start, ';');
        size_t len = end ? (size_t)(end - start) : strcspn(start, "\r\n");

        if (index == wanted_index) {
            if (len >= out_size) len = out_size - 1;
            memcpy(out, start, len);
            out[len] = '\0';
            return true;
        }

        if (!end) break;
        start = end + 1;
        index++;
    }

    return false;
}

static float parse_float_de(const char *text, bool *ok)
{
    char tmp[24];
    size_t len = strnlen(text, sizeof(tmp) - 1);

    if (len == 0 || strcmp(text, "---") == 0) {
        *ok = false;
        return 0.0f;
    }

    memcpy(tmp, text, len);
    tmp[len] = '\0';
    for (size_t i = 0; i < len; i++) {
        if (tmp[i] == ',') tmp[i] = '.';
    }

    char *end = NULL;
    float value = strtof(tmp, &end);
    *ok = (end != tmp);
    return value;
}

static int parse_int_de(const char *text, bool *ok)
{
    if (text[0] == '\0' || strcmp(text, "---") == 0) {
        *ok = false;
        return 0;
    }

    char *end = NULL;
    int value = (int)strtol(text, &end, 10);
    *ok = (end != text);
    return value;
}

static bool dwd_parse_station_line(const char *line, dwd_station_t *station)
{
    char field[80];
    bool ok_lat = false;
    bool ok_lon = false;

    memset(station, 0, sizeof(*station));

    if (!csv_get_field(line, 2, station->id, sizeof(station->id))) return false;
    if (!csv_get_field(line, 3, station->name, sizeof(station->name))) return false;

    if (csv_get_field(line, 7, field, sizeof(field))) {
        station->lat = parse_float_de(field, &ok_lat);
    }
    if (csv_get_field(line, 8, field, sizeof(field))) {
        station->lon = parse_float_de(field, &ok_lon);
    }

    if (!ok_lat || !ok_lon || station->id[0] == '\0') return false;

    float dlat = station->lat - DWD_LATITUDE;
    float dlon = (station->lon - DWD_LONGITUDE) * 0.63f;
    station->distance_score = dlat * dlat + dlon * dlon;
    station->valid = true;
    return true;
}

static bool dwd_select_nearest_station(const char *csv, dwd_station_t *nearest)
{
    const char *line = strchr(csv, '\n');
    if (!line) return false;
    line++;

    dwd_station_t best = {0};

    while (line && *line) {
        const char *next = strchr(line, '\n');
        dwd_station_t candidate;

        if (dwd_parse_station_line(line, &candidate) &&
            (!best.valid || candidate.distance_score < best.distance_score)) {
            best = candidate;
        }

        if (!next) break;
        line = next + 1;
    }

    if (!best.valid) return false;
    *nearest = best;
    return true;
}

static bool dwd_parse_latest(const char *csv, dwd_weather_t *out)
{
    const char *line = csv;
    for (int i = 0; i < 3; i++) {
        line = strchr(line, '\n');
        if (!line) return false;
        line++;
    }

    char field[32];
    bool ok = false;
    memset(out, 0, sizeof(*out));

    csv_get_field(line, 0, out->date, sizeof(out->date));
    csv_get_field(line, 1, out->utc_time, sizeof(out->utc_time));

    if (csv_get_field(line, 2, field, sizeof(field))) {
        out->cloud_cover_pct = parse_int_de(field, &ok);
    }
    if (csv_get_field(line, 9, field, sizeof(field))) {
        out->temp_c = parse_float_de(field, &ok);
        if (!ok) return false;
    }
    if (csv_get_field(line, 35, field, sizeof(field))) {
        out->weather_code = parse_int_de(field, &ok);
        if (!ok) out->weather_code = -1;
    }
    if (csv_get_field(line, 36, field, sizeof(field))) {
        out->pressure_hpa = parse_float_de(field, &ok);
    }
    if (csv_get_field(line, 37, field, sizeof(field))) {
        out->humidity_pct = parse_int_de(field, &ok);
    }

    out->valid = true;
    return true;
}

static esp_err_t dwd_http_get_url(const char *url, char *buf, size_t buf_size)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    esp_http_client_fetch_headers(client);

    int total = 0;
    while (total < (int)buf_size - 1) {
        int read = esp_http_client_read(client, buf + total, (int)buf_size - 1 - total);
        if (read <= 0) break;
        total += read;
    }
    buf[total] = '\0';

    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return (status == 200 && total > 0) ? ESP_OK : ESP_FAIL;
}

static bool dwd_update_nearest_station(void)
{
    char *station_buf = calloc(1, DWD_STATION_BUF_SIZE);
    if (!station_buf) return false;

    bool ok = false;
    if (dwd_http_get_url(DWD_STATION_LIST_URL, station_buf, DWD_STATION_BUF_SIZE) == ESP_OK) {
        dwd_station_t nearest;
        if (dwd_select_nearest_station(station_buf, &nearest)) {
            s_dwd_station = nearest;
            ok = true;
            ESP_LOGI(TAG, "DWD Station: %s (%s)", s_dwd_station.name, s_dwd_station.id);
        }
    }

    free(station_buf);
    return ok;
}

static const lv_image_dsc_t *weather_icon_for(const dwd_weather_t *w)
{
    int code = w->weather_code;

    if (code >= 95) return &icon_weather_thunder;
    if ((code >= 70 && code <= 79) || code == 85 || code == 86) return &icon_weather_snow;
    if ((code >= 50 && code <= 69) || (code >= 80 && code <= 84)) return &icon_weather_rain;
    if (code >= 40 && code <= 49) return &icon_weather_fog;
    if (w->cloud_cover_pct >= 75) return &icon_weather_cloudy;
    if (w->cloud_cover_pct >= 25) return &icon_weather_partly_cloudy;
    return &icon_weather_clear;
}

// ---------- EEZ UI Runtime -------------------------------------------------

static const char *weekday_de(int wday)
{
    static const char *names[] = {
        "Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"
    };
    if (wday < 0 || wday > 6) return "";
    return names[wday];
}

static void update_status_icon(const struct tm *timeinfo)
{
    if (!s_wifi_ok || !s_time_ok) {
        lv_image_set_src(objects.statussonne, &icon_status_offline);
    } else if (timeinfo->tm_hour >= 7 && timeinfo->tm_hour < 20) {
        lv_image_set_src(objects.statussonne, &icon_status_day_line);
    } else {
        lv_image_set_src(objects.statussonne, &icon_status_night_line);
    }
}

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year < (2024 - 1900)) {
        lv_label_set_text(objects.lbl_clock, "--:--");
        lv_label_set_text(objects.lbl_datum_tag, "warte auf Uhrzeit");
        lv_bar_set_value(objects.bar_sec, 0, LV_ANIM_OFF);
        update_status_icon(&timeinfo);
        return;
    }

    lv_label_set_text_fmt(objects.lbl_clock, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    lv_label_set_text_fmt(objects.lbl_datum_tag, "%s | %02d.%02d.%04d",
                          weekday_de(timeinfo.tm_wday),
                          timeinfo.tm_mday,
                          timeinfo.tm_mon + 1,
                          timeinfo.tm_year + 1900);
    lv_bar_set_value(objects.bar_sec, timeinfo.tm_sec, LV_ANIM_OFF);
    update_status_icon(&timeinfo);
}

static void apply_weather_to_ui(const dwd_weather_t *weather)
{
    lv_label_set_text(objects.lbl_wetterstation,
                      s_dwd_station.valid ? s_dwd_station.name : DWD_STATION_NAME);

    if (!weather->valid) {
        lv_label_set_text(objects.lbl_temp, "--.- °C");
        lv_image_set_src(objects.weather_icon, &icon_weather_cloudy);
        return;
    }

    // LVGLs kleines printf kann floats je nach Config nicht formatieren.
    int temp_tenths = (int)(weather->temp_c * 10.0f + (weather->temp_c >= 0.0f ? 0.5f : -0.5f));
    lv_label_set_text_fmt(objects.lbl_temp, "%d.%d °C", temp_tenths / 10, abs(temp_tenths % 10));
    lv_image_set_src(objects.weather_icon, weather_icon_for(weather));
}

static void dashboard_task(void *arg)
{
    (void)arg;

    nvs_init_once();
    s_wifi_ok = wifi_connect_sta();

    if (s_wifi_ok) {
        ESP_LOGI(TAG, "WLAN verbunden, starte SNTP");
        time_sync_start();
        s_time_ok = time_wait_valid();
        dwd_update_nearest_station();
    }

    char *dwd_buf = calloc(1, DWD_BUF_SIZE);
    if (!dwd_buf) {
        ESP_LOGE(TAG, "Kein Speicher fuer DWD-Puffer");
        vTaskDelete(NULL);
    }

    dwd_weather_t last_weather = {0};

    while (1) {
        dwd_weather_t weather = {0};
        char dwd_url[128];

        snprintf(dwd_url, sizeof(dwd_url), "%s%s-BEOB.csv",
                 DWD_CURRENT_URL_BASE,
                 s_dwd_station.valid ? s_dwd_station.id : DWD_FALLBACK_STATION_ID);

        if (s_wifi_ok && dwd_http_get_url(dwd_url, dwd_buf, DWD_BUF_SIZE) == ESP_OK) {
            if (dwd_parse_latest(dwd_buf, &weather)) {
                last_weather = weather;
            }
        }

        if (lvgl_port_lock(pdMS_TO_TICKS(1000))) {
            apply_weather_to_ui(&last_weather);
            lvgl_port_unlock();
        }

        // DWD-BEOB wird stuendlich erneuert; 15 min reicht fuer ein Dashboard.
        vTaskDelay(pdMS_TO_TICKS(15 * 60 * 1000));
    }
}

// ---------- main -----------------------------------------------------------

void app_main(void)
{
    ESP_LOGI(TAG, "Backlight on");
    backlight_init_on();

    ESP_LOGI(TAG, "Init RGB panel");
    esp_lcd_panel_handle_t panel = panel_init();

    ESP_LOGI(TAG, "Init I2C + GT911");
    i2c_master_bus_handle_t i2c_bus = i2c_init();
    esp_lcd_touch_handle_t tp = touch_init(i2c_bus);

    ESP_LOGI(TAG, "Init LVGL port");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel,
        .buffer_size = LCD_H_RES * LCD_V_RES,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .direct_mode = true,
        },
    };
    lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    assert(disp);

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = tp,
    };
    lvgl_port_add_touch(&touch_cfg);

    ESP_LOGI(TAG, "Init EEZ UI");
    if (lvgl_port_lock(0)) {
        ui_init();
        lv_label_set_text(objects.lbl_wetterstation, DWD_STATION_NAME);
        lv_label_set_text(objects.lbl_clock, "--:--");
        lv_label_set_text(objects.lbl_datum_tag, "warte auf WLAN");
        lv_label_set_text(objects.lbl_temp, "--.- °C");
        lv_image_set_src(objects.weather_icon, &icon_weather_cloudy);
        lv_image_set_src(objects.statussonne, &icon_status_offline);
        lv_timer_create(clock_timer_cb, 1000, NULL);
        lvgl_port_unlock();
    }

    xTaskCreate(dashboard_task, "dashboard_task", 8192, NULL, 5, NULL);

    ESP_LOGI(TAG, "Running.");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
