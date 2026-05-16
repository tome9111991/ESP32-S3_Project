// X-Macro Schema fuer alle persistenten Settings.
//
// Eine Zeile pro Setting. Aufrufer definieren SETTING_STR / SETTING_INT /
// SETTING_FLT vor dem Include und bekommen damit eine einheitliche Sicht auf
// alle Eintraege. Storage-Layer, Web-API und Web-UI ziehen ihre Daten aus
// dieser Datei. Neue Settings -> hier ergaenzen, sonst nichts.
//
// Felder:
//   SETTING_STR(key_id, nvs_key, label, default, maxlen, secret, required)
//   SETTING_INT(key_id, nvs_key, label, default, min, max, required)
//   SETTING_FLT(key_id, nvs_key, label, default, min, max, required)
//
// - key_id  : C-Identifier fuer enum/typedef (z.B. SETTING_WIFI_SSID)
// - nvs_key : Key im NVS-Namespace (max. 15 Zeichen!)
// - label   : Anzeige-Name in der Web-UI
// - secret  : 1 = wird in API als "***" zurueckgegeben (nicht ausgelesen)
// - required: 1 = ohne diesen Wert gilt Geraet als nicht konfiguriert
//
// "required" entscheidet, ob beim Boot Provisioning gestartet wird.

// Stubs falls Aufrufer eine Slot-Variante nicht ueberschreibt - so kann das
// File mehrfach inkludiert werden. Parameter-Namen hier sind nur Platzhalter,
// jeder Aufrufer waehlt eigene (mit Unterstrich-Prefix, damit Designated
// Initializers wie ".id" nicht versehentlich ersetzt werden).
#ifndef SETTING_STR
#define SETTING_STR(_id, _k, _l, _d, _mlen, _sec, _req)
#endif
#ifndef SETTING_INT
#define SETTING_INT(_id, _k, _l, _d, _mn, _mx, _req)
#endif
#ifndef SETTING_FLT
#define SETTING_FLT(_id, _k, _l, _d, _mn, _mx, _req)
#endif
#ifndef SETTING_GROUP
#define SETTING_GROUP(_name)
#endif

SETTING_GROUP("WLAN")
SETTING_STR(SETTING_WIFI_SSID, "wifi_ssid", "WLAN SSID",      WIFI_SSID,     32, 0, 1)
SETTING_STR(SETTING_WIFI_PASS, "wifi_pass", "WLAN Passwort",  WIFI_PASSWORD, 64, 1, 0)

SETTING_GROUP("Shelly")
SETTING_STR(SETTING_SHELLY_IP,   "shelly_ip",   "Shelly Plug IP",        SHELLY_IP, 16, 0, 1)
SETTING_INT(SETTING_SHELLY_POLL, "shelly_poll", "Pollrate (ms)",         SHELLY_POLL_INTERVAL_MS, 500, 60000, 0)

SETTING_GROUP("Tarif")
SETTING_FLT(SETTING_TARIFF, "tariff_ct", "Arbeitspreis (ct/kWh)", TARIFF_CT_PER_KWH, 0.0f, 500.0f, 0)

SETTING_GROUP("Zeit")
SETTING_STR(SETTING_TZ,  "tz",  "Zeitzone (POSIX)", TIMEZONE_POSIX, 48, 0, 0)
SETTING_STR(SETTING_NTP, "ntp", "NTP Server",       SNTP_SERVER,    48, 0, 0)

SETTING_GROUP("Display")
SETTING_INT(SETTING_BRIGHTNESS, "brightness", "Helligkeit (10-255)",    DISPLAY_BRIGHTNESS, 10, 255, 0)
SETTING_INT(SETTING_ROTATION,   "rotation",   "Display-Rotation (0-3)", DISPLAY_ROTATION, 0, 3, 0)

#undef SETTING_STR
#undef SETTING_INT
#undef SETTING_FLT
#undef SETTING_GROUP
