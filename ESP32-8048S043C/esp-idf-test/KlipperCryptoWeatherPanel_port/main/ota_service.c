// Implementierung siehe ota_service.h.
//
// Layout der GitHub-Release-Erkennung:
//   - GET https://api.github.com/repos/OWNER/REPO/releases?per_page=20
//   - Filter: tag_name beginnt mit "APP_FW_NAME-", draft=false, prerelease=false.
//   - Aus jedem passenden Release wird der Suffix hinter "APP_FW_NAME-" als
//     Version interpretiert und per strcmp gegen APP_FW_VERSION verglichen.
//     Format YYYYMMDD oder semver sortiert mit strcmp lexikographisch korrekt.
//   - Asset wird nur akzeptiert, wenn sein Name sowohl APP_FW_BOARD als auch
//     APP_FW_LANG enthaelt (Lang als Token, also "-DE." / "-DE-" / "-DE_").
//   - Asset-URL wird gespeichert; bei start_install ruft esp_https_ota auf
//     genau diese URL und reboot bei Erfolg.

#include "app_state.h"
#include "ota_service.h"
#include "version.h"

#include "cJSON.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ota";

#define OTA_TASK_STACK         12288
#define OTA_TASK_PRIO          4
#define OTA_HTTP_TIMEOUT_MS    15000
#define OTA_USER_AGENT         "ESP32-KCWP-OTA"
#define OTA_API_RELEASES_FMT   "https://api.github.com/repos/%s/%s/releases?per_page=20"
#define OTA_API_LATEST_FMT     "https://api.github.com/repos/%s/%s/releases/latest"
#define OTA_ASSET_URL_MAX      384
#define OTA_PERFORM_DELAY_MS   80

typedef struct {
    char *data;
    int   len;
    int   cap;
} ota_http_buf_t;

typedef struct {
    int last_status;
} ota_install_http_ctx_t;

static SemaphoreHandle_t s_mutex;
static TaskHandle_t      s_task;       // != NULL = check oder install laeuft
static ota_status_t      s_status;
static char              s_asset_url[OTA_ASSET_URL_MAX];
static volatile bool     s_installing;

static void status_set(ota_state_t state, const char *msg)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.state = state;
    if (msg) {
        size_t n = strlen(msg);
        if (n >= sizeof(s_status.message)) n = sizeof(s_status.message) - 1;
        memcpy(s_status.message, msg, n);
        s_status.message[n] = '\0';
    }
    xSemaphoreGive(s_mutex);
}

static void status_set_progress(int percent)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    s_status.progress_percent = percent;
    xSemaphoreGive(s_mutex);
}

static void status_set_available(const char *version)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.state = OTA_STATE_UPDATE_AVAILABLE;
    size_t n = strlen(version);
    if (n >= sizeof(s_status.available_version)) n = sizeof(s_status.available_version) - 1;
    memcpy(s_status.available_version, version, n);
    s_status.available_version[n] = '\0';
    snprintf(s_status.message, sizeof(s_status.message),
             "Update %s verfuegbar", s_status.available_version);
    xSemaphoreGive(s_mutex);
}

static esp_err_t ota_http_event(esp_http_client_event_t *evt)
{
    ota_http_buf_t *buf = (ota_http_buf_t *)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || !buf || !evt->data || evt->data_len <= 0) {
        return ESP_OK;
    }
    int needed = buf->len + evt->data_len + 1;
    if (needed > buf->cap) {
        int new_cap = buf->cap ? buf->cap * 2 : 4096;
        while (new_cap < needed) new_cap *= 2;
        char *next = realloc(buf->data, new_cap);
        if (!next) return ESP_ERR_NO_MEM;
        buf->data = next;
        buf->cap  = new_cap;
    }
    memcpy(buf->data + buf->len, evt->data, evt->data_len);
    buf->len += evt->data_len;
    buf->data[buf->len] = '\0';
    return ESP_OK;
}

static bool ota_http_get(const char *url, char **out, int *status_out)
{
    *out = NULL;
    if (status_out) *status_out = 0;
    ota_http_buf_t buf = {0};
    esp_http_client_config_t cfg = {
        .url               = url,
        .timeout_ms        = OTA_HTTP_TIMEOUT_MS,
        .event_handler     = ota_http_event,
        .user_data         = &buf,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;
    esp_http_client_set_header(client, "User-Agent", OTA_USER_AGENT);
    esp_http_client_set_header(client, "Accept",     "application/vnd.github+json");
    esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");

    esp_err_t err  = esp_http_client_perform(client);
    int       code = esp_http_client_get_status_code(client);
    if (status_out) *status_out = code;
    esp_http_client_cleanup(client);

    if (err != ESP_OK || code < 200 || code >= 300) {
        ESP_LOGW(TAG, "GET failed status=%d err=%s url=%s", code, esp_err_to_name(err), url);
        free(buf.data);
        return false;
    }
    *out = buf.data ? buf.data : calloc(1, 1);
    return *out != NULL;
}

static esp_err_t ota_install_http_event(esp_http_client_event_t *evt)
{
    ota_install_http_ctx_t *ctx = (ota_install_http_ctx_t *)evt->user_data;
    if (!ctx) {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_STATUS_CODE && evt->data &&
        evt->data_len == sizeof(int)) {
        ctx->last_status = *(int *)evt->data;
        ESP_LOGI(TAG, "OTA HTTP status=%d", ctx->last_status);
    }
    return ESP_OK;
}

static esp_err_t ota_install_client_init(esp_http_client_handle_t client)
{
    // GitHub erwartet einen User-Agent; application/octet-stream triggert den Asset-Download.
    esp_err_t err = esp_http_client_set_header(client, "User-Agent", OTA_USER_AGENT);
    if (err != ESP_OK) return err;
    err = esp_http_client_set_header(client, "Accept", "application/octet-stream");
    if (err != ESP_OK) return err;
    err = esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
    if (err != ESP_OK) return err;
    return ESP_OK;
}

static void expected_ota_asset_name(char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    snprintf(out, out_size, "%s-%s-%s.bin", APP_FW_NAME, APP_FW_BOARD, APP_FW_LANG);
}

static bool asset_matches(const char *name)
{
    if (!name) return false;
    char expected[96];
    expected_ota_asset_name(expected, sizeof(expected));
    return strcmp(name, expected) == 0;
}

// Erwartet tag_name in der Form "APP_FW_NAME-<version>". Liefert Pointer auf
// den Versions-Suffix oder NULL.
static const char *tag_version_suffix(const char *tag)
{
    if (!tag) return NULL;
    size_t plen = strlen(APP_FW_NAME);
    if (strncmp(tag, APP_FW_NAME, plen) != 0) return NULL;
    if (tag[plen] != '-')                    return NULL;
    return tag + plen + 1;
}

// Pruefft ein einzelnes Release-Objekt. Wenn es nicht draft/prerelease ist,
// der Tag mit "APP_FW_NAME-" beginnt und ein passendes Asset enthaelt, wird
// dessen Version/URL in best_*/best_url uebernommen sofern hoeher als bisher.
static void process_release_obj(cJSON *rel,
                                char *best_version, size_t bv_size,
                                char *best_url,     size_t bu_size)
{
    if (!cJSON_IsObject(rel)) return;

    cJSON *draft = cJSON_GetObjectItemCaseSensitive(rel, "draft");
    if (cJSON_IsBool(draft) && cJSON_IsTrue(draft)) return;
    cJSON *pre = cJSON_GetObjectItemCaseSensitive(rel, "prerelease");
    if (cJSON_IsBool(pre) && cJSON_IsTrue(pre)) return;

    cJSON *tag = cJSON_GetObjectItemCaseSensitive(rel, "tag_name");
    const char *ver = tag_version_suffix(cJSON_IsString(tag) ? tag->valuestring : NULL);
    if (!ver || !*ver) return;

    if (best_version[0] && strcmp(ver, best_version) <= 0) return;

    cJSON *assets = cJSON_GetObjectItemCaseSensitive(rel, "assets");
    if (!cJSON_IsArray(assets)) return;
    const char *asset_url = NULL;
    char expected_name[96];
    expected_ota_asset_name(expected_name, sizeof(expected_name));
    int an = cJSON_GetArraySize(assets);
    for (int j = 0; j < an; ++j) {
        cJSON *a = cJSON_GetArrayItem(assets, j);
        cJSON *name = cJSON_GetObjectItemCaseSensitive(a, "name");
        if (!cJSON_IsString(name)) continue;
        if (!asset_matches(name->valuestring)) continue;
        ESP_LOGI(TAG, "OTA Asset gefunden: %s", name->valuestring);
        // Browser-Download-URL wird vor OTA separat auf den finalen
        // Redirect aufgeloest; das ist robuster als esp_https_ota mit 302.
        cJSON *du = cJSON_GetObjectItemCaseSensitive(a, "browser_download_url");
        if (cJSON_IsString(du) && du->valuestring) {
            asset_url = du->valuestring;
            break;
        }
        cJSON *api_url = cJSON_GetObjectItemCaseSensitive(a, "url");
        if (cJSON_IsString(api_url) && api_url->valuestring) {
            asset_url = api_url->valuestring;
            break;
        }
    }
    if (!asset_url) {
        ESP_LOGI(TAG, "Release ohne OTA-App-Asset: erwartet %s", expected_name);
        return;
    }

    size_t vlen = strlen(ver);
    size_t ulen = strlen(asset_url);
    if (vlen >= bv_size || ulen >= bu_size) return;
    memcpy(best_version, ver, vlen + 1);
    memcpy(best_url,     asset_url, ulen + 1);
}

static void ota_check_task(void *arg)
{
    (void)arg;

    if (APP_OTA_REPO_OWNER[0] == '\0' || APP_OTA_REPO_NAME[0] == '\0') {
        status_set(OTA_STATE_NOT_CONFIGURED, "OTA nicht konfiguriert");
        goto done;
    }

    char url[160];
    char *body = NULL;
    int   http_status = 0;

    char best_version[24] = {0};
    char best_url[256]    = {0};

    // Schritt 1: Releases-Liste abfragen und alle passenden durchgehen.
    snprintf(url, sizeof(url), OTA_API_RELEASES_FMT,
             APP_OTA_REPO_OWNER, APP_OTA_REPO_NAME);
    if (!ota_http_get(url, &body, &http_status)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "GitHub HTTP %d", http_status);
        status_set(OTA_STATE_ERROR, msg);
        goto done;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    body = NULL;
    if (cJSON_IsArray(root)) {
        int n = cJSON_GetArraySize(root);
        for (int i = 0; i < n; ++i) {
            process_release_obj(cJSON_GetArrayItem(root, i),
                                best_version, sizeof(best_version),
                                best_url,     sizeof(best_url));
        }
    }
    cJSON_Delete(root);

    // Schritt 2: Fallback. GitHub's /releases-Listing kann fuer einzelne Repos
    // leer zurueckkommen (CDN-Cache-Effekt nach urspruenglichem
    // --latest=false). /releases/latest funktioniert in dem Fall noch und
    // liefert genau das Release, das der User als "Latest" markiert hat.
    if (!best_version[0]) {
        snprintf(url, sizeof(url), OTA_API_LATEST_FMT,
                 APP_OTA_REPO_OWNER, APP_OTA_REPO_NAME);
        if (ota_http_get(url, &body, &http_status)) {
            cJSON *latest = cJSON_Parse(body);
            free(body);
            body = NULL;
            process_release_obj(latest,
                                best_version, sizeof(best_version),
                                best_url,     sizeof(best_url));
            cJSON_Delete(latest);
        }
    }

    if (!best_version[0]) {
        status_set(OTA_STATE_ERROR, "Kein passendes Release gefunden");
        goto done;
    }

    if (strcmp(best_version, APP_FW_VERSION) <= 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Aktuell (%s)", APP_FW_VERSION);
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_status.state = OTA_STATE_NO_UPDATE;
        memcpy(s_status.available_version, best_version, strlen(best_version) + 1);
        memcpy(s_status.message, msg, strlen(msg) + 1);
        xSemaphoreGive(s_mutex);
        goto done;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(s_asset_url, best_url, strlen(best_url) + 1);
    xSemaphoreGive(s_mutex);
    status_set_available(best_version);

done:
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_task = NULL;
    xSemaphoreGive(s_mutex);
    vTaskDelete(NULL);
}

static void ota_install_task(void *arg)
{
    (void)arg;

    char url_copy[sizeof(s_asset_url)];
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(url_copy, s_asset_url, sizeof(url_copy));
    xSemaphoreGive(s_mutex);

    if (!url_copy[0]) {
        status_set(OTA_STATE_ERROR, "Keine Asset-URL");
        goto done;
    }

    ESP_LOGI(TAG, "OTA von %s", url_copy);
    s_installing = true;
    status_set(OTA_STATE_DOWNLOADING, "Starte OTA...");
    status_set_progress(0);
    net_pause_fetches(true);
    ui_firmware_info_enter_ota_mode();
    // Dem RGB-Panel Zeit geben, den ruhigen OTA-Screen komplett zu uebernehmen.
    vTaskDelay(pdMS_TO_TICKS(300));

    ota_install_http_ctx_t http_ctx = {0};
    esp_http_client_config_t http_cfg = {
        .url               = url_copy,
        .timeout_ms        = 30000,
        .user_agent        = OTA_USER_AGENT,
        .event_handler     = ota_install_http_event,
        .user_data         = &http_ctx,
        .buffer_size       = 2048,
        .buffer_size_tx    = 1024,
        // Zertifikatspruefung fuer den Firmware-Stream ist deaktiviert
        // (CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP + CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY),
        // damit der grosse GitHub-Download nicht in mbedTLS/X.509-Heap laeuft.
        .keep_alive_enable = true,
        .disable_auto_redirect = false,
        .max_redirection_count = 10,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
        .http_client_init_cb = ota_install_client_init,
    };

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if (err != ESP_OK || !handle) {
        char msg[64];
        if (http_ctx.last_status > 0) {
            snprintf(msg, sizeof(msg), "OTA-Start HTTP %d: %s",
                     http_ctx.last_status, esp_err_to_name(err));
        } else {
            snprintf(msg, sizeof(msg), "OTA-Start: %s", esp_err_to_name(err));
        }
        status_set(OTA_STATE_ERROR, msg);
        goto done;
    }
    status_set(OTA_STATE_DOWNLOADING, "Lade Firmware...");

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(handle, &app_desc);
    if (err != ESP_OK) {
        esp_https_ota_abort(handle);
        char msg[64];
        snprintf(msg, sizeof(msg), "Image-Header: %s", esp_err_to_name(err));
        status_set(OTA_STATE_ERROR, msg);
        goto done;
    }
    ESP_LOGI(TAG, "OTA Image: project=%s version=%s", app_desc.project_name,
             app_desc.version);

    int total = esp_https_ota_get_image_size(handle);
    int last_progress = 0;
    TickType_t last_progress_tick = xTaskGetTickCount();
    while ((err = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        bool update_visual = false;
        int read = esp_https_ota_get_image_len_read(handle);
        if (total > 0) {
            int percent = (int)((int64_t)read * 100 / total);
            TickType_t now = xTaskGetTickCount();
            // UI-Redraws waehrend Flash-Writes stark drosseln: 5%-Schritte
            // oder spaetestens einmal pro Sekunde.
            if (percent >= last_progress + 5 ||
                now - last_progress_tick >= pdMS_TO_TICKS(1000)) {
                status_set_progress(percent);
                last_progress = percent;
                last_progress_tick = now;
                update_visual = true;
            }
        }
        // Kurze Luft zwischen HTTP/Flash-Chunks: reduziert RGB/PSRAM-Artefakte,
        // ohne den OTA-Download komplett zu verstecken.
        vTaskDelay(pdMS_TO_TICKS(OTA_PERFORM_DELAY_MS));
        if (update_visual) {
            ui_firmware_info_ota_progress(last_progress);
        }
    }

    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
        int status = esp_https_ota_get_status_code(handle);
        int read = esp_https_ota_get_image_len_read(handle);
        esp_https_ota_abort(handle);
        char msg[64];
        if (status > 0) {
            snprintf(msg, sizeof(msg), "Download HTTP %d: %s", status,
                     esp_err_to_name(err));
        } else if (read >= 0) {
            snprintf(msg, sizeof(msg), "Download %dB: %s", read,
                     esp_err_to_name(err));
        } else {
            snprintf(msg, sizeof(msg), "Download: %s", esp_err_to_name(err));
        }
        status_set(OTA_STATE_ERROR, msg);
        goto done;
    }

    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "OTA-Finish: %s", esp_err_to_name(err));
        status_set(OTA_STATE_ERROR, msg);
        goto done;
    }

    status_set_progress(100);
    ui_firmware_info_ota_progress(100);
    status_set(OTA_STATE_SUCCESS, "Erfolg, starte neu...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    // unreachable

done:
    ui_firmware_info_leave_ota_mode();
    s_installing = false;
    net_pause_fetches(false);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_task = NULL;
    xSemaphoreGive(s_mutex);
    vTaskDelete(NULL);
}

void ota_service_init(void)
{
    if (s_mutex) return;
    s_mutex = xSemaphoreCreateMutex();
    s_status.state = OTA_STATE_IDLE;
    s_status.message[0] = '\0';
    s_status.available_version[0] = '\0';
    s_status.progress_percent = 0;
    s_asset_url[0] = '\0';

    // Falls Boot pending verify war: jetzt als gut markieren. Wenn der Code
    // bis hier laeuft, ist die Firmware genuegend lauffaehig.
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t   img_state;
    if (running && esp_ota_get_state_partition(running, &img_state) == ESP_OK) {
        if (img_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "OTA pending verify -> mark valid");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
}

bool ota_service_start_check(void)
{
    if (!s_mutex) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_task) { xSemaphoreGive(s_mutex); return false; }
    s_status.state = OTA_STATE_CHECKING;
    s_status.message[0] = '\0';
    s_status.progress_percent = 0;
    snprintf(s_status.message, sizeof(s_status.message), "Suche nach Update...");
    BaseType_t ok = xTaskCreate(ota_check_task, "ota_check", OTA_TASK_STACK, NULL,
                                OTA_TASK_PRIO, &s_task);
    if (ok != pdPASS) {
        s_task = NULL;
        s_status.state = OTA_STATE_ERROR;
        snprintf(s_status.message, sizeof(s_status.message), "Task-Start fehlgeschlagen");
        xSemaphoreGive(s_mutex);
        return false;
    }
    xSemaphoreGive(s_mutex);
    return true;
}

bool ota_service_start_install(void)
{
    if (!s_mutex) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_task || s_status.state != OTA_STATE_UPDATE_AVAILABLE || !s_asset_url[0]) {
        xSemaphoreGive(s_mutex);
        return false;
    }
    s_status.state = OTA_STATE_DOWNLOADING;
    s_status.progress_percent = 0;
    snprintf(s_status.message, sizeof(s_status.message), "Starte Update...");
    BaseType_t ok = xTaskCreate(ota_install_task, "ota_install", OTA_TASK_STACK, NULL,
                                OTA_TASK_PRIO, &s_task);
    if (ok != pdPASS) {
        s_task = NULL;
        s_status.state = OTA_STATE_ERROR;
        snprintf(s_status.message, sizeof(s_status.message), "Task-Start fehlgeschlagen");
        xSemaphoreGive(s_mutex);
        return false;
    }
    xSemaphoreGive(s_mutex);
    return true;
}

void ota_service_get_status(ota_status_t *out)
{
    if (!out) return;
    if (!s_mutex) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_mutex);
}

bool ota_service_is_installing(void)
{
    return s_installing;
}
