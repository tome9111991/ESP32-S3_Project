#pragma once

// Diese Datei darf ins Git-Repository. Kopiere sie lokal nach
// config_private.h und trage dort deine echten privaten Werte ein.

#define WIFI_SSID "DEIN_WLAN_NAME"
#define WIFI_PASSWORD "DEIN_WLAN_PASSWORT"

// Standort fuer Wetterdaten und Sonnenstand.
#define LOCATION_LATITUDE 52.000000f
#define LOCATION_LONGITUDE 13.000000f
// POSIX-Zeitzone fuer die lokale Uhrzeit. Deutschland: CET/CEST.
#define TIMEZONE_POSIX "CET-1CEST,M3.5.0,M10.5.0/3"

// Optionale Anzeige-/API-Einstellungen.
#define CRYPTO_BASE_SYMBOL "BTC"
#define CRYPTO_QUOTE_SYMBOL "USD"
#define CRYPTO_PRICE_PREFIX ""
#define CRYPTO_SERVICE_NAME "COINBASE"
// Chart-Kerzen: "15M", "1H", "6H" oder "1D".
#define CRYPTO_CHART_TIMEFRAME "1D"
#define KLIPPER_BASE_URL "http://mainsail"

// Display-Rotation. 2 = Standardausrichtung (wie bisher), 0 = um 180 Grad gedreht.
// Erlaubte Werte: 0, 1, 2, 3 (LovyanGFX setRotation).
#define DISPLAY_ROTATION 2

// Welche Screens sollen angezeigt werden? 1 = an, 0 = aus.
// Mindestens ein Screen muss aktiviert sein. Deaktivierte Screens werden
// gar nicht erzeugt und ihre API-Abfragen entfallen (entlastet das Board).
#define SCREEN_TIME_ENABLED 1
#define SCREEN_CRYPTO_PRICE_ENABLED 1
#define SCREEN_CRYPTO_CHART_ENABLED 1
#define SCREEN_KLIPPER_ENABLED 1
