#ifndef CONFIG_PRIVATE_EXAMPLE_H
#define CONFIG_PRIVATE_EXAMPLE_H

// Diese Datei als config_private.h kopieren und echte WLAN-Daten eintragen.
#define WIFI_SSID "DEIN_WLAN_NAME"
#define WIFI_PASSWORD "DEIN_WLAN_PASSWORT"

// Koordinaten fuer die automatische Suche nach der naechsten DWD-Station.
#define DWD_LATITUDE 51.3397f
#define DWD_LONGITUDE 12.3731f
#define DWD_LOCATION_NAME "Leipzig"

// Fallback, falls die Stationsliste nicht geladen werden kann.
#define DWD_FALLBACK_STATION_ID "10471"

#endif
