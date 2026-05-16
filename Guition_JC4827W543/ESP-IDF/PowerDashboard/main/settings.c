// Settings-Storage. Liest/Schreibt NVS, faellt auf Schema-Defaults zurueck.
//
// Tabelle wird aus settings_schema.h via X-Macro entfaltet. Neue Eintraege
// landen automatisch im Storage und in der Web-API.

#include "settings.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";
static const char *NVS_NS = "powerdash";
static const char *NVS_FORCE_PROV_KEY = "force_prov";

// ---------------------------------------------------------------------------
// Schema entfalten -> setting_meta_t[]
// ---------------------------------------------------------------------------

// Enum mit allen IDs kommt aus settings.h (gemeinsam mit der Public-API).

// Macro-Parameter mit Unterstrich-Prefix: der Preprocessor wuerde sonst
// auch hinter '.' Token-fuer-Token ersetzen und ".id = id" zu
// ".SETTING_WIFI_SSID = SETTING_WIFI_SSID" expandieren (Compile-Fehler).
//
// Nicht-const, weil die Gruppen-Pointer beim Init nachgetragen werden.
static setting_meta_t g_meta[] = {
#define SETTING_GROUP(_name)
#define SETTING_STR(_id, _k, _l, _d, _mlen, _sec, _req)              \
    { .id = _id, .nvs_key = _k, .label = _l,                         \
      .type = SETTING_TYPE_STR, .group = NULL,                       \
      .default_str = _d, .max_len = _mlen, .secret = (_sec),         \
      .default_int = 0, .min_int = 0, .max_int = 0,                  \
      .default_flt = 0.f, .min_flt = 0.f, .max_flt = 0.f,            \
      .required = (_req) != 0 },
#define SETTING_INT(_id, _k, _l, _d, _mn, _mx, _req)                 \
    { .id = _id, .nvs_key = _k, .label = _l,                         \
      .type = SETTING_TYPE_INT, .group = NULL,                       \
      .default_str = NULL, .max_len = 0, .secret = 0,                \
      .default_int = (_d), .min_int = (_mn), .max_int = (_mx),       \
      .default_flt = 0.f, .min_flt = 0.f, .max_flt = 0.f,            \
      .required = (_req) != 0 },
#define SETTING_FLT(_id, _k, _l, _d, _mn, _mx, _req)                 \
    { .id = _id, .nvs_key = _k, .label = _l,                         \
      .type = SETTING_TYPE_FLT, .group = NULL,                       \
      .default_str = NULL, .max_len = 0, .secret = 0,                \
      .default_int = 0, .min_int = 0, .max_int = 0,                  \
      .default_flt = (_d), .min_flt = (_mn), .max_flt = (_mx),       \
      .required = (_req) != 0 },
#include "settings_schema.h"
};
static const size_t g_meta_count = sizeof(g_meta) / sizeof(g_meta[0]);

// Gruppen-Zuordnung pro Eintrag - zweite Passage, weil SETTING_GROUP nur
// die laufende Gruppe setzt und mehrere Settings teilen kann.
static const char *g_groups[sizeof(g_meta) / sizeof(g_meta[0])];

static void settings_meta_init_groups(void)
{
    size_t i = 0;
    const char *current = "";
#define SETTING_GROUP(_name) current = (_name);
#define SETTING_STR(_id, _k, _l, _d, _mlen, _sec, _req) g_groups[i++] = current;
#define SETTING_INT(_id, _k, _l, _d, _mn, _mx, _req)    g_groups[i++] = current;
#define SETTING_FLT(_id, _k, _l, _d, _mn, _mx, _req)    g_groups[i++] = current;
#include "settings_schema.h"
    (void)current;
    for (size_t k = 0; k < g_meta_count; ++k) g_meta[k].group = g_groups[k];
}

const setting_meta_t *settings_meta_list(size_t *count)
{
    if (count) *count = g_meta_count;
    return g_meta;
}

const setting_meta_t *settings_meta_by_id(int id)
{
    for (size_t i = 0; i < g_meta_count; ++i) {
        if (g_meta[i].id == id) return &g_meta[i];
    }
    return NULL;
}

const setting_meta_t *settings_meta_by_key(const char *key)
{
    if (!key) return NULL;
    for (size_t i = 0; i < g_meta_count; ++i) {
        if (strcmp(g_meta[i].nvs_key, key) == 0) return &g_meta[i];
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// NVS-Wrapper
// ---------------------------------------------------------------------------

static bool g_inited = false;

void settings_init(void)
{
    if (g_inited) return;
    settings_meta_init_groups();
    // nvs_flash_init() wird im main schon gerufen, hier nur Namespace-Open
    // testweise, um Probleme frueh zu sehen.
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_OK) nvs_close(h);
    g_inited = true;
}

void settings_factory_reset(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGW(TAG, "Factory reset: NVS-Namespace '%s' geloescht", NVS_NS);
}

bool settings_force_provisioning_enabled(void)
{
    nvs_handle_t h;
    uint8_t enabled = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_FORCE_PROV_KEY, &enabled);
        nvs_close(h);
    }
    return enabled != 0;
}

void settings_set_force_provisioning(bool enabled)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (enabled) {
        nvs_set_u8(h, NVS_FORCE_PROV_KEY, 1);
    } else {
        nvs_erase_key(h, NVS_FORCE_PROV_KEY);
    }
    nvs_commit(h);
    nvs_close(h);
}

bool settings_is_configured(void)
{
    for (size_t i = 0; i < g_meta_count; ++i) {
        const setting_meta_t *m = &g_meta[i];
        if (!m->required) continue;
        if (m->type == SETTING_TYPE_STR) {
            char buf[96];
            settings_get_str(m->id, buf, sizeof(buf));
            if (buf[0] == '\0') return false;
        }
    }
    return true;
}

void settings_get_str(int id, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    out[0] = '\0';
    const setting_meta_t *m = settings_meta_by_id(id);
    if (!m || m->type != SETTING_TYPE_STR) return;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = out_size;
        esp_err_t err = nvs_get_str(h, m->nvs_key, out, &len);
        nvs_close(h);
        if (err == ESP_OK) {
            // Leere alte NVS-Werte sollen private Defaults nicht verdecken.
            if (out[0] != '\0' ||
                !m->required ||
                !m->default_str ||
                m->default_str[0] == '\0' ||
                settings_force_provisioning_enabled()) {
                return;
            }
        }
    }
    // Fallback Default
    strncpy(out, m->default_str ? m->default_str : "", out_size - 1);
    out[out_size - 1] = '\0';
}

int32_t settings_get_int(int id)
{
    const setting_meta_t *m = settings_meta_by_id(id);
    if (!m || m->type != SETTING_TYPE_INT) return 0;
    nvs_handle_t h;
    int32_t val = m->default_int;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, m->nvs_key, &val);
        nvs_close(h);
    }
    return val;
}

float settings_get_float(int id)
{
    const setting_meta_t *m = settings_meta_by_id(id);
    if (!m || m->type != SETTING_TYPE_FLT) return 0.f;
    // NVS hat kein float - Wir speichern als IEEE754 in i32.
    nvs_handle_t h;
    float val = m->default_flt;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        int32_t raw = 0;
        if (nvs_get_i32(h, m->nvs_key, &raw) == ESP_OK) {
            memcpy(&val, &raw, sizeof(float));
        }
        nvs_close(h);
    }
    return val;
}

void settings_set_str(int id, const char *value)
{
    const setting_meta_t *m = settings_meta_by_id(id);
    if (!m || m->type != SETTING_TYPE_STR) return;
    const char *v = value ? value : (m->default_str ? m->default_str : "");
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, m->nvs_key, v);
    nvs_commit(h);
    nvs_close(h);
    if (id == SETTING_WIFI_SSID && v[0] != '\0') {
        settings_set_force_provisioning(false);
    }
}

void settings_set_int(int id, int32_t value)
{
    const setting_meta_t *m = settings_meta_by_id(id);
    if (!m || m->type != SETTING_TYPE_INT) return;
    if (value < m->min_int) value = m->min_int;
    if (value > m->max_int) value = m->max_int;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, m->nvs_key, value);
    nvs_commit(h);
    nvs_close(h);
}

void settings_set_float(int id, float value)
{
    const setting_meta_t *m = settings_meta_by_id(id);
    if (!m || m->type != SETTING_TYPE_FLT) return;
    if (value < m->min_flt) value = m->min_flt;
    if (value > m->max_flt) value = m->max_flt;
    int32_t raw = 0;
    memcpy(&raw, &value, sizeof(float));
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, m->nvs_key, raw);
    nvs_commit(h);
    nvs_close(h);
}
