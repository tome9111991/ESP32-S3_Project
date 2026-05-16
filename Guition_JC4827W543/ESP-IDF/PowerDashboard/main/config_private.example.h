#pragma once

// Diese Datei darf ins Git-Repository. Kopiere sie lokal nach
// config_private.h und trage dort deine echten privaten Werte ein.
//
// Bleibt WIFI_SSID leer (""), startet das Geraet beim Boot im
// Provisioning-Modus mit QR-Code-Setup auf dem Display.

#define WIFI_SSID "DEIN_WLAN_NAME"
#define WIFI_PASSWORD "DEIN_WLAN_PASSWORT"

// Shelly Plug (Gen2/Plus, RPC ueber HTTP).
#define SHELLY_IP "192.168.178.24"
#define SHELLY_POLL_INTERVAL_MS 2000

// Arbeitspreis in ct/kWh - laut Anbieter.
#define TARIFF_CT_PER_KWH 30.86f

// POSIX-Zeitzone fuer die lokale Uhrzeit. Deutschland: CET/CEST.
#define TIMEZONE_POSIX "CET-1CEST,M3.5.0,M10.5.0/3"
#define SNTP_SERVER "pool.ntp.org"

// Display-Rotation. Erlaubt: 0, 1, 2, 3 (LovyanGFX setRotation).
#define DISPLAY_ROTATION 2
// Hintergrundbeleuchtung 10..255.
#define DISPLAY_BRIGHTNESS 160
