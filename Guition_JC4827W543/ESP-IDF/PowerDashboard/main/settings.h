#pragma once

// Persistent Settings (NVS-backed).
//
// Schema in settings_schema.h - hier nur API. Aufrufer ziehen Werte mit
// settings_get_str / _int / _float und schreiben mit settings_set_*.
//
// settings_is_configured() prueft alle als "required" markierten Felder.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Compile-Time-Defaults. Werden in settings_schema.h als Default-Werte
// referenziert - NVS-Eintraege haben Vorrang.
// config_private.h ist gitignored. Fehlt sie, greifen die unteren Fallbacks
// und das Geraet startet beim Boot im Provisioning-Modus (leere WIFI_SSID).
#if defined(__has_include)
#  if __has_include("config_private.h")
#    include "config_private.h"
#  endif
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef SHELLY_IP
#define SHELLY_IP "192.168.178.24"
#endif
#ifndef SHELLY_POLL_INTERVAL_MS
#define SHELLY_POLL_INTERVAL_MS 2000
#endif
#ifndef TARIFF_CT_PER_KWH
#define TARIFF_CT_PER_KWH 30.86f
#endif
#ifndef TIMEZONE_POSIX
#define TIMEZONE_POSIX "CET-1CEST,M3.5.0,M10.5.0/3"
#endif
#ifndef SNTP_SERVER
#define SNTP_SERVER "pool.ntp.org"
#endif
#ifndef DISPLAY_ROTATION
#define DISPLAY_ROTATION 2
#endif
#ifndef DISPLAY_BRIGHTNESS
#define DISPLAY_BRIGHTNESS 160
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SETTING_TYPE_STR,
    SETTING_TYPE_INT,
    SETTING_TYPE_FLT,
} setting_type_t;

// Stabile IDs fuer alle Settings - aus settings_schema.h generiert.
// Hinweis: Macro-Parameter mit Unterstrich-Prefix, damit sie nicht mit
// gleichnamigen Struct-Feldern (id, label, secret, required) bei
// Designated-Initializer-Substitution kollidieren.
enum {
#define SETTING_STR(_id, _k, _l, _d, _mlen, _sec, _req) _id,
#define SETTING_INT(_id, _k, _l, _d, _mn, _mx, _req)    _id,
#define SETTING_FLT(_id, _k, _l, _d, _mn, _mx, _req)    _id,
#define SETTING_GROUP(_name)
#include "settings_schema.h"
};

typedef struct {
    int             id;
    const char     *nvs_key;
    const char     *label;
    setting_type_t  type;
    const char     *group;
    // String-Felder
    const char     *default_str;
    int             max_len;
    bool            secret;
    // Numeric-Felder
    int32_t         default_int;
    int32_t         min_int;
    int32_t         max_int;
    float           default_flt;
    float           min_flt;
    float           max_flt;
    // Allgemein
    bool            required;
} setting_meta_t;

// Konstante Liste aller Settings (in Reihenfolge des Schemas).
const setting_meta_t *settings_meta_list(size_t *count);

// Lookup per id (aus dem enum in settings_schema.h). NULL wenn unbekannt.
const setting_meta_t *settings_meta_by_id(int id);
const setting_meta_t *settings_meta_by_key(const char *nvs_key);

// Lifecycle.
void settings_init(void);

// Reset = alle NVS-Eintraege loeschen. Beim naechsten Boot greifen Defaults.
void settings_factory_reset(void);

// Expliziter QR-Setup-Start, z.B. nach WLAN-Timeout. So kann ein leerer
// NVS-String private Defaults aus config_private.h nicht versehentlich blockieren.
bool settings_force_provisioning_enabled(void);
void settings_set_force_provisioning(bool enabled);

// Mindestens ein required-Wert leer? -> not configured -> Provisioning starten.
bool settings_is_configured(void);

// String-API. Schreibt bis (out_size-1) Zeichen + '\0' in out.
// Liefert den Schema-Default zurueck wenn der Key in NVS nicht existiert.
void settings_get_str(int id, char *out, size_t out_size);
int32_t settings_get_int(int id);
float settings_get_float(int id);

// Setter. value == NULL beim String setzt den Schema-Default zurueck.
// Numeric werden gegen min/max geclampt.
void settings_set_str(int id, const char *value);
void settings_set_int(int id, int32_t value);
void settings_set_float(int id, float value);

#ifdef __cplusplus
}
#endif
