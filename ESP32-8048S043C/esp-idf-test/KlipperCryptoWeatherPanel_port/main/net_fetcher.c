#include "app_state.h"
#include "i18n.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "net";

#define WIFI_NVS_NS    "wifi"
#define WIFI_NVS_SSID  "ssid"
#define WIFI_NVS_PASS  "pass"
#define CRYPTO_NVS_NS        "crypto"
#define CRYPTO_NVS_BASE      "base"
#define CRYPTO_NVS_QUOTE     "quote"
#define CRYPTO_NVS_TIMEFRAME "timeframe"
#define LOCATION_NVS_NS      "location"
#define LOCATION_NVS_LAT     "lat"
#define LOCATION_NVS_LON     "lon"

// Lat/Lon sind Floats; in NVS landen sie als u32-Bitmuster, damit nvs_get_blob
// nicht mit Endianness/Float-Repraesentation kaempfen muss.
static inline uint32_t float_to_u32(float v) { union { float f; uint32_t u; } x; x.f = v; return x.u; }
static inline float    u32_to_float(uint32_t u) { union { float f; uint32_t u; } x; x.u = u; return x.f; }

static bool location_valid(float lat, float lon)
{
    if (!isfinite(lat) || !isfinite(lon)) return false;
    if (lat < -90.0f  || lat > 90.0f)  return false;
    if (lon < -180.0f || lon > 180.0f) return false;
    return true;
}

crypto_config_t g_crypto = {
    .base = CRYPTO_BASE_SYMBOL,
    .quote = CRYPTO_QUOTE_SYMBOL,
    .price_prefix = CRYPTO_PRICE_PREFIX,
    .service = CRYPTO_SERVICE_NAME,
    .timeframe = CRYPTO_CHART_TIMEFRAME,
};

#define WEATHER_REFRESH_MS       300000U
#define WEATHER_RETRY_MS         120000U
#define PRICE_REFRESH_MS         60000U
#define PRICE_RETRY_MS           30000U
#define CANDLES_REFRESH_MS       300000U
#define CANDLES_RETRY_MS         60000U
#define STATS_REFRESH_MS         300000U
#define STATS_RETRY_MS           60000U
#define KLIPPER_REFRESH_MS       7000U
#define KLIPPER_RETRY_MS         30000U
#define KLIPPER_NAME_REFRESH_MS  300000U
#define KLIPPER_NAME_RETRY_MS    120000U
#define WIFI_STABLE_BEFORE_FETCH_MS 8000U
#define API_REQUEST_GAP_MS       1500U
#define HTTP_TIMEOUT_MS          8000
#define FETCH_TASK_STACK         16384
#define FETCH_TASK_PRIO          4
#define CRYPTO_HTTP_USER_AGENT   "ESP32-S3-HMI"

typedef struct {
    char *data;
    int len;
    int cap;
} http_buf_t;

static TaskHandle_t s_fetch_task;
static volatile bool s_crypto_refresh_requested;
static volatile bool s_sntp_started;
static volatile bool s_time_synced;
static volatile bool s_fetch_pause_requested;
static volatile bool s_http_get_active;
static volatile bool s_weather_refresh_requested;
// Reverse-Geocoding nur einmal pro Boot; bei Location-Wechsel zuruecksetzen,
// damit der City-Name neu aufgeloest wird.
static volatile bool s_location_name_resolved;

static void update_live_candle_from_price(float price);

static void app_set_str(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    // Bewusst begrenzt kopieren; vermeidet GCC format-truncation bei kleinen UI-Puffern.
    size_t n = strlen(src);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void crypto_copy_setting(char *dst, size_t dst_size, const char *src)
{
    app_set_str(dst, dst_size, src);
}

static void crypto_snapshot(crypto_config_t *out)
{
    if (!out) return;
    if (g_app.mutex) app_lock();
    *out = g_crypto;
    if (g_app.mutex) app_unlock();
}

static bool crypto_option_allowed(const char *value, const char * const *options, size_t count)
{
    if (!value || !value[0]) return false;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(value, options[i]) == 0) return true;
    }
    return false;
}

static const char * const CRYPTO_BASE_OPTIONS[] = {
    "BTC", "ETH", "SOL", "XRP", "DOGE", "ADA",
};
static const char * const CRYPTO_QUOTE_OPTIONS[] = {
    "USD", "EUR", "GBP", "USDC", "USDT",
};
static const char * const CRYPTO_TIMEFRAME_OPTIONS[] = {
    "15M", "1H", "6H", "1D",
};

static bool crypto_base_allowed(const char *value)
{
    return crypto_option_allowed(value, CRYPTO_BASE_OPTIONS,
                                 sizeof(CRYPTO_BASE_OPTIONS) / sizeof(CRYPTO_BASE_OPTIONS[0]));
}

static bool crypto_quote_allowed(const char *value)
{
    return crypto_option_allowed(value, CRYPTO_QUOTE_OPTIONS,
                                 sizeof(CRYPTO_QUOTE_OPTIONS) / sizeof(CRYPTO_QUOTE_OPTIONS[0]));
}

static bool crypto_timeframe_allowed(const char *value)
{
    return crypto_option_allowed(value, CRYPTO_TIMEFRAME_OPTIONS,
                                 sizeof(CRYPTO_TIMEFRAME_OPTIONS) / sizeof(CRYPTO_TIMEFRAME_OPTIONS[0]));
}

void crypto_settings_init_defaults(void)
{
    crypto_copy_setting(g_crypto.base, sizeof(g_crypto.base), CRYPTO_BASE_SYMBOL);
    crypto_copy_setting(g_crypto.quote, sizeof(g_crypto.quote), CRYPTO_QUOTE_SYMBOL);
    crypto_copy_setting(g_crypto.price_prefix, sizeof(g_crypto.price_prefix), CRYPTO_PRICE_PREFIX);
    crypto_copy_setting(g_crypto.service, sizeof(g_crypto.service), CRYPTO_SERVICE_NAME);
    crypto_copy_setting(g_crypto.timeframe, sizeof(g_crypto.timeframe), CRYPTO_CHART_TIMEFRAME);
    if (!crypto_base_allowed(g_crypto.base)) crypto_copy_setting(g_crypto.base, sizeof(g_crypto.base), "BTC");
    if (!crypto_quote_allowed(g_crypto.quote)) crypto_copy_setting(g_crypto.quote, sizeof(g_crypto.quote), "USD");
    if (!crypto_timeframe_allowed(g_crypto.timeframe)) crypto_copy_setting(g_crypto.timeframe, sizeof(g_crypto.timeframe), "1D");
}

bool crypto_settings_load(void)
{
    crypto_settings_init_defaults();

    nvs_handle_t h;
    if (nvs_open(CRYPTO_NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;

    bool loaded = false;
    char value[16] = {0};
    size_t size = sizeof(value);
    if (nvs_get_str(h, CRYPTO_NVS_BASE, value, &size) == ESP_OK && crypto_base_allowed(value)) {
        crypto_copy_setting(g_crypto.base, sizeof(g_crypto.base), value);
        loaded = true;
    }
    memset(value, 0, sizeof(value));
    size = sizeof(value);
    if (nvs_get_str(h, CRYPTO_NVS_QUOTE, value, &size) == ESP_OK && crypto_quote_allowed(value)) {
        crypto_copy_setting(g_crypto.quote, sizeof(g_crypto.quote), value);
        loaded = true;
    }
    memset(value, 0, sizeof(value));
    size = sizeof(value);
    if (nvs_get_str(h, CRYPTO_NVS_TIMEFRAME, value, &size) == ESP_OK) {
        // Alte Arduino-Werte grob auf die neuen Coinbase-Granularitaeten mappen.
        if (strcmp(value, "24H") == 0 || strcmp(value, "7D") == 0) crypto_copy_setting(value, sizeof(value), "1H");
        else if (strcmp(value, "30D") == 0) crypto_copy_setting(value, sizeof(value), "6H");
        else if (strcmp(value, "90D") == 0) crypto_copy_setting(value, sizeof(value), "1D");
        if (crypto_timeframe_allowed(value)) {
            crypto_copy_setting(g_crypto.timeframe, sizeof(g_crypto.timeframe), value);
            loaded = true;
        }
    }
    nvs_close(h);
    return loaded;
}

bool crypto_settings_save(void)
{
    crypto_config_t cfg;
    crypto_snapshot(&cfg);

    nvs_handle_t h;
    esp_err_t err = nvs_open(CRYPTO_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "crypto nvs_open RW: %s", esp_err_to_name(err));
        return false;
    }
    bool ok = (nvs_set_str(h, CRYPTO_NVS_BASE, cfg.base) == ESP_OK);
    if (ok) ok = (nvs_set_str(h, CRYPTO_NVS_QUOTE, cfg.quote) == ESP_OK);
    if (ok) ok = (nvs_set_str(h, CRYPTO_NVS_TIMEFRAME, cfg.timeframe) == ESP_OK);
    if (ok) ok = (nvs_commit(h) == ESP_OK);
    nvs_close(h);
    return ok;
}

void config_private_seed_nvs(void)
{
#ifdef CONFIG_PRIVATE_PRESENT
    nvs_handle_t h;

    // Crypto: seed wenn 'base' fehlt (Indikator fuer leeren Namespace).
    bool crypto_missing = true;
    if (nvs_open(CRYPTO_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = 0;
        crypto_missing = (nvs_get_str(h, CRYPTO_NVS_BASE, NULL, &sz) != ESP_OK);
        nvs_close(h);
    }
    if (crypto_missing) {
        ESP_LOGI(TAG, "Seed crypto NVS aus config_private.h");
        crypto_settings_init_defaults();
        crypto_settings_save();
    }

    // Location: seed wenn Lat-Key fehlt.
    bool location_missing = true;
    if (nvs_open(LOCATION_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint32_t bits = 0;
        location_missing = (nvs_get_u32(h, LOCATION_NVS_LAT, &bits) != ESP_OK);
        nvs_close(h);
    }
    if (location_missing) {
        ESP_LOGI(TAG, "Seed location NVS aus config_private.h (%.6f, %.6f)",
                 (double)LOCATION_LATITUDE, (double)LOCATION_LONGITUDE);
        location_settings_save((float)LOCATION_LATITUDE, (float)LOCATION_LONGITUDE);
    }

    // WLAN: nur seeden, wenn Compile-Time-SSID nicht leer und in NVS nichts steht.
    if (WIFI_SSID[0] != '\0') {
        bool wifi_missing = true;
        if (nvs_open(WIFI_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
            size_t sz = 0;
            wifi_missing = (nvs_get_str(h, WIFI_NVS_SSID, NULL, &sz) != ESP_OK);
            nvs_close(h);
        }
        if (wifi_missing) {
            ESP_LOGI(TAG, "Seed WLAN NVS aus config_private.h (SSID=\"%s\")", WIFI_SSID);
            wifi_credentials_save(WIFI_SSID, WIFI_PASSWORD);
        }
    }
#endif
}

// location_settings_load() laeuft vor init_app_state() (vgl. crypto_settings_load
// in main.c), daher ist g_app.mutex noch nicht initialisiert. Wir muessen die
// Locks bedingt nehmen wie crypto_snapshot.
void location_settings_init_defaults(void)
{
    if (g_app.mutex) app_lock();
    g_app.location_latitude  = (float)LOCATION_LATITUDE;
    g_app.location_longitude = (float)LOCATION_LONGITUDE;
    if (g_app.mutex) app_unlock();
}

bool location_settings_load(void)
{
    location_settings_init_defaults();

    nvs_handle_t h;
    if (nvs_open(LOCATION_NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;

    uint32_t lat_bits = 0, lon_bits = 0;
    bool have_lat = (nvs_get_u32(h, LOCATION_NVS_LAT, &lat_bits) == ESP_OK);
    bool have_lon = (nvs_get_u32(h, LOCATION_NVS_LON, &lon_bits) == ESP_OK);
    nvs_close(h);

    if (!have_lat || !have_lon) return false;
    float lat = u32_to_float(lat_bits);
    float lon = u32_to_float(lon_bits);
    if (!location_valid(lat, lon)) return false;

    if (g_app.mutex) app_lock();
    g_app.location_latitude  = lat;
    g_app.location_longitude = lon;
    if (g_app.mutex) app_unlock();
    return true;
}

bool location_settings_save(float latitude, float longitude)
{
    if (!location_valid(latitude, longitude)) return false;

    nvs_handle_t h;
    esp_err_t err = nvs_open(LOCATION_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "location nvs_open RW: %s", esp_err_to_name(err));
        return false;
    }
    bool ok = (nvs_set_u32(h, LOCATION_NVS_LAT, float_to_u32(latitude)) == ESP_OK);
    if (ok) ok = (nvs_set_u32(h, LOCATION_NVS_LON, float_to_u32(longitude)) == ESP_OK);
    if (ok) ok = (nvs_commit(h) == ESP_OK);
    nvs_close(h);

    if (ok) {
        if (g_app.mutex) app_lock();
        g_app.location_latitude  = latitude;
        g_app.location_longitude = longitude;
        // Neue Koordinaten sofort sichtbar machen; echte Werte kommen mit dem
        // erzwungenen Weather-Fetch nach.
        app_set_str(g_app.current_temp, sizeof(g_app.current_temp), "--");
        app_set_str(g_app.weather_status, sizeof(g_app.weather_status), T(WEATHER_STATUS_INIT));
        g_app.weather_location[0] = '\0';
        g_app.weather_code = -1;
        g_app.weather_apparent_temp = NAN;
        g_app.weather_wind_speed = NAN;
        g_app.weather_wind_dir = -1;
        g_app.weather_humidity = -1;
        g_app.weather_is_day = -1;
        for (int i = 0; i < WEATHER_DAILY_COUNT; i++) {
            g_app.weather_daily[i].code = -1;
            g_app.weather_daily[i].tmin = NAN;
            g_app.weather_daily[i].tmax = NAN;
            g_app.weather_daily[i].precip_prob_max = -1;
            g_app.weather_daily[i].sunrise_min = -1;
            g_app.weather_daily[i].sunset_min = -1;
            g_app.weather_daily[i].weekday = -1;
        }
        g_app.weather_daily_count = 0;
        g_app.weather_forecast_ready = false;
        if (g_app.mutex) app_unlock();
        s_location_name_resolved = false;
        s_weather_refresh_requested = true;
        display_brightness_request_sun_refresh();
    }
    return ok;
}

void location_snapshot(float *latitude, float *longitude)
{
    if (g_app.mutex) app_lock();
    if (latitude)  *latitude  = g_app.location_latitude;
    if (longitude) *longitude = g_app.location_longitude;
    if (g_app.mutex) app_unlock();
}

void crypto_request_refresh(void)
{
    s_crypto_refresh_requested = true;
}

const char *crypto_ok_status(void)
{
    static char status[32];
    crypto_config_t cfg;
    crypto_snapshot(&cfg);
    snprintf(status, sizeof(status), "%s %s", cfg.service, cfg.quote);
    return status;
}

void crypto_pair_title_text(char *out, size_t out_size)
{
    crypto_config_t cfg;
    crypto_snapshot(&cfg);
    snprintf(out, out_size, "%s / %s", cfg.base, cfg.quote);
}

void crypto_day_title_text(char *out, size_t out_size)
{
    crypto_config_t cfg;
    crypto_snapshot(&cfg);
    snprintf(out, out_size, "%s %s", cfg.base,
             crypto_timeframe_allowed(cfg.timeframe) ? cfg.timeframe : "1D");
}

void crypto_reset_data_state(const char *status_text)
{
    crypto_config_t cfg;
    crypto_snapshot(&cfg);
    const char *tf = crypto_timeframe_allowed(cfg.timeframe) ? cfg.timeframe : "1D";
    char ok_status[32];
    snprintf(ok_status, sizeof(ok_status), "%s %s", cfg.service, cfg.quote);
    app_lock();
    app_set_str(g_app.crypto_price, sizeof(g_app.crypto_price), T(LOADING));
    g_app.crypto_live_price = 0.0f;
    g_app.crypto_price_direction = 0;
    app_set_str(g_app.crypto_status, sizeof(g_app.crypto_status),
                (status_text && status_text[0]) ? status_text : ok_status);
    snprintf(g_app.btc_day_change, sizeof(g_app.btc_day_change), "%s --", tf);
    app_set_str(g_app.btc_day_time_range, sizeof(g_app.btc_day_time_range), "--");
    app_set_str(g_app.btc_day_volume, sizeof(g_app.btc_day_volume), "VOL --");
    app_set_str(g_app.btc_candle_status, sizeof(g_app.btc_candle_status), "CANDLE --");
    g_app.btc_day_change_percent = 0.0f;
    g_app.btc_day_change_positive = true;
    g_app.btc_day_data_ready = false;
    g_app.crypto_24h_open = 0.0f;
    g_app.crypto_24h_ready = false;
    g_app.candle_count = 0;
    app_unlock();
    ui_chart_invalidate();
}

void crypto_settings_apply(const char *base, const char *quote, const char *timeframe)
{
    if (!crypto_base_allowed(base) || !crypto_quote_allowed(quote) ||
        !crypto_timeframe_allowed(timeframe)) {
        return;
    }

    app_lock();
    crypto_copy_setting(g_crypto.base, sizeof(g_crypto.base), base);
    crypto_copy_setting(g_crypto.quote, sizeof(g_crypto.quote), quote);
    crypto_copy_setting(g_crypto.timeframe, sizeof(g_crypto.timeframe), timeframe);
    g_crypto.price_prefix[0] = '\0';
    app_unlock();

    crypto_reset_data_state(crypto_ok_status());
    crypto_request_refresh();
}

static void crypto_set_error_state(const char *status_text)
{
    crypto_config_t cfg;
    crypto_snapshot(&cfg);
    char fallback_price[24];
    snprintf(fallback_price, sizeof(fallback_price), "%s %s", cfg.base, T(STATUS_ERROR_SHORT));

    app_lock();
    app_set_str(g_app.crypto_status, sizeof(g_app.crypto_status), status_text);
    if (strcmp(g_app.crypto_price, T(LOADING)) == 0) {
        app_set_str(g_app.crypto_price, sizeof(g_app.crypto_price), fallback_price);
    }
    app_unlock();
}

static void btc_set_candle_status(const char *status_text)
{
    app_lock();
    app_set_str(g_app.btc_candle_status, sizeof(g_app.btc_candle_status), status_text);
    g_app.btc_day_data_ready = false;
    app_unlock();
}

static const char *crypto_prefix(void)
{
    crypto_config_t cfg;
    crypto_snapshot(&cfg);
    if (cfg.price_prefix[0]) return g_crypto.price_prefix;
    if (strcasecmp(cfg.quote, "USD") == 0) return "$ ";
    if (strcasecmp(cfg.quote, "EUR") == 0) return "\xe2\x82\xac ";
    if (strcasecmp(cfg.quote, "GBP") == 0) return "\xc2\xa3 ";
    if (strcasecmp(cfg.quote, "JPY") == 0) return "\xc2\xa5 ";
    if (strcasecmp(cfg.quote, "BTC") == 0) return "\xe2\x82\xbf ";
    return "";
}

static void format_quote_full_from_amount(const char *amount, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return;
    if (!amount || !amount[0]) {
        snprintf(buffer, buffer_size, "%s--", crypto_prefix());
        return;
    }

    char price[20];
    app_set_str(price, sizeof(price), amount);
    char *dot = strchr(price, '.');
    if (dot && strlen(dot) > 3) {
        dot[3] = '\0';
    }
    snprintf(buffer, buffer_size, "%s%s", crypto_prefix(), price);
}

void format_quote_compact(float value, char *buffer, size_t buffer_size)
{
    const char *prefix = crypto_prefix();
    if (!buffer || buffer_size == 0) return;
    if (!isfinite(value) || value <= 0.0f) {
        snprintf(buffer, buffer_size, "%s--", prefix);
    } else if (value >= 100000.0f) {
        snprintf(buffer, buffer_size, "%s%.0fK", prefix, value / 1000.0f);
    } else if (value >= 10000.0f) {
        snprintf(buffer, buffer_size, "%s%.1fK", prefix, value / 1000.0f);
    } else if (value >= 100.0f) {
        snprintf(buffer, buffer_size, "%s%.0f", prefix, value);
    } else if (value >= 1.0f) {
        snprintf(buffer, buffer_size, "%s%.2f", prefix, value);
    } else {
        snprintf(buffer, buffer_size, "%s%.4f", prefix, value);
    }
}

uint32_t crypto_chart_granularity_seconds(void)
{
    if (strcmp(g_crypto.timeframe, "15M") == 0) return 900;
    if (strcmp(g_crypto.timeframe, "1H") == 0) return 3600;
    if (strcmp(g_crypto.timeframe, "6H") == 0) return 21600;
    return BTC_CANDLE_SECONDS;
}

int crypto_chart_candle_count(void)
{
    return BTC_DAY_CANDLE_COUNT;
}

const char *crypto_chart_timeframe_label(void)
{
    if (strcmp(g_crypto.timeframe, "15M") == 0 ||
        strcmp(g_crypto.timeframe, "1H") == 0 ||
        strcmp(g_crypto.timeframe, "6H") == 0 ||
        strcmp(g_crypto.timeframe, "1D") == 0) {
        return g_crypto.timeframe;
    }
    return "1D";
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buf_t *buf = (http_buf_t *)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || !buf || !evt->data || evt->data_len <= 0) {
        return ESP_OK;
    }
    int needed = buf->len + evt->data_len + 1;
    if (needed > buf->cap) {
        int new_cap = buf->cap ? buf->cap * 2 : 2048;
        while (new_cap < needed) new_cap *= 2;
        char *next = realloc(buf->data, new_cap);
        if (!next) return ESP_ERR_NO_MEM;
        buf->data = next;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->len, evt->data, evt->data_len);
    buf->len += evt->data_len;
    buf->data[buf->len] = '\0';
    return ESP_OK;
}

static bool http_get(const char *url, bool use_crt_bundle,
                     const char *user_agent, int *status_out, char **out)
{
    *out = NULL;
    if (status_out) *status_out = 0;
    s_http_get_active = true;
    http_buf_t buf = {0};
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .user_data = &buf,
        .disable_auto_redirect = false,
        .keep_alive_enable = false,
    };
    if (use_crt_bundle) {
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        s_http_get_active = false;
        return false;
    }
    if (user_agent && user_agent[0]) {
        esp_http_client_set_header(client, "User-Agent", user_agent);
    }
    esp_http_client_set_header(client, "Accept", "application/json");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (status_out) *status_out = status;
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "GET failed status=%d err=%s url=%s", status, esp_err_to_name(err), url);
        free(buf.data);
        s_http_get_active = false;
        return false;
    }
    if (!buf.data) {
        buf.data = calloc(1, 1);
    }
    *out = buf.data;
    s_http_get_active = false;
    return *out != NULL;
}

static const cJSON *json_obj(const cJSON *parent, const char *key)
{
    return parent ? cJSON_GetObjectItemCaseSensitive(parent, key) : NULL;
}

static const char *json_str(const cJSON *item, const char *fallback)
{
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : fallback;
}

static double json_num(const cJSON *item, double fallback)
{
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

static bool weather_code_is_thunder(int code)
{
    return code >= 95 && code <= 99;
}

static bool weather_current_has_precip(double precipitation, double rain,
                                       double showers, double snowfall)
{
    const double mm_eps = 0.01;
    const double cm_eps = 0.001;
    if (isfinite(precipitation) && precipitation > mm_eps) return true;
    if (isfinite(rain)          && rain          > mm_eps) return true;
    if (isfinite(showers)       && showers       > mm_eps) return true;
    if (isfinite(snowfall)      && snowfall      > cm_eps) return true;
    return false;
}

static int weather_hourly_code_for_current_hour(const char *current_time,
                                                const cJSON *h_time,
                                                const cJSON *h_code)
{
    if (!current_time || strlen(current_time) < 13 ||
        !cJSON_IsArray(h_time) || !cJSON_IsArray(h_code)) {
        return -1;
    }

    int hn = cJSON_GetArraySize(h_time);
    int cn = cJSON_GetArraySize(h_code);
    if (hn > cn) hn = cn;
    for (int i = 0; i < hn; i++) {
        const cJSON *ht = cJSON_GetArrayItem(h_time, i);
        const char *hts = cJSON_IsString(ht) ? ht->valuestring : NULL;
        if (!hts || strlen(hts) < 13) continue;
        if (strncmp(hts, current_time, 13) != 0) continue;

        int code = (int)json_num(cJSON_GetArrayItem(h_code, i), -1);
        return (code >= 0 && code < 100) ? code : -1;
    }
    return -1;
}

static int weather_display_code_from_current(int code, const char *current_time,
                                             double precipitation, double rain,
                                             double showers, double snowfall,
                                             const cJSON *h_time,
                                             const cJSON *h_code)
{
    if (!weather_code_is_thunder(code)) return code;

    // DWD-ICON-D2 markiert haeufig jeden instabilen Schauer als Gewitter (95).
    // Wir akzeptieren das Alarm-Icon nur, wenn auch der Stunden-Code Gewitter
    // meldet UND die Niederschlagsmenge nennenswert ist (>= 1.0 mm/h, was etwa
    // einer echten Gewitter-Lage entspricht). Sonst fallen wir auf den Stunden-
    // Code zurueck oder auf "Schauer"/"bewoelkt" als Notnagel.
    int hourly_code = weather_hourly_code_for_current_hour(current_time, h_time, h_code);
    if (hourly_code >= 0 && !weather_code_is_thunder(hourly_code)) return hourly_code;

    const double thunder_precip_mm = 1.0;
    double r = isfinite(rain)          ? rain          : 0.0;
    double s = isfinite(showers)       ? showers       : 0.0;
    double p = isfinite(precipitation) ? precipitation : 0.0;
    double sn = isfinite(snowfall)     ? snowfall      : 0.0;
    if (p >= thunder_precip_mm || r >= thunder_precip_mm || s >= thunder_precip_mm) {
        return code;
    }

    // Beide melden Gewitter, aber kaum Niederschlag -> sehr wahrscheinlich
    // Modell-Overshoot. Schauer-Icon wenn ueberhaupt etwas faellt, sonst bewoelkt.
    if (weather_current_has_precip(p, r, s, sn)) return 80;
    return 3;
}

static bool time_due(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

// Reverse-Geocoding via BigDataCloud (kein API-Key, kein User-Agent-Zwang).
// Wird einmalig pro Boot vor dem ersten Wetter-Fetch versucht, Ergebnis lebt
// in g_app.weather_location. Bei Misserfolg bleibt WEATHER_LOCATION_NAME stehen.
static bool resolve_location_name(char *out, size_t out_size)
{
    float lat_f = 0.0f, lon_f = 0.0f;
    location_snapshot(&lat_f, &lon_f);
    char url[224];
    snprintf(url, sizeof(url),
             "https://api.bigdatacloud.net/data/reverse-geocode-client"
             "?latitude=%.6f&longitude=%.6f&localityLanguage=de",
             (double)lat_f, (double)lon_f);

    char *payload = NULL;
    if (!http_get(url, true, NULL, NULL, &payload)) return false;

    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) return false;

    // city ist der lesbare Stadt-/Ortsname; locality ist der Stadtteil und
    // fuer ein Wetter-Display meist zu granular.
    const char *city = json_str(json_obj(root, "city"), "");
    if (!city[0]) city = json_str(json_obj(root, "locality"), "");
    if (!city[0]) city = json_str(json_obj(root, "principalSubdivision"), "");

    bool ok = (city[0] != '\0');
    if (ok) app_set_str(out, out_size, city);
    cJSON_Delete(root);
    return ok;
}

static bool fetch_weather(void)
{
    if (!s_location_name_resolved) {
        char name[48] = {0};
        if (resolve_location_name(name, sizeof(name))) {
            app_lock();
            app_set_str(g_app.weather_location, sizeof(g_app.weather_location), name);
            app_unlock();
            ESP_LOGI(TAG, "Standort aufgeloest: %s", name);
            s_location_name_resolved = true;
        } else {
            // Netzwerk/DNS kann direkt nach WLAN noch wackeln; beim naechsten
            // Weather-Refresh erneut versuchen.
            s_location_name_resolved = false;
        }
    }

    // open-meteo waehlt mit models=best_match automatisch das beste Modell:
    // DWD-ICON-D2 (2.2 km) in DE, ICON-EU in Europa, ECMWF/GFS sonst.
    // weather_code ist direkt WMO-konform (passt zu weather_visual_from_code).
    // Zusaetzlich daily (5 Tage) und hourly weather_code; aus letzterem
    // berechnen wir den dominanten Tagescode (8-20 Uhr), damit der Forecast
    // nicht jedes Mal "Gewitter" zeigt, sobald irgendwo am Tag 30 min Sturm
    // durchzieht. daily.weather_code von Open-Meteo ist immer Worst-Case.
    float lat_f = 0.0f, lon_f = 0.0f;
    location_snapshot(&lat_f, &lon_f);
    char url[520];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast"
             "?latitude=%.6f&longitude=%.6f"
             "&current=temperature_2m,weather_code,precipitation,rain,showers,snowfall,"
                       "is_day,apparent_temperature,"
                       "relative_humidity_2m,wind_speed_10m,wind_direction_10m"
             "&hourly=weather_code"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
                     "precipitation_probability_max,sunrise,sunset"
             "&forecast_days=5"
             "&timezone=auto&models=best_match",
             (double)lat_f, (double)lon_f);

    char *payload = NULL;
    if (!http_get(url, true, NULL, NULL, &payload)) {
        app_lock();
        app_set_str(g_app.weather_status, sizeof(g_app.weather_status), "OM: HTTP");
        app_unlock();
        return false;
    }

    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) {
        app_lock();
        app_set_str(g_app.weather_status, sizeof(g_app.weather_status), "OM: JSON");
        app_unlock();
        return false;
    }

    const cJSON *current = json_obj(root, "current");
    const char *current_time = json_str(json_obj(current, "time"), "");
    double temp     = json_num(json_obj(current, "temperature_2m"), NAN);
    int    code     = (int)json_num(json_obj(current, "weather_code"), -1);
    double precip   = json_num(json_obj(current, "precipitation"), NAN);
    double rain     = json_num(json_obj(current, "rain"), NAN);
    double showers  = json_num(json_obj(current, "showers"), NAN);
    double snowfall = json_num(json_obj(current, "snowfall"), NAN);
    int    is_day   = (int)json_num(json_obj(current, "is_day"), -1);
    double apparent = json_num(json_obj(current, "apparent_temperature"), NAN);
    double wind_kmh = json_num(json_obj(current, "wind_speed_10m"), NAN);
    int    wind_dir = (int)json_num(json_obj(current, "wind_direction_10m"), -1);
    int    humidity = (int)json_num(json_obj(current, "relative_humidity_2m"), -1);

    // --- daily: bis zu 5 Tage --------------------------------------------------
    const cJSON *daily        = json_obj(root, "daily");
    const cJSON *d_time       = json_obj(daily, "time");
    const cJSON *d_code       = json_obj(daily, "weather_code");
    const cJSON *d_tmax       = json_obj(daily, "temperature_2m_max");
    const cJSON *d_tmin       = json_obj(daily, "temperature_2m_min");
    const cJSON *d_prec       = json_obj(daily, "precipitation_probability_max");
    const cJSON *d_sunrise    = json_obj(daily, "sunrise");
    const cJSON *d_sunset     = json_obj(daily, "sunset");

    weather_daily_slot_t daily_buf[WEATHER_DAILY_COUNT] = {0};
    int daily_count = 0;
    if (cJSON_IsArray(d_time)) {
        int n = cJSON_GetArraySize(d_time);
        if (n > WEATHER_DAILY_COUNT) n = WEATHER_DAILY_COUNT;
        for (int i = 0; i < n; i++) {
            const cJSON *t_item = cJSON_GetArrayItem(d_time, i);
            const char *t_str = cJSON_IsString(t_item) ? t_item->valuestring : NULL;
            int weekday = -1;
            if (t_str && strlen(t_str) >= 10) {
                struct tm day_tm = {0};
                int y = 0, mo = 0, d = 0;
                if (sscanf(t_str, "%d-%d-%d", &y, &mo, &d) == 3) {
                    day_tm.tm_year = y - 1900;
                    day_tm.tm_mon  = mo - 1;
                    day_tm.tm_mday = d;
                    day_tm.tm_hour = 12; // Mittags, damit DST-Switch nicht stoert
                    time_t tt = mktime(&day_tm);
                    if (tt != (time_t)-1) {
                        struct tm out_tm;
                        if (localtime_r(&tt, &out_tm)) weekday = out_tm.tm_wday;
                    }
                }
            }
            // sunrise/sunset im Format "YYYY-MM-DDTHH:MM" -> Minuten seit Mitternacht
            int sr_min = -1, ss_min = -1;
            const cJSON *sr_item = cJSON_GetArrayItem(d_sunrise, i);
            const cJSON *ss_item = cJSON_GetArrayItem(d_sunset, i);
            if (cJSON_IsString(sr_item) && strlen(sr_item->valuestring) >= 16) {
                const char *s = sr_item->valuestring;
                sr_min = ((s[11]-'0')*10 + (s[12]-'0')) * 60 + ((s[14]-'0')*10 + (s[15]-'0'));
            }
            if (cJSON_IsString(ss_item) && strlen(ss_item->valuestring) >= 16) {
                const char *s = ss_item->valuestring;
                ss_min = ((s[11]-'0')*10 + (s[12]-'0')) * 60 + ((s[14]-'0')*10 + (s[15]-'0'));
            }
            daily_buf[daily_count].code            = (int)json_num(cJSON_GetArrayItem(d_code, i), -1);
            daily_buf[daily_count].tmax            = (float)json_num(cJSON_GetArrayItem(d_tmax, i), NAN);
            daily_buf[daily_count].tmin            = (float)json_num(cJSON_GetArrayItem(d_tmin, i), NAN);
            daily_buf[daily_count].precip_prob_max = (int)json_num(cJSON_GetArrayItem(d_prec, i), -1);
            daily_buf[daily_count].sunrise_min     = sr_min;
            daily_buf[daily_count].sunset_min      = ss_min;
            daily_buf[daily_count].weekday         = weekday;
            daily_count++;
        }
    }

    // --- Dominanten Tagescode aus hourly berechnen ---------------------------
    // daily.weather_code von Open-Meteo ist immer Worst-Case. Wir ersetzen ihn
    // durch den haeufigsten weather_code zwischen 8 und 20 Uhr (Tagzeit), damit
    // ein 30-Minuten-Gewitter den ganzen Tag nicht als "Gewitter" markiert.
    // Tie-Break: niedrigerer WMO-Code (weniger schlimm) gewinnt.
    const cJSON *hourly  = json_obj(root, "hourly");
    const cJSON *h_time  = json_obj(hourly, "time");
    const cJSON *h_code  = json_obj(hourly, "weather_code");
    int display_code = weather_display_code_from_current(code, current_time,
                                                         precip, rain,
                                                         showers, snowfall,
                                                         h_time, h_code);
    if (cJSON_IsArray(h_time) && cJSON_IsArray(h_code) && cJSON_IsArray(d_time)) {
        int hn = cJSON_GetArraySize(h_time);
        for (int di = 0; di < daily_count; di++) {
            const cJSON *day_t = cJSON_GetArrayItem(d_time, di);
            if (!cJSON_IsString(day_t)) continue;
            const char *day_prefix = day_t->valuestring;
            if (strlen(day_prefix) < 10) continue;

            int counts[100] = {0};
            int total = 0;
            int max_code_seen = -1;
            for (int hi = 0; hi < hn; hi++) {
                const cJSON *ht = cJSON_GetArrayItem(h_time, hi);
                const char *hts = cJSON_IsString(ht) ? ht->valuestring : NULL;
                if (!hts || strlen(hts) < 13) continue;
                if (strncmp(hts, day_prefix, 10) != 0) continue;
                int hour = (hts[11] - '0') * 10 + (hts[12] - '0');
                if (hour < 8 || hour > 20) continue;
                int c = (int)json_num(cJSON_GetArrayItem(h_code, hi), -1);
                if (c < 0 || c >= 100) continue;
                counts[c]++;
                total++;
                if (c > max_code_seen) max_code_seen = c;
            }
            if (total == 0) continue;  // Keine Tagstunden gefunden -> Worst-Case lassen
            int best_code = -1, best_count = 0;
            for (int c = 0; c <= max_code_seen; c++) {
                if (counts[c] > best_count) {
                    best_count = counts[c];
                    best_code  = c;
                }
            }
            if (best_code >= 0) daily_buf[di].code = best_code;
        }
    }

    app_lock();
    if (isfinite(temp)) snprintf(g_app.current_temp, sizeof(g_app.current_temp), "%.1f", temp);
    if (g_app.weather_location[0] == '\0') {
        // Fallback nur wenn Reverse-Geocoding fehlschlug; UI zeigt "Standort"
        // statt einer leeren Zeile.
        app_set_str(g_app.weather_location, sizeof(g_app.weather_location), T(LOCATION_FALLBACK));
    }
    if (display_code >= 0) g_app.weather_code = display_code;
    g_app.weather_apparent_temp = (float)apparent;
    g_app.weather_wind_speed    = (float)wind_kmh;
    g_app.weather_wind_dir      = wind_dir;
    g_app.weather_humidity      = humidity;
    g_app.weather_is_day        = is_day;
    memcpy(g_app.weather_daily,  daily_buf,  sizeof(daily_buf));
    g_app.weather_daily_count  = daily_count;
    g_app.weather_forecast_ready = (daily_count > 0);
    app_set_str(g_app.weather_status, sizeof(g_app.weather_status), T(WEATHER_STATUS_OK));
    app_unlock();

    cJSON_Delete(root);
    return true;
}

static bool fetch_price(void)
{
    crypto_config_t cfg;
    crypto_snapshot(&cfg);
    char url[128];
    snprintf(url, sizeof(url), "https://api.coinbase.com/v2/prices/%s-%s/spot",
             cfg.base, cfg.quote);
    char *payload = NULL;
    int http_status = 0;
    if (!http_get(url, false, CRYPTO_HTTP_USER_AGENT, &http_status, &payload)) {
        char status_text[32];
        if (http_status > 0) {
            snprintf(status_text, sizeof(status_text), "%s HTTP %d", cfg.base, http_status);
        } else {
            snprintf(status_text, sizeof(status_text), "%s HTTP", cfg.base);
        }
        crypto_set_error_state(status_text);
        return false;
    }

    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) {
        char status_text[32];
        snprintf(status_text, sizeof(status_text), "%s JSON", cfg.base);
        crypto_set_error_state(status_text);
        return false;
    }
    const char *amount = json_str(json_obj(json_obj(root, "data"), "amount"), "");
    float live = amount[0] ? strtof(amount, NULL) : 0.0f;
    if (live <= 0.0f) {
        cJSON_Delete(root);
        char status_text[32];
        snprintf(status_text, sizeof(status_text), "%s %s", cfg.base, T(STATUS_PRICE));
        crypto_set_error_state(status_text);
        return false;
    }

    char price[24];
    format_quote_full_from_amount(amount, price, sizeof(price));

    app_lock();
    if (g_app.crypto_live_price > 0.0f) {
        if (live > g_app.crypto_live_price) g_app.crypto_price_direction = 1;
        else if (live < g_app.crypto_live_price) g_app.crypto_price_direction = -1;
    } else {
        g_app.crypto_price_direction = 0;
    }
    g_app.crypto_live_price = live;
    app_set_str(g_app.crypto_price, sizeof(g_app.crypto_price), price);
    snprintf(g_app.crypto_status, sizeof(g_app.crypto_status), "%s %s",
             cfg.service, cfg.quote);
    app_unlock();

    cJSON_Delete(root);
    update_live_candle_from_price(live);
    return true;
}

static int candle_cmp(const void *a, const void *b)
{
    const btc_candle_t *ca = (const btc_candle_t *)a;
    const btc_candle_t *cb = (const btc_candle_t *)b;
    return (ca->time > cb->time) - (ca->time < cb->time);
}

static void format_chart_time(time_t t, bool include_time, char *out, size_t out_size)
{
    struct tm tm_info;
    if (t <= 0 || !localtime_r(&t, &tm_info)) {
        snprintf(out, out_size, "--");
        return;
    }
    strftime(out, out_size, include_time ? "%b %d %H:%M" : "%b %d", &tm_info);
}

static void update_btc_stats_locked(void)
{
    if (!g_app.candles || g_app.candle_count < 2) {
        snprintf(g_app.btc_day_change, sizeof(g_app.btc_day_change), "%s --",
                 crypto_chart_timeframe_label());
        app_set_str(g_app.btc_day_time_range, sizeof(g_app.btc_day_time_range), "--");
        app_set_str(g_app.btc_day_volume, sizeof(g_app.btc_day_volume), "VOL --");
        app_set_str(g_app.btc_candle_status, sizeof(g_app.btc_candle_status), "CANDLE --");
        g_app.btc_day_data_ready = false;
        return;
    }

    int start = g_app.candle_count - crypto_chart_candle_count();
    if (start < 0) start = 0;
    btc_candle_t *latest = &g_app.candles[g_app.candle_count - 1];
    float volume = 0.0f;
    for (int i = start; i < g_app.candle_count; i++) volume += g_app.candles[i].volume;

    g_app.btc_day_change_percent = latest->open > 0.0f
        ? ((latest->close - latest->open) / latest->open) * 100.0f
        : 0.0f;
    g_app.btc_day_change_positive = g_app.btc_day_change_percent >= 0.0f;
    snprintf(g_app.btc_day_change, sizeof(g_app.btc_day_change), "%s %s%.2f%%",
             crypto_chart_timeframe_label(),
             g_app.btc_day_change_positive ? "+" : "",
             (double)g_app.btc_day_change_percent);

    uint32_t seconds = crypto_chart_granularity_seconds();
    bool include_time = seconds < 86400;
    char start_text[24], end_text[24];
    time_t start_time = (time_t)g_app.candles[start].time;
    time_t end_time = (time_t)latest->time + (time_t)seconds;
    format_chart_time(start_time, include_time, start_text, sizeof(start_text));
    format_chart_time(end_time, include_time, end_text, sizeof(end_text));
    snprintf(g_app.btc_day_time_range, sizeof(g_app.btc_day_time_range), "%s - %s",
             start_text, end_text);
    snprintf(g_app.btc_day_volume, sizeof(g_app.btc_day_volume), "VOL %.1f %s",
             (double)volume, g_crypto.base);
    app_set_str(g_app.btc_candle_status, sizeof(g_app.btc_candle_status), "LIVE");
    g_app.btc_day_data_ready = true;
}

static void update_live_candle_from_price(float price)
{
    if (!isfinite(price) || price <= 0.0f) return;
    if (!g_app.candles) return;

    time_t now = time(NULL);
    if (now < 100000) return;

    uint32_t candle_seconds = crypto_chart_granularity_seconds();
    uint32_t bucket_time = ((uint32_t)now / candle_seconds) * candle_seconds;

    app_lock();
    if (g_app.candle_count > 0 &&
        g_app.candles[g_app.candle_count - 1].time == bucket_time) {
        btc_candle_t *c = &g_app.candles[g_app.candle_count - 1];
        c->close = price;
        if (price > c->high) c->high = price;
        if (price < c->low) c->low = price;
        update_btc_stats_locked();
    } else if (g_app.candle_count > 0 &&
               bucket_time > g_app.candles[g_app.candle_count - 1].time) {
        if (g_app.candle_count >= BTC_CANDLE_CAPACITY) {
            memmove(g_app.candles, g_app.candles + 1,
                    sizeof(btc_candle_t) * (BTC_CANDLE_CAPACITY - 1));
            g_app.candle_count = BTC_CANDLE_CAPACITY - 1;
        }

        float previous_close = g_app.candles[g_app.candle_count - 1].close;
        btc_candle_t candle = {
            .time = bucket_time,
            .low = price < previous_close ? price : previous_close,
            .high = price > previous_close ? price : previous_close,
            .open = previous_close,
            .close = price,
            .volume = 0.0f,
        };
        g_app.candles[g_app.candle_count++] = candle;
        update_btc_stats_locked();
    }
    app_unlock();
}

static bool fetch_candles(void)
{
    crypto_config_t cfg;
    crypto_snapshot(&cfg);
    if (!g_app.candles) {
        btc_set_candle_status("CANDLE RAM");
        return false;
    }
    time_t now = time(NULL);
    if (now < 100000) {
        btc_set_candle_status("CANDLE WARTET ZEIT");
        return false;
    }

    uint32_t granularity = crypto_chart_granularity_seconds();
    time_t end = now + granularity;
    time_t start = end - (time_t)(granularity * (BTC_DAY_CANDLE_COUNT + 5));
    struct tm tm_start, tm_end;
    gmtime_r(&start, &tm_start);
    gmtime_r(&end, &tm_end);
    char start_iso[32], end_iso[32];
    strftime(start_iso, sizeof(start_iso), "%Y-%m-%dT%H:%M:%SZ", &tm_start);
    strftime(end_iso, sizeof(end_iso), "%Y-%m-%dT%H:%M:%SZ", &tm_end);

    char url[240];
    snprintf(url, sizeof(url),
             "https://api.exchange.coinbase.com/products/%s-%s/candles?granularity=%lu&start=%s&end=%s",
             cfg.base, cfg.quote, (unsigned long)granularity, start_iso, end_iso);

    char *payload = NULL;
    int http_status = 0;
    if (!http_get(url, false, CRYPTO_HTTP_USER_AGENT, &http_status, &payload)) {
        char status_text[24];
        if (http_status > 0) {
            snprintf(status_text, sizeof(status_text), "CANDLE HTTP %d", http_status);
        } else {
            snprintf(status_text, sizeof(status_text), "CANDLE HTTP");
        }
        btc_set_candle_status(status_text);
        return false;
    }
    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        btc_set_candle_status("CANDLE JSON");
        return false;
    }

    btc_candle_t parsed[BTC_CANDLE_CAPACITY];
    int count = 0;
    cJSON *row = NULL;
    cJSON_ArrayForEach(row, root) {
        if (!cJSON_IsArray(row) || cJSON_GetArraySize(row) < 6 || count >= BTC_CANDLE_CAPACITY) continue;
        btc_candle_t c = {
            .time = (uint32_t)json_num(cJSON_GetArrayItem(row, 0), 0),
            .low = (float)json_num(cJSON_GetArrayItem(row, 1), 0),
            .high = (float)json_num(cJSON_GetArrayItem(row, 2), 0),
            .open = (float)json_num(cJSON_GetArrayItem(row, 3), 0),
            .close = (float)json_num(cJSON_GetArrayItem(row, 4), 0),
            .volume = (float)json_num(cJSON_GetArrayItem(row, 5), 0),
        };
        if (c.time > 0 && c.low > 0.0f && c.high >= c.low && c.open > 0.0f && c.close > 0.0f) {
            parsed[count++] = c;
        }
    }
    cJSON_Delete(root);
    if (count < 2) {
        btc_set_candle_status("CANDLE LEER");
        return false;
    }
    qsort(parsed, count, sizeof(parsed[0]), candle_cmp);

    app_lock();
    memcpy(g_app.candles, parsed, sizeof(btc_candle_t) * count);
    g_app.candle_count = count;
    update_btc_stats_locked();
    app_unlock();
    return true;
}

static bool fetch_stats(void)
{
    crypto_config_t cfg;
    crypto_snapshot(&cfg);
    char url[128];
    snprintf(url, sizeof(url), "https://api.exchange.coinbase.com/products/%s-%s/stats",
             cfg.base, cfg.quote);

    char *payload = NULL;
    int http_status = 0;
    if (!http_get(url, false, CRYPTO_HTTP_USER_AGENT, &http_status, &payload)) {
        ESP_LOGW(TAG, "%s stats failed status=%d", cfg.base, http_status);
        return false;
    }

    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) return false;

    const cJSON *open_item = json_obj(root, "open");
    float open_price = 0.0f;
    if (cJSON_IsString(open_item) && open_item->valuestring) {
        open_price = strtof(open_item->valuestring, NULL);
    } else if (cJSON_IsNumber(open_item)) {
        open_price = (float)open_item->valuedouble;
    }
    cJSON_Delete(root);

    if (open_price <= 0.0f) return false;

    app_lock();
    g_app.crypto_24h_open = open_price;
    g_app.crypto_24h_ready = true;
    app_unlock();
    return true;
}

static double json_num_or_string(const cJSON *item, double fallback)
{
    if (cJSON_IsNumber(item)) return item->valuedouble;
    if (cJSON_IsString(item) && item->valuestring && item->valuestring[0]) {
        char *end = NULL;
        double value = strtod(item->valuestring, &end);
        if (end != item->valuestring) return value;
    }
    return fallback;
}

static int json_int_or_string(const cJSON *item, int fallback)
{
    double value = json_num_or_string(item, NAN);
    return isfinite(value) ? (int)value : fallback;
}

static void json_text(const cJSON *item, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (cJSON_IsString(item) && item->valuestring) {
        app_set_str(out, out_size, item->valuestring);
    } else if (cJSON_IsNumber(item)) {
        snprintf(out, out_size, "%.3f", item->valuedouble);
    }
}

static void sanitize_screen_text(char *text, size_t max_len)
{
    if (!text) return;
    char *r = text;
    char *w = text;
    bool prev_space = false;
    while (*r) {
        char c = *r++;
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        if (c == ' ') {
            if (prev_space) continue;
            prev_space = true;
        } else {
            prev_space = false;
        }
        *w++ = c;
    }
    *w = '\0';
    while (w > text && w[-1] == ' ') *--w = '\0';
    while (*text == ' ') memmove(text, text + 1, strlen(text));
    if (max_len > 3 && strlen(text) > max_len) {
        text[max_len - 3] = '.';
        text[max_len - 2] = '.';
        text[max_len - 1] = '.';
        text[max_len] = '\0';
    }
}

static void url_encode_query_param(const char *in, char *out, size_t out_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t pos = 0;
    if (!out || out_size == 0) return;
    for (const unsigned char *p = (const unsigned char *)in; p && *p && pos + 1 < out_size; p++) {
        bool safe = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                    (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' ||
                    *p == '.' || *p == '~';
        if (safe) {
            out[pos++] = (char)*p;
        } else if (pos + 3 < out_size) {
            out[pos++] = '%';
            out[pos++] = hex[(*p >> 4) & 0x0f];
            out[pos++] = hex[*p & 0x0f];
        }
    }
    out[pos] = '\0';
}

static void format_klipper_temperature(char *out, size_t out_size, const cJSON *current_item, const cJSON *target_item)
{
    double current = json_num_or_string(current_item, NAN);
    double target = json_num_or_string(target_item, NAN);
    if (!isfinite(current)) snprintf(out, out_size, "--");
    else if (isfinite(target) && target > 0.5) snprintf(out, out_size, "%.0f%s/%.0f%s", current, TEMP_UNIT, target, TEMP_UNIT);
    else snprintf(out, out_size, "%.0f%s", current, TEMP_UNIT);
}

static void format_klipper_progress(char *out, size_t out_size, const cJSON *progress_item)
{
    double progress = json_num_or_string(progress_item, NAN);
    if (!isfinite(progress)) {
        snprintf(out, out_size, "--");
        return;
    }
    if (progress <= 1.0) progress *= 100.0;
    if (progress < 0.0) progress = 0.0;
    if (progress > 100.0) progress = 100.0;
    snprintf(out, out_size, "%.0f%%", progress);
}

static void format_duration_clock(char *out, size_t out_size, double seconds, bool force_hours)
{
    if (!isfinite(seconds) || seconds < 0.0) {
        snprintf(out, out_size, "--");
        return;
    }
    unsigned long minutes_total = (unsigned long)((seconds + 30.0) / 60.0);
    unsigned long hours = minutes_total / 60UL;
    unsigned long minutes = minutes_total % 60UL;
    if (!force_hours && hours == 0) snprintf(out, out_size, "%luM", minutes);
    else snprintf(out, out_size, "%lu:%02lu", hours, minutes);
}

static void format_klipper_duration(char *out, size_t out_size, double seconds)
{
    if (!isfinite(seconds) || seconds <= 0.0) {
        snprintf(out, out_size, "DAUER --");
        return;
    }
    unsigned long total = (unsigned long)seconds;
    unsigned long hours = total / 3600UL;
    unsigned long minutes = (total % 3600UL) / 60UL;
    if (hours > 0) snprintf(out, out_size, "DAUER %luH %luM", hours, minutes);
    else snprintf(out, out_size, "DAUER %luM", minutes);
}

static void format_klipper_duration_progress(char *out, size_t out_size, double seconds, float estimated_seconds)
{
    if (isfinite(estimated_seconds) && estimated_seconds > 0.5f) {
        char current[16], estimated[16];
        if (!isfinite(seconds) || seconds < 0.0) seconds = 0.0;
        format_duration_clock(current, sizeof(current), seconds, true);
        format_duration_clock(estimated, sizeof(estimated), estimated_seconds, true);
        snprintf(out, out_size, "%s/%s", current, estimated);
        return;
    }
    format_klipper_duration(out, out_size, seconds);
}

static const char *format_klipper_state(const char *state)
{
    if (!state || !state[0]) return "--";
    if (strcasecmp(state, "printing") == 0) return T(PRINT_STATE_PRINTING);
    if (strcasecmp(state, "paused") == 0 || strcasecmp(state, "pausing") == 0) return T(PRINT_STATE_PAUSED);
    if (strcasecmp(state, "error") == 0) return T(PRINT_STATE_ERROR);
    if (strcasecmp(state, "complete") == 0) return T(PRINT_STATE_COMPLETE);
    if (strcasecmp(state, "cancelled") == 0 || strcasecmp(state, "cancelling") == 0) return T(PRINT_STATE_CANCELLED);
    if (strcasecmp(state, "standby") == 0 || strcasecmp(state, "ready") == 0) return T(PRINT_STATE_READY);
    return state;
}

static const char *format_klippy_connection_state(const char *state)
{
    if (!state || !state[0]) return "--";
    if (strcasecmp(state, "ready") == 0) return T(PRINT_STATE_READY);
    if (strcasecmp(state, "startup") == 0) return "START";
    if (strcasecmp(state, "shutdown") == 0 || strcasecmp(state, "disconnected") == 0) return T(PRINT_STATE_OFF);
    if (strcasecmp(state, "error") == 0) return T(PRINT_STATE_ERROR);
    return state;
}

static void klippy_offline_detail(const char *state, const char *message, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (message && message[0]) {
        app_set_str(out, out_size, message);
        return;
    }
    if (state && (strcasecmp(state, "shutdown") == 0 || strcasecmp(state, "disconnected") == 0)) {
        snprintf(out, out_size, "%s", T(KLIPPER_PRINTER_OFF_OR_MCU));
    } else if (state && strcasecmp(state, "startup") == 0) {
        snprintf(out, out_size, "%s", T(KLIPPER_STARTING));
    } else if (state && strcasecmp(state, "error") == 0) {
        snprintf(out, out_size, "%s", T(KLIPPER_ERROR));
    } else if (state && strcasecmp(state, "ready") == 0) {
        snprintf(out, out_size, "%s", T(KLIPPER_PRINT_DATA_MISSING));
    } else {
        snprintf(out, out_size, "%s", T(KLIPPER_KLIPPY_NOT_READY));
    }
}

static void format_klipper_file(char *out, size_t out_size, const char *filename)
{
    if (!filename || !filename[0]) {
        snprintf(out, out_size, "%s", T(KLIPPER_NO_JOB));
        return;
    }
    const char *slash = strrchr(filename, '/');
    const char *backslash = strrchr(filename, '\\');
    const char *base = slash;
    if (backslash && (!base || backslash > base)) base = backslash;
    app_set_str(out, out_size, base ? base + 1 : filename);
}

static void format_klipper_display_message(char *out, size_t out_size, const char *message)
{
    if (!out || out_size == 0) return;
    if (out != message) app_set_str(out, out_size, message);
    sanitize_screen_text(out, out_size > 0 ? out_size - 1 : 0);
}

static uint32_t parse_klipper_color(const char *text)
{
    if (!text) return COLOR_DIM;
    while (*text == ' ' || *text == '#') text++;
    char rgb[7] = {0};
    size_t len = strlen(text);
    if (len < 6) return COLOR_DIM;
    memcpy(rgb, text, 6);
    char *end = NULL;
    uint32_t color = (uint32_t)strtoul(rgb, &end, 16);
    return end == rgb ? COLOR_DIM : color;
}

static void format_klipper_mmu_info(char *out, size_t out_size, int tool, int gate,
                                    const char *action, const char *operation, const char *filament)
{
    const char *activity = (action && action[0]) ? action : operation;
    if (!activity || !activity[0]) activity = "Idle";
    int used = snprintf(out, out_size, "MMU");
    if (tool >= 0 && used > 0 && (size_t)used < out_size) used += snprintf(out + used, out_size - used, " T%d", tool);
    if (gate >= 0 && used > 0 && (size_t)used < out_size) used += snprintf(out + used, out_size - used, " G%d", gate);
    if (used > 0 && (size_t)used < out_size) used += snprintf(out + used, out_size - used, " %s", activity);
    if (filament && filament[0] && strcasecmp(filament, "Unloaded") != 0 && used > 0 && (size_t)used < out_size) {
        snprintf(out + used, out_size - used, "  %s", filament);
    }
}

static bool fetch_klipper_file_metadata(const char *filename, float *estimated_seconds)
{
    if (estimated_seconds) *estimated_seconds = 0.0f;
    if (!filename || !filename[0]) return false;

    char encoded[384];
    char url[512];
    url_encode_query_param(filename, encoded, sizeof(encoded));
    snprintf(url, sizeof(url), "%s/server/files/metadata?filename=%s", KLIPPER_BASE_URL, encoded);

    char *payload = NULL;
    if (!http_get(url, false, NULL, NULL, &payload)) return false;
    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) return false;

    double estimate = json_num_or_string(json_obj(json_obj(root, "result"), "estimated_time"), NAN);
    if (!isfinite(estimate)) estimate = json_num_or_string(json_obj(root, "estimated_time"), NAN);
    cJSON_Delete(root);
    if (!isfinite(estimate) || estimate <= 0.5) return false;
    if (estimated_seconds) *estimated_seconds = (float)estimate;
    return true;
}

static float klipper_estimated_seconds_for_file(const char *filename)
{
    uint32_t now = (uint32_t)esp_log_timestamp();
    if (!filename || !filename[0]) {
        app_lock();
        g_app.klipper_metadata_filename[0] = '\0';
        g_app.klipper_estimated_duration_seconds = 0.0f;
        g_app.klipper_metadata_retry_after = 0;
        app_unlock();
        return 0.0f;
    }

    app_lock();
    bool changed = strcmp(g_app.klipper_metadata_filename, filename) != 0;
    if (changed) {
        app_set_str(g_app.klipper_metadata_filename, sizeof(g_app.klipper_metadata_filename), filename);
        g_app.klipper_estimated_duration_seconds = 0.0f;
        g_app.klipper_metadata_retry_after = 0;
    }
    float cached = g_app.klipper_estimated_duration_seconds;
    uint32_t retry_after = g_app.klipper_metadata_retry_after;
    app_unlock();

    if (cached > 0.5f) return cached;
    if (retry_after != 0 && !time_due(now, retry_after)) return 0.0f;

    float estimate = 0.0f;
    bool ok = fetch_klipper_file_metadata(filename, &estimate);
    app_lock();
    if (ok) {
        g_app.klipper_estimated_duration_seconds = estimate;
        g_app.klipper_metadata_retry_after = 0;
    } else {
        g_app.klipper_metadata_retry_after = now + KLIPPER_NAME_RETRY_MS;
    }
    cached = g_app.klipper_estimated_duration_seconds;
    app_unlock();
    return cached;
}

static bool fetch_klipper_printer_name(void)
{
    char url[192];
    snprintf(url, sizeof(url), "%s/server/database/item?namespace=mainsail&key=general.printername", KLIPPER_BASE_URL);

    char *payload = NULL;
    if (!http_get(url, false, NULL, NULL, &payload)) return false;
    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) return false;

    char name[48];
    json_text(json_obj(json_obj(root, "result"), "value"), name, sizeof(name));
    if (!name[0]) json_text(json_obj(root, "value"), name, sizeof(name));
    cJSON_Delete(root);
    sanitize_screen_text(name, sizeof(name) - 1);
    if (!name[0]) return false;

    app_lock();
    app_set_str(g_app.klipper_printer_name, sizeof(g_app.klipper_printer_name), name);
    app_unlock();
    return true;
}

static bool fetch_klipper_server_info(char *klippy_state, size_t state_size,
                                      char *klippy_message, size_t message_size)
{
    if (klippy_state && state_size) klippy_state[0] = '\0';
    if (klippy_message && message_size) klippy_message[0] = '\0';

    char url[160];
    snprintf(url, sizeof(url), "%s/server/info", KLIPPER_BASE_URL);

    char *payload = NULL;
    int http_status = 0;
    if (!http_get(url, false, NULL, &http_status, &payload)) {
        app_lock();
        g_app.klipper_host_available = false;
        g_app.klipper_available = false;
        g_app.klipper_mmu_available = false;
        app_set_str(g_app.klipper_connection_state, sizeof(g_app.klipper_connection_state), "--");
        app_set_str(g_app.klipper_connection_message, sizeof(g_app.klipper_connection_message), "");
        app_set_str(g_app.klipper_status, sizeof(g_app.klipper_status),
                    http_status > 0 ? "MOONRAKER HTTP" : "MOONRAKER BEGIN");
        app_set_str(g_app.klipper_display_message, sizeof(g_app.klipper_display_message), "");
        app_unlock();
        return false;
    }

    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) {
        app_lock();
        g_app.klipper_host_available = false;
        g_app.klipper_available = false;
        g_app.klipper_mmu_available = false;
        app_set_str(g_app.klipper_status, sizeof(g_app.klipper_status), "INFO JSON");
        app_set_str(g_app.klipper_display_message, sizeof(g_app.klipper_display_message), "");
        app_unlock();
        return false;
    }

    const cJSON *state_item = json_obj(json_obj(root, "result"), "klippy_state");
    if (!state_item) state_item = json_obj(root, "klippy_state");
    json_text(state_item, klippy_state, state_size);

    const cJSON *connected_item = json_obj(json_obj(root, "result"), "klippy_connected");
    if (!connected_item) connected_item = json_obj(root, "klippy_connected");
    if (klippy_state && !klippy_state[0]) {
        app_set_str(klippy_state, state_size, cJSON_IsTrue(connected_item) ? "ready" : "disconnected");
    }

    const cJSON *warning_item = cJSON_GetArrayItem(json_obj(json_obj(root, "result"), "warnings"), 0);
    if (!warning_item) warning_item = cJSON_GetArrayItem(json_obj(root, "warnings"), 0);
    json_text(warning_item, klippy_message, message_size);
    sanitize_screen_text(klippy_message, message_size > 0 ? message_size - 1 : 0);
    cJSON_Delete(root);

    app_lock();
    g_app.klipper_host_available = true;
    app_set_str(g_app.klipper_connection_state, sizeof(g_app.klipper_connection_state), klippy_state);
    app_set_str(g_app.klipper_connection_message, sizeof(g_app.klipper_connection_message), klippy_message);
    app_set_str(g_app.klipper_status, sizeof(g_app.klipper_status), "MOONRAKER OK");
    if (strcasecmp(klippy_state, "ready") != 0) {
        char detail[96];
        klippy_offline_detail(klippy_state, klippy_message, detail, sizeof(detail));
        g_app.klipper_available = false;
        g_app.klipper_mmu_available = false;
        app_set_str(g_app.klipper_state, sizeof(g_app.klipper_state), format_klippy_connection_state(klippy_state));
        app_set_str(g_app.klipper_file, sizeof(g_app.klipper_file), detail);
        app_set_str(g_app.klipper_progress, sizeof(g_app.klipper_progress), "--");
        app_set_str(g_app.klipper_nozzle, sizeof(g_app.klipper_nozzle), "OK");
        app_set_str(g_app.klipper_bed, sizeof(g_app.klipper_bed), g_app.klipper_state);
        app_set_str(g_app.klipper_duration, sizeof(g_app.klipper_duration), "MAINSAIL OK");
        app_set_str(g_app.klipper_display_message, sizeof(g_app.klipper_display_message), "");
        app_set_str(g_app.klipper_mmu_info, sizeof(g_app.klipper_mmu_info), T(KLIPPER_POWER_ON_PRINTER));
        g_app.klipper_mmu_gate_count = 0;
        for (int i = 0; i < MMU_GATE_MAX; i++) {
            g_app.klipper_mmu_gate_colors[i] = COLOR_DIM;
            g_app.klipper_mmu_gate_status[i] = -1;
        }
    }
    app_unlock();
    return true;
}

static bool fetch_klipper_printer_info(char *klippy_state, size_t state_size,
                                       char *klippy_message, size_t message_size)
{
    char url[160];
    snprintf(url, sizeof(url), "%s/printer/info", KLIPPER_BASE_URL);

    char *payload = NULL;
    if (!http_get(url, false, NULL, NULL, &payload)) return false;
    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) return false;

    char state[24];
    char message[96];
    json_text(json_obj(json_obj(root, "result"), "state"), state, sizeof(state));
    if (!state[0]) json_text(json_obj(root, "state"), state, sizeof(state));
    json_text(json_obj(json_obj(root, "result"), "state_message"), message, sizeof(message));
    if (!message[0]) json_text(json_obj(root, "state_message"), message, sizeof(message));
    cJSON_Delete(root);
    sanitize_screen_text(message, sizeof(message) - 1);

    if (state[0]) app_set_str(klippy_state, state_size, state);
    if (message[0]) app_set_str(klippy_message, message_size, message);
    return state[0] || message[0];
}

static bool fetch_klipper(void)
{
    char klippy_state[24];
    char klippy_message[96];
    if (!fetch_klipper_server_info(klippy_state, sizeof(klippy_state),
                                   klippy_message, sizeof(klippy_message))) {
        return false;
    }

    if (strcasecmp(klippy_state, "ready") != 0) {
        fetch_klipper_printer_info(klippy_state, sizeof(klippy_state),
                                   klippy_message, sizeof(klippy_message));
        char detail[96];
        klippy_offline_detail(klippy_state, klippy_message, detail, sizeof(detail));
        app_lock();
        app_set_str(g_app.klipper_connection_state, sizeof(g_app.klipper_connection_state), klippy_state);
        app_set_str(g_app.klipper_connection_message, sizeof(g_app.klipper_connection_message), klippy_message);
        app_set_str(g_app.klipper_state, sizeof(g_app.klipper_state), format_klippy_connection_state(klippy_state));
        app_set_str(g_app.klipper_file, sizeof(g_app.klipper_file), detail);
        app_set_str(g_app.klipper_progress, sizeof(g_app.klipper_progress), "--");
        app_set_str(g_app.klipper_display_message, sizeof(g_app.klipper_display_message), "");
        app_unlock();
        return true;
    }

    char url[320];
    snprintf(url, sizeof(url),
             "%s/printer/objects/query?webhooks&print_stats&display_status&extruder&heater_bed&mmu",
             KLIPPER_BASE_URL);

    char *payload = NULL;
    int http_status = 0;
    if (!http_get(url, false, NULL, &http_status, &payload)) {
        app_lock();
        g_app.klipper_host_available = true;
        g_app.klipper_available = false;
        g_app.klipper_mmu_available = false;
        app_set_str(g_app.klipper_status, sizeof(g_app.klipper_status),
                    http_status > 0 ? T(STATUS_HTTP_ERROR) : "HTTP BEGIN");
        app_set_str(g_app.klipper_display_message, sizeof(g_app.klipper_display_message), "");
        app_unlock();
        return false;
    }

    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) {
        app_lock();
        g_app.klipper_host_available = true;
        g_app.klipper_available = false;
        g_app.klipper_mmu_available = false;
        app_set_str(g_app.klipper_status, sizeof(g_app.klipper_status), "JSON PARSE");
        app_set_str(g_app.klipper_display_message, sizeof(g_app.klipper_display_message), "");
        app_unlock();
        return false;
    }

    const cJSON *status = json_obj(json_obj(root, "result"), "status");
    if (!status) status = json_obj(root, "status");
    if (!status) status = root;

    const cJSON *extruder = json_obj(status, "extruder");
    const cJSON *bed = json_obj(status, "heater_bed");
    const cJSON *print_stats = json_obj(status, "print_stats");
    const cJSON *display = json_obj(status, "display_status");
    const cJSON *mmu = json_obj(status, "mmu");

    char raw_state[24], raw_filename[192], file[96], progress[8];
    char nozzle[24], bed_text[24], duration[40], display_msg[64], status_msg[64];
    char mmu_action[32], mmu_operation[32], mmu_filament[32], mmu_info[64];
    json_text(json_obj(print_stats, "state"), raw_state, sizeof(raw_state));
    json_text(json_obj(print_stats, "filename"), raw_filename, sizeof(raw_filename));
    json_text(json_obj(display, "message"), display_msg, sizeof(display_msg));
    format_klipper_display_message(display_msg, sizeof(display_msg), display_msg);
    format_klipper_file(file, sizeof(file), raw_filename);
    format_klipper_progress(progress, sizeof(progress), json_obj(display, "progress"));
    format_klipper_temperature(nozzle, sizeof(nozzle), json_obj(extruder, "temperature"), json_obj(extruder, "target"));
    format_klipper_temperature(bed_text, sizeof(bed_text), json_obj(bed, "temperature"), json_obj(bed, "target"));

    double print_duration = json_num_or_string(json_obj(print_stats, "print_duration"), NAN);
    if (!isfinite(print_duration)) print_duration = json_num_or_string(json_obj(print_stats, "total_duration"), NAN);
    float estimated_seconds = raw_filename[0] ? klipper_estimated_seconds_for_file(raw_filename) : 0.0f;
    if (!raw_filename[0]) (void)klipper_estimated_seconds_for_file("");
    format_klipper_duration_progress(duration, sizeof(duration), print_duration, estimated_seconds);

    bool has_klipper_data = raw_state[0] ||
        isfinite(json_num_or_string(json_obj(display, "progress"), NAN)) ||
        isfinite(json_num_or_string(json_obj(extruder, "temperature"), NAN)) ||
        isfinite(json_num_or_string(json_obj(bed, "temperature"), NAN));

    if (!has_klipper_data) {
        cJSON_Delete(root);
        app_lock();
        g_app.klipper_host_available = true;
        g_app.klipper_available = false;
        g_app.klipper_mmu_available = false;
        app_set_str(g_app.klipper_status, sizeof(g_app.klipper_status), T(STATUS_JSON_ERROR));
        app_set_str(g_app.klipper_display_message, sizeof(g_app.klipper_display_message), "");
        app_unlock();
        return false;
    }

    int mmu_gate_count = json_int_or_string(json_obj(mmu, "num_gates"), 0);
    if (mmu_gate_count < 0) mmu_gate_count = 0;
    if (mmu_gate_count > MMU_GATE_MAX) mmu_gate_count = MMU_GATE_MAX;
    bool has_mmu_data = cJSON_IsObject(mmu) && mmu_gate_count > 0;
    json_text(json_obj(mmu, "action"), mmu_action, sizeof(mmu_action));
    json_text(json_obj(mmu, "operation"), mmu_operation, sizeof(mmu_operation));
    json_text(json_obj(mmu, "filament"), mmu_filament, sizeof(mmu_filament));
    int mmu_tool = json_int_or_string(json_obj(mmu, "tool"), -1);
    int mmu_gate = json_int_or_string(json_obj(mmu, "gate"), -1);
    format_klipper_mmu_info(mmu_info, sizeof(mmu_info), mmu_tool, mmu_gate,
                            mmu_action, mmu_operation, mmu_filament);
    snprintf(status_msg, sizeof(status_msg), "MOONRAKER OK");

    app_lock();
    g_app.klipper_host_available = true;
    g_app.klipper_available = true;
    app_set_str(g_app.klipper_connection_state, sizeof(g_app.klipper_connection_state), "ready");
    app_set_str(g_app.klipper_connection_message, sizeof(g_app.klipper_connection_message), "");
    app_set_str(g_app.klipper_state, sizeof(g_app.klipper_state), format_klipper_state(raw_state));
    app_set_str(g_app.klipper_file, sizeof(g_app.klipper_file), file);
    app_set_str(g_app.klipper_progress, sizeof(g_app.klipper_progress), progress);
    app_set_str(g_app.klipper_nozzle, sizeof(g_app.klipper_nozzle), nozzle);
    app_set_str(g_app.klipper_bed, sizeof(g_app.klipper_bed), bed_text);
    app_set_str(g_app.klipper_duration, sizeof(g_app.klipper_duration), duration);
    app_set_str(g_app.klipper_status, sizeof(g_app.klipper_status), status_msg);
    app_set_str(g_app.klipper_display_message, sizeof(g_app.klipper_display_message), display_msg);
    g_app.klipper_mmu_available = has_mmu_data;
    g_app.klipper_mmu_tool = mmu_tool;
    g_app.klipper_mmu_gate = mmu_gate;
    g_app.klipper_mmu_gate_count = has_mmu_data ? mmu_gate_count : 0;
    app_set_str(g_app.klipper_mmu_info, sizeof(g_app.klipper_mmu_info), mmu_info);
    for (int i = 0; i < MMU_GATE_MAX; i++) {
        g_app.klipper_mmu_gate_colors[i] = COLOR_DIM;
        g_app.klipper_mmu_gate_status[i] = -1;
    }
    const cJSON *gate_colors = json_obj(mmu, "gate_color");
    const cJSON *gate_status = json_obj(mmu, "gate_status");
    for (int i = 0; i < g_app.klipper_mmu_gate_count; i++) {
        g_app.klipper_mmu_gate_colors[i] = parse_klipper_color(json_str(cJSON_GetArrayItem(gate_colors, i), ""));
        g_app.klipper_mmu_gate_status[i] = json_int_or_string(cJSON_GetArrayItem(gate_status, i), -1);
    }
    app_unlock();

    cJSON_Delete(root);
    return true;
}

static void on_time_synced(struct timeval *tv)
{
    (void)tv;
    s_time_synced = true;
    time_t now = time(NULL);
    struct tm tm_local;
    localtime_r(&now, &tm_local);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_local);
    ESP_LOGI(TAG, "SNTP synced: %s", buf);
}

static void start_sntp(void)
{
    if (s_sntp_started) return;
    setenv("TZ", TIMEZONE_POSIX, 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
#if LWIP_DHCP_GET_NTP_SRV
    esp_sntp_servermode_dhcp(true);                     // Router-NTP via DHCP Option 42
#endif
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_setservername(2, "time.cloudflare.com");
    esp_sntp_set_sync_interval(15 * 1000);              // schneller erster Sync
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_time_sync_notification_cb(on_time_synced);
    esp_sntp_init();
    s_sntp_started = true;
    ESP_LOGI(TAG, "SNTP started after GOT_IP");
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        app_lock();
        g_app.wifi_connected = false;
        g_app.internet_available = false;
        app_unlock();
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        app_lock();
        g_app.wifi_connected = true;
        g_app.internet_available = true;
        app_unlock();
        // Zeit-Sync hat Prio: SNTP erst nach GOT_IP starten, sonst scheitert DNS.
        start_sntp();
    }
}

static void fetch_task(void *arg)
{
    (void)arg;
    uint32_t next_weather = 0;
    uint32_t next_price = 0;
    uint32_t next_candles = 0;
    uint32_t next_stats = 0;
    uint32_t next_klipper = 0;
    uint32_t next_klipper_name = 0;
    uint32_t last_api_request = 0;
    bool schedule_initialized = false;

    while (true) {
        if (s_fetch_pause_requested) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        uint32_t now = (uint32_t)(esp_log_timestamp());
        bool connected;
        app_lock();
        connected = g_app.wifi_connected;
        app_unlock();

        if (!connected) {
            schedule_initialized = false;
            app_lock();
            g_app.klipper_host_available = false;
            g_app.klipper_available = false;
            g_app.klipper_mmu_available = false;
            app_set_str(g_app.klipper_connection_state, sizeof(g_app.klipper_connection_state), "--");
            app_set_str(g_app.klipper_connection_message, sizeof(g_app.klipper_connection_message), "");
            app_set_str(g_app.klipper_status, sizeof(g_app.klipper_status), T(WIFI_WAITING));
            app_set_str(g_app.klipper_display_message, sizeof(g_app.klipper_display_message), "");
            app_unlock();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!schedule_initialized) {
            // Zeit-Sync hat Prio: erst loslegen wenn SNTP eine plausible Zeit gesetzt hat,
            // sonst stehen Candles/Stats mit Epoch ~1970 und Klipper-Status zeigt Muell.
            if (!s_time_synced && time(NULL) < 1700000000) {
                app_lock();
                app_set_str(g_app.klipper_status, sizeof(g_app.klipper_status), "ZEIT SYNC");
                app_unlock();
                vTaskDelay(pdMS_TO_TICKS(250));
                continue;
            }
            uint32_t start = now + WIFI_STABLE_BEFORE_FETCH_MS;
            next_price = start;
            next_candles = start + 5000U;
            next_stats = start + 7000U;
            next_weather = start + 10000U;
            next_klipper_name = start + 17000U;
            next_klipper = start + 24000U;
            last_api_request = 0;
            schedule_initialized = true;
        }

        if (s_crypto_refresh_requested) {
            // Settings-Wechsel sofort neu laden, ohne auf das normale Intervall zu warten.
            s_crypto_refresh_requested = false;
            next_price = now;
            next_candles = now + 1500U;
            next_stats = now + 3000U;
        }

        if (s_weather_refresh_requested) {
            // Location-Wechsel nicht bis zum 5-Minuten-Intervall liegen lassen.
            s_weather_refresh_requested = false;
            next_weather = now;
        }

        if (last_api_request != 0 && now - last_api_request < API_REQUEST_GAP_MS) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        if (time_due(now, next_price)) {
            bool ok = fetch_price();
            next_price = now + (ok ? PRICE_REFRESH_MS : PRICE_RETRY_MS);
            last_api_request = (uint32_t)esp_log_timestamp();
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        if (time_due(now, next_candles)) {
            if (time(NULL) < 100000) {
                btc_set_candle_status("CANDLE WARTET ZEIT");
                next_candles = now + 10000U;
            } else {
                bool ok = fetch_candles();
                next_candles = now + (ok ? CANDLES_REFRESH_MS : CANDLES_RETRY_MS);
                last_api_request = (uint32_t)esp_log_timestamp();
            }
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        if (time_due(now, next_stats)) {
            bool ok = fetch_stats();
            next_stats = now + (ok ? STATS_REFRESH_MS : STATS_RETRY_MS);
            last_api_request = (uint32_t)esp_log_timestamp();
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        if (time_due(now, next_weather)) {
            bool ok = fetch_weather();
            next_weather = now + (ok ? WEATHER_REFRESH_MS : WEATHER_RETRY_MS);
            last_api_request = (uint32_t)esp_log_timestamp();
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        if (time_due(now, next_klipper_name)) {
            // Der Mainsail-Anzeigename aendert selten; getrennt vom schnellen Statuspoll.
            bool ok = fetch_klipper_printer_name();
            next_klipper_name = now + (ok ? KLIPPER_NAME_REFRESH_MS : KLIPPER_NAME_RETRY_MS);
            last_api_request = (uint32_t)esp_log_timestamp();
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        if (time_due(now, next_klipper)) {
            bool ok = fetch_klipper();
            next_klipper = now + (ok ? KLIPPER_REFRESH_MS : KLIPPER_RETRY_MS);
            last_api_request = (uint32_t)esp_log_timestamp();
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

bool wifi_credentials_load(char *ssid_out, size_t ssid_size,
                           char *pass_out, size_t pass_size)
{
    if (!ssid_out || ssid_size == 0 || !pass_out || pass_size == 0) return false;
    ssid_out[0] = '\0';
    pass_out[0] = '\0';

    bool from_nvs = false;
    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = ssid_size;
        if (nvs_get_str(h, WIFI_NVS_SSID, ssid_out, &sz) == ESP_OK && ssid_out[0]) {
            sz = pass_size;
            if (nvs_get_str(h, WIFI_NVS_PASS, pass_out, &sz) != ESP_OK) {
                pass_out[0] = '\0';
            }
            from_nvs = true;
        }
        nvs_close(h);
    }
    if (!from_nvs) {
        // Fallback auf Compile-Time-Defaults (config_private.h).
        snprintf(ssid_out, ssid_size, "%s", WIFI_SSID);
        snprintf(pass_out, pass_size, "%s", WIFI_PASSWORD);
    }
    return ssid_out[0] != '\0';
}

bool wifi_credentials_save(const char *ssid, const char *pass)
{
    if (!ssid) return false;
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open RW: %s", esp_err_to_name(err));
        return false;
    }
    bool ok = (nvs_set_str(h, WIFI_NVS_SSID, ssid) == ESP_OK);
    if (ok) ok = (nvs_set_str(h, WIFI_NVS_PASS, pass ? pass : "") == ESP_OK);
    if (ok) ok = (nvs_commit(h) == ESP_OK);
    nvs_close(h);
    if (ok) ESP_LOGI(TAG, "WLAN-Daten in NVS gespeichert (SSID=\"%s\")", ssid);
    return ok;
}

void net_pause_fetches(bool pause)
{
    s_fetch_pause_requested = pause;
    if (!pause) return;

    // OTA braucht exklusiven TLS/HTTP-Speicher; kurz warten bis ein laufender
    // normaler API-Request aus dem HTTP-Client raus ist.
    for (int i = 0; i < 60 && s_http_get_active; ++i) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void net_start(void)
{
    char ssid[33] = {0};
    char pass[65] = {0};
    bool have_creds = wifi_credentials_load(ssid, sizeof(ssid), pass, sizeof(pass));
    if (!have_creds) {
        app_lock();
        g_app.wifi_connected = false;
        app_set_str(g_app.weather_status, sizeof(g_app.weather_status), T(WIFI_NOT_CONFIGURED));
        app_set_str(g_app.crypto_status, sizeof(g_app.crypto_status), T(WIFI_MISSING));
        app_unlock();
        ESP_LOGW(TAG, "Keine WLAN-Credentials; Settings -> WLAN oeffnen");
        return;
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
    snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), "%s", ssid);
    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), "%s", pass);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    // start_sntp() laeuft jetzt im GOT_IP-Handler, sobald die Verbindung steht.

    if (!s_fetch_task) {
        xTaskCreatePinnedToCore(fetch_task, "net_fetch", FETCH_TASK_STACK, NULL,
                                FETCH_TASK_PRIO, &s_fetch_task, 1);
    }
    ESP_LOGI(TAG, "WiFi/API services started");
}
