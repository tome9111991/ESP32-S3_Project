#include "app_state.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
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

static void update_live_candle_from_price(float price);

static void app_set_str(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    snprintf(dst, dst_size, "%s", src);
}

static void crypto_set_error_state(const char *status_text)
{
    char fallback_price[24];
    snprintf(fallback_price, sizeof(fallback_price), "%s FEHLER", CRYPTO_BASE_SYMBOL);

    app_lock();
    app_set_str(g_app.crypto_status, sizeof(g_app.crypto_status), status_text);
    if (strcmp(g_app.crypto_price, "Laden...") == 0) {
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
    if (CRYPTO_PRICE_PREFIX[0]) return CRYPTO_PRICE_PREFIX;
    if (strcasecmp(CRYPTO_QUOTE_SYMBOL, "USD") == 0) return "$ ";
    if (strcasecmp(CRYPTO_QUOTE_SYMBOL, "EUR") == 0) return "\xe2\x82\xac ";
    if (strcasecmp(CRYPTO_QUOTE_SYMBOL, "GBP") == 0) return "\xc2\xa3 ";
    if (strcasecmp(CRYPTO_QUOTE_SYMBOL, "JPY") == 0) return "\xc2\xa5 ";
    if (strcasecmp(CRYPTO_QUOTE_SYMBOL, "BTC") == 0) return "\xe2\x82\xbf ";
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
    snprintf(price, sizeof(price), "%s", amount);
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
    if (strcmp(CRYPTO_CHART_TIMEFRAME, "15M") == 0) return 900;
    if (strcmp(CRYPTO_CHART_TIMEFRAME, "1H") == 0) return 3600;
    if (strcmp(CRYPTO_CHART_TIMEFRAME, "6H") == 0) return 21600;
    return BTC_CANDLE_SECONDS;
}

int crypto_chart_candle_count(void)
{
    return BTC_DAY_CANDLE_COUNT;
}

const char *crypto_chart_timeframe_label(void)
{
    if (strcmp(CRYPTO_CHART_TIMEFRAME, "15M") == 0 ||
        strcmp(CRYPTO_CHART_TIMEFRAME, "1H") == 0 ||
        strcmp(CRYPTO_CHART_TIMEFRAME, "6H") == 0 ||
        strcmp(CRYPTO_CHART_TIMEFRAME, "1D") == 0) {
        return CRYPTO_CHART_TIMEFRAME;
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
    if (!client) return false;
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
        return false;
    }
    if (!buf.data) {
        buf.data = calloc(1, 1);
    }
    *out = buf.data;
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

static bool time_due(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

static void weather_station_name(const cJSON *sources, int source_id, char *out, size_t out_size)
{
    if (!cJSON_IsArray(sources) || source_id < 0) return;
    const cJSON *source = NULL;
    cJSON_ArrayForEach(source, sources) {
        if ((int)json_num(json_obj(source, "id"), -1) == source_id) {
            app_set_str(out, out_size, json_str(json_obj(source, "station_name"), ""));
            return;
        }
    }
}

static int weather_priority_code_from_text(const char *text)
{
    if (!text) return -1;
    char lower[64];
    size_t n = strlen(text);
    if (n >= sizeof(lower)) n = sizeof(lower) - 1;
    for (size_t i = 0; i < n; i++) lower[i] = (char)tolower((unsigned char)text[i]);
    lower[n] = '\0';

    if (strstr(lower, "thunder") || strstr(lower, "storm") || strstr(lower, "gewitter")) return 95;
    if (strstr(lower, "snow") || strstr(lower, "sleet") || strstr(lower, "hail") || strstr(lower, "schnee")) return 71;
    if (strstr(lower, "rain") || strstr(lower, "drizzle") || strstr(lower, "regen")) return 61;
    if (strstr(lower, "fog") || strstr(lower, "mist") || strstr(lower, "nebel")) return 45;
    return -1;
}

static int weather_code_from_text(const char *text)
{
    if (!text) return -1;
    char lower[64];
    size_t n = strlen(text);
    if (n >= sizeof(lower)) n = sizeof(lower) - 1;
    for (size_t i = 0; i < n; i++) lower[i] = (char)tolower((unsigned char)text[i]);
    lower[n] = '\0';

    if (strstr(lower, "thunder") || strstr(lower, "storm") || strstr(lower, "gewitter")) return 95;
    if (strstr(lower, "snow") || strstr(lower, "sleet") || strstr(lower, "hail") || strstr(lower, "schnee")) return 71;
    if (strstr(lower, "rain") || strstr(lower, "drizzle") || strstr(lower, "regen")) return 61;
    if (strstr(lower, "fog") || strstr(lower, "mist") || strstr(lower, "nebel")) return 45;
    if (strstr(lower, "partly")) return 2;
    if (strstr(lower, "cloud") || strstr(lower, "overcast") || strstr(lower, "wolke")) return 3;
    if (strstr(lower, "clear") || strstr(lower, "sun") || strstr(lower, "dry") || strstr(lower, "sonne")) return 0;
    return -1;
}

static bool weather_has_recent_sunshine(const cJSON *weather)
{
    return json_num(json_obj(weather, "sunshine_30"), 0.0) >= 5.0 ||
           json_num(json_obj(weather, "sunshine_60"), 0.0) >= 10.0;
}

static bool weather_has_recent_precipitation(const cJSON *weather)
{
    return json_num(json_obj(weather, "precipitation_10"), 0.0) > 0.0 ||
           json_num(json_obj(weather, "precipitation_30"), 0.0) >= 0.1 ||
           json_num(json_obj(weather, "precipitation_60"), 0.0) >= 0.1;
}

static int weather_code_from_brightsky(const cJSON *weather, const char *icon, const char *condition)
{
    int code = weather_priority_code_from_text(condition);
    if (code >= 0) return code;

    code = weather_priority_code_from_text(icon);
    if (code >= 0) return code;

    if (weather_has_recent_precipitation(weather)) return 61;

    code = weather_code_from_text(icon);
    if (code < 0) code = weather_code_from_text(condition);
    if (code == 3 && weather_has_recent_sunshine(weather)) code = 2;
    return code;
}

static bool fetch_weather(void)
{
    char url[160];
    snprintf(url, sizeof(url),
             "https://api.brightsky.dev/current_weather?lat=%.6f&lon=%.6f",
             (double)LOCATION_LATITUDE, (double)LOCATION_LONGITUDE);

    char *payload = NULL;
    if (!http_get(url, true, NULL, NULL, &payload)) {
        app_lock();
        app_set_str(g_app.weather_status, sizeof(g_app.weather_status), "DWD: HTTP");
        app_unlock();
        return false;
    }

    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) {
        app_lock();
        app_set_str(g_app.weather_status, sizeof(g_app.weather_status), "DWD: JSON");
        app_unlock();
        return false;
    }

    const cJSON *weather = json_obj(root, "weather");
    double temp = json_num(json_obj(weather, "temperature"), NAN);
    const char *icon = json_str(json_obj(weather, "icon"), "");
    const char *condition = json_str(json_obj(weather, "condition"), "");
    int code = weather_code_from_brightsky(weather, icon, condition);

    char station[48] = "";
    int source_id = (int)json_num(json_obj(json_obj(weather, "fallback_source_ids"), "temperature"), -1);
    if (source_id < 0) source_id = (int)json_num(json_obj(weather, "source_id"), -1);
    weather_station_name(json_obj(root, "sources"), source_id, station, sizeof(station));
    if (!station[0]) {
        weather_station_name(json_obj(root, "sources"),
                             (int)json_num(json_obj(weather, "source_id"), -1),
                             station, sizeof(station));
    }

    app_lock();
    if (isfinite(temp)) snprintf(g_app.current_temp, sizeof(g_app.current_temp), "%.1f", temp);
    if (station[0]) app_set_str(g_app.weather_location, sizeof(g_app.weather_location), station);
    if (code >= 0) g_app.weather_code = code;
    app_set_str(g_app.weather_status, sizeof(g_app.weather_status), "WETTER: DWD");
    app_unlock();

    cJSON_Delete(root);
    return true;
}

static bool fetch_price(void)
{
    char url[128];
    snprintf(url, sizeof(url), "https://api.coinbase.com/v2/prices/%s-%s/spot",
             CRYPTO_BASE_SYMBOL, CRYPTO_QUOTE_SYMBOL);
    char *payload = NULL;
    int http_status = 0;
    if (!http_get(url, false, CRYPTO_HTTP_USER_AGENT, &http_status, &payload)) {
        char status_text[32];
        if (http_status > 0) {
            snprintf(status_text, sizeof(status_text), "%s HTTP %d", CRYPTO_BASE_SYMBOL, http_status);
        } else {
            snprintf(status_text, sizeof(status_text), "%s HTTP", CRYPTO_BASE_SYMBOL);
        }
        crypto_set_error_state(status_text);
        return false;
    }

    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) {
        char status_text[32];
        snprintf(status_text, sizeof(status_text), "%s JSON", CRYPTO_BASE_SYMBOL);
        crypto_set_error_state(status_text);
        return false;
    }
    const char *amount = json_str(json_obj(json_obj(root, "data"), "amount"), "");
    float live = amount[0] ? strtof(amount, NULL) : 0.0f;
    if (live <= 0.0f) {
        cJSON_Delete(root);
        char status_text[32];
        snprintf(status_text, sizeof(status_text), "%s PREIS", CRYPTO_BASE_SYMBOL);
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
             CRYPTO_SERVICE_NAME, CRYPTO_QUOTE_SYMBOL);
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
             (double)volume, CRYPTO_BASE_SYMBOL);
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
             CRYPTO_BASE_SYMBOL, CRYPTO_QUOTE_SYMBOL, (unsigned long)granularity, start_iso, end_iso);

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
    char url[128];
    snprintf(url, sizeof(url), "https://api.exchange.coinbase.com/products/%s-%s/stats",
             CRYPTO_BASE_SYMBOL, CRYPTO_QUOTE_SYMBOL);

    char *payload = NULL;
    int http_status = 0;
    if (!http_get(url, false, CRYPTO_HTTP_USER_AGENT, &http_status, &payload)) {
        ESP_LOGW(TAG, "%s stats failed status=%d", CRYPTO_BASE_SYMBOL, http_status);
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

static void fmt_temp_pair(char *out, size_t out_size, double current, double target)
{
    if (isfinite(target) && target > 0.0) snprintf(out, out_size, "%.0f/%.0f C", current, target);
    else if (isfinite(current)) snprintf(out, out_size, "%.0f C", current);
    else snprintf(out, out_size, "--");
}

static void fmt_duration(char *out, size_t out_size, double seconds)
{
    if (!isfinite(seconds) || seconds <= 0.0) {
        snprintf(out, out_size, "--");
        return;
    }
    int s = (int)seconds;
    int h = s / 3600;
    int m = (s % 3600) / 60;
    if (h > 0) snprintf(out, out_size, "%dh %02dm", h, m);
    else snprintf(out, out_size, "%dm", m);
}

static const char *klipper_state_de(const char *state)
{
    if (!state) return "--";
    if (strcmp(state, "printing") == 0) return "DRUCKT";
    if (strcmp(state, "paused") == 0) return "PAUSE";
    if (strcmp(state, "complete") == 0) return "FERTIG";
    if (strcmp(state, "error") == 0) return "FEHLER";
    if (strcmp(state, "standby") == 0) return "STANDBY";
    return "BEREIT";
}

static bool fetch_klipper(void)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/printer/objects/query?webhooks&print_stats&display_status&extruder&heater_bed",
             KLIPPER_BASE_URL);
    char *payload = NULL;
    if (!http_get(url, false, NULL, NULL, &payload)) {
        app_lock();
        g_app.klipper_available = false;
        g_app.klipper_host_available = false;
        app_set_str(g_app.klipper_status, sizeof(g_app.klipper_status), "Moonraker nicht erreichbar");
        app_unlock();
        return false;
    }

    cJSON *root = cJSON_Parse(payload);
    free(payload);
    if (!root) return false;

    const cJSON *status = json_obj(json_obj(root, "result"), "status");
    const cJSON *print_stats = json_obj(status, "print_stats");
    const cJSON *display = json_obj(status, "display_status");
    const cJSON *extruder = json_obj(status, "extruder");
    const cJSON *bed = json_obj(status, "heater_bed");
    const char *raw_state = json_str(json_obj(print_stats, "state"), "standby");
    const char *filename = json_str(json_obj(print_stats, "filename"), "");
    double progress = json_num(json_obj(display, "progress"), NAN);

    char progress_text[8];
    if (isfinite(progress)) snprintf(progress_text, sizeof(progress_text), "%.0f%%", progress * 100.0);
    else snprintf(progress_text, sizeof(progress_text), "--");

    char nozzle[24], bed_text[24], duration[40], file[96], msg[64];
    fmt_temp_pair(nozzle, sizeof(nozzle),
                  json_num(json_obj(extruder, "temperature"), NAN),
                  json_num(json_obj(extruder, "target"), NAN));
    fmt_temp_pair(bed_text, sizeof(bed_text),
                  json_num(json_obj(bed, "temperature"), NAN),
                  json_num(json_obj(bed, "target"), NAN));
    fmt_duration(duration, sizeof(duration), json_num(json_obj(print_stats, "print_duration"), NAN));
    app_set_str(file, sizeof(file), filename[0] ? filename : "Kein Job");
    app_set_str(msg, sizeof(msg), json_str(json_obj(display, "message"), ""));

    app_lock();
    g_app.klipper_available = true;
    g_app.klipper_host_available = true;
    app_set_str(g_app.klipper_state, sizeof(g_app.klipper_state), klipper_state_de(raw_state));
    app_set_str(g_app.klipper_file, sizeof(g_app.klipper_file), file);
    app_set_str(g_app.klipper_progress, sizeof(g_app.klipper_progress), progress_text);
    app_set_str(g_app.klipper_nozzle, sizeof(g_app.klipper_nozzle), nozzle);
    app_set_str(g_app.klipper_bed, sizeof(g_app.klipper_bed), bed_text);
    app_set_str(g_app.klipper_duration, sizeof(g_app.klipper_duration), duration);
    app_set_str(g_app.klipper_status, sizeof(g_app.klipper_status), "Moonraker OK");
    app_set_str(g_app.klipper_display_message, sizeof(g_app.klipper_display_message), msg);
    app_unlock();

    cJSON_Delete(root);
    return true;
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
    }
}

static void start_sntp(void)
{
    setenv("TZ", TIMEZONE_POSIX, 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
}

static void fetch_task(void *arg)
{
    (void)arg;
    uint32_t next_weather = 0;
    uint32_t next_price = 0;
    uint32_t next_candles = 0;
    uint32_t next_stats = 0;
    uint32_t next_klipper = 0;
    uint32_t last_api_request = 0;
    bool schedule_initialized = false;

    while (true) {
        uint32_t now = (uint32_t)(esp_log_timestamp());
        bool connected;
        app_lock();
        connected = g_app.wifi_connected;
        app_unlock();

        if (!connected) {
            schedule_initialized = false;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!schedule_initialized) {
            uint32_t start = now + WIFI_STABLE_BEFORE_FETCH_MS;
            next_price = start;
            next_candles = start + 5000U;
            next_stats = start + 7000U;
            next_weather = start + 10000U;
            next_klipper = start + 24000U;
            last_api_request = 0;
            schedule_initialized = true;
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

void net_start(void)
{
    if (!WIFI_SSID[0]) {
        app_lock();
        g_app.wifi_connected = false;
        app_set_str(g_app.weather_status, sizeof(g_app.weather_status), "WLAN: keine SSID");
        app_set_str(g_app.crypto_status, sizeof(g_app.crypto_status), "WLAN fehlt");
        app_unlock();
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
    snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), "%s", WIFI_SSID);
    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), "%s", WIFI_PASSWORD);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    start_sntp();

    if (!s_fetch_task) {
        xTaskCreatePinnedToCore(fetch_task, "net_fetch", FETCH_TASK_STACK, NULL,
                                FETCH_TASK_PRIO, &s_fetch_task, 1);
    }
    ESP_LOGI(TAG, "WiFi/API services started");
}
