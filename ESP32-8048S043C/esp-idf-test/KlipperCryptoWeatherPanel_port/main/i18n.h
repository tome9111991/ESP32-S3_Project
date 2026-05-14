// Compile-time Lokalisierung. Sprache wird via Kconfig (CONFIG_APP_LANG_*)
// gewaehlt. Neue Strings hier in APP_STRINGS(X) eintragen (id, deutsch, english).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// id           deutsch                                   english
#define APP_STRINGS(X)                                                                            \
    X(SETTINGS_TITLE,                 "Einstellungen",                         "Settings")       \
    X(TILE_WIFI,                      "WLAN",                                  "Wi-Fi")          \
    X(TILE_SCREENS,                   "Screens",                               "Screens")        \
    X(TILE_CRYPTO,                    "Crypto",                                "Crypto")         \
    X(TILE_DISPLAY,                   "Display",                               "Display")        \
    X(TILE_TOUCH,                     "Touch",                                 "Touch")          \
    X(TILE_LOCATION,                  "Standort",                              "Location")       \
    X(COMMON_SAVE,                    "Speichern",                             "Save")           \
    X(COMMON_SAVE_FAILED,             "Speichern fehlgeschlagen",              "Save failed")    \
    X(COMMON_SAVED,                   "Gespeichert",                           "Saved")          \
    X(COMMON_SAVED_RESTART,           "Gespeichert, Neustart...",              "Saved, restarting...") \
    X(COMMON_SAVED_ON_BACK,           "Wird beim Zurueckgehen gespeichert",     "Saved when going back") \
    X(COMMON_SELECTION_SAVED_ON_BACK, "Auswahl wird beim Zurueckgehen gespeichert", "Selection saved when going back") \
    X(COMMON_CANCEL,                  "Abbrechen",                             "Cancel")         \
    X(COMMON_CLOSE,                   "Schliessen",                            "Close")          \
    X(COMMON_RESET,                   "Zuruecksetzen",                         "Reset")          \
    X(COMMON_ACTIVE,                  "Aktiv",                                 "On")             \
    X(COMMON_OFF,                     "Aus",                                   "Off")            \
    X(COMMON_WAIT,                    "Bitte warten",                          "Please wait")    \
    X(KEYBOARD_DONE,                  "Fertig",                                "Done")           \
    X(WIFI_ENTER_SSID,                "SSID eingeben",                         "Enter SSID")     \
    X(WIFI_ENTER_PASSWORD,            "Passwort eingeben",                     "Enter password") \
    X(WIFI_PASSWORD,                  "Passwort",                              "Password")       \
    X(WIFI_NAME_PLACEHOLDER,          "WLAN-Name",                             "Wi-Fi name")     \
    X(WIFI_PASSWORD_PLACEHOLDER,      "WLAN-Passwort",                         "Wi-Fi password") \
    X(WIFI_HINT_EDIT_FIELD,           "Tippe auf ein Feld, um es zu bearbeiten", "Tap a field to edit it") \
    X(WIFI_MISSING_SSID,              "SSID fehlt",                            "SSID missing")   \
    X(WIFI_NO_DATA_SAVED,             "Keine WLAN-Daten gespeichert",          "No Wi-Fi data saved") \
    X(WIFI_CONNECTING,                "WLAN verbindet",                        "Wi-Fi connecting") \
    X(WIFI_NOT_CONFIGURED,            "WLAN nicht konfiguriert",               "Wi-Fi not configured") \
    X(WIFI_MISSING,                   "WLAN fehlt",                            "Wi-Fi missing")  \
    X(WIFI_WAITING,                   "WLAN WARTET",                           "WI-FI WAITING")  \
    X(LOCATION_LATITUDE,              "Breitengrad",                           "Latitude")       \
    X(LOCATION_LONGITUDE,             "Laengengrad",                           "Longitude")      \
    X(LOCATION_ENTER_LAT,             "Breitengrad (-90 bis 90)",              "Latitude (-90 to 90)") \
    X(LOCATION_ENTER_LON,             "Laengengrad (-180 bis 180)",            "Longitude (-180 to 180)") \
    X(LOCATION_LAT_INVALID,           "Breitengrad ungueltig (-90 bis 90)",    "Invalid latitude (-90 to 90)") \
    X(LOCATION_LON_INVALID,           "Laengengrad ungueltig (-180 bis 180)",  "Invalid longitude (-180 to 180)") \
    X(LOCATION_HELP,                  "Dezimalgrad (z.B. maps.google.com -> Rechtsklick)", "Decimal degrees (e.g. maps.google.com -> right click)") \
    X(LOCATION_FALLBACK,              "Standort",                              "Location")       \
    X(CRYPTO_CURRENCY,                "Waehrung",                              "Currency")       \
    X(CRYPTO_CANDLES,                 "Kerzen",                                "Candles")        \
    X(CRYPTO_SELECT_ALL,              "Coin, Waehrung und Chart auswaehlen",   "Select coin, currency and chart") \
    X(DISPLAY_DAY_BRIGHTNESS,         "Helligkeit (Tag)",                      "Brightness (day)") \
    X(DISPLAY_NIGHT_MODE,             "Nacht-Modus",                           "Night mode")     \
    X(DISPLAY_DIM_AFTER_SUNSET,       "Nach Sonnenuntergang dimmen",           "Dim after sunset") \
    X(DISPLAY_DAY_BRIGHTNESS_ALWAYS,  "Tag-Helligkeit rund um die Uhr",        "Day brightness around the clock") \
    X(DISPLAY_RESTART_ON_BACK,        "Neustart beim Zurueckgehen",            "Restarts when going back") \
    X(DISPLAY_TOUCH_CAL_KEEP,         "Touch-Kalibrierung bleibt",             "Touch calibration stays") \
    X(DISPLAY_ROTATE_180,             "Display 180 Grad drehen",               "Rotate display 180 degrees") \
    X(SCREEN_CLOCK_SHORT,             "UHR",                                   "CLOCK")          \
    X(SCREEN_PRICE_SHORT,             "PREIS",                                 "PRICE")          \
    X(SCREEN_CHART_SHORT,             "CHART",                                 "CHART")          \
    X(SCREEN_KLIPPER_SHORT,           "KLIPPER",                               "KLIPPER")        \
    X(SCREEN_LIVE_PRICE,              "Live-Kurs",                             "Live price")     \
    X(SCREEN_CANDLE_CHART,            "Kerzenchart",                           "Candlestick chart") \
    X(SCREEN_PRINTER_STATUS,          "Druckerstatus",                         "Printer status") \
    X(SCREEN_TIME_WEATHER,            "Zeit und Wetter",                       "Time and weather") \
    X(SCREEN_OPTIONS,                 "Optionen",                              "Options")        \
    X(SCREEN_DISPLAY_DURATION,        "Anzeigedauer",                          "Display duration") \
    X(SCREEN_ONE_REQUIRED,            "Mindestens eine Seite muss aktiv bleiben", "At least one page must stay on") \
    X(SCREEN_KLIPPER_ONLY_ONLINE,     "Klipper erscheint nur, wenn der Drucker erreichbar ist", "Klipper only appears when the printer is reachable") \
    X(POPUP_FACTORY_DELETE_1,         "Alle Einstellungen werden geloescht",    "All settings will be deleted") \
    X(POPUP_FACTORY_DELETE_2,         "und das Geraet startet neu.",            "and the device will restart.") \
    X(POPUP_FIRMWARE,                 "Firmware",                              "Firmware")       \
    X(POPUP_FW_VERSION,               "Version",                               "Version")        \
    X(POPUP_FW_BOARD,                 "Board",                                 "Board")          \
    X(POPUP_FW_LANGUAGE,              "Sprache",                               "Language")       \
    X(TOUCH_TITLE,                    "Touch kalibrieren",                     "Calibrate touch") \
    X(TOUCH_POINT_FMT,                "Punkt %d / %d",                         "Point %d / %d")  \
    X(TOUCH_HINT,                     "Punkt antippen. Halten zum Abbrechen.",  "Tap the point. Hold to cancel.") \
    X(TOUCH_FAILED,                   "Kalibrierung fehlgeschlagen, bitte erneut versuchen", "Calibration failed, please try again") \
    X(TOUCH_SAVED,                    "Kalibrierung gespeichert",              "Calibration saved") \
    X(TOUCH_HOLDING,                  "Halten...",                             "Holding...")     \
    X(TOUCH_ABORTED,                  "Kalibrierung abgebrochen",              "Calibration cancelled") \
    X(TOUCH_TOO_SHORT,                "Zu kurz, bitte erneut tippen",          "Too short, tap again") \
    X(WEATHER_TITLE,                  "Wetter",                                "Weather")        \
    X(WEATHER_TEMP,                   "Temperatur",                            "Temperature")    \
    X(WEATHER_FEELS_LIKE,             "Gefuehlt",                              "Feels like")     \
    X(WEATHER_WIND,                   "Wind",                                  "Wind")           \
    X(WEATHER_HUMIDITY,               "Luftfeuchte",                           "Humidity")       \
    X(WEATHER_SUN,                    "Sonne",                                 "Sun")            \
    X(WEATHER_5_DAYS,                 "5 Tage",                                "5 days")         \
    X(WEATHER_DAILY_LOADING,          "Tagesvorhersage wird geladen...",        "Daily forecast loading...") \
    X(WEATHER_TODAY,                  "Heute",                                 "Today")          \
    X(WEATHER_SYNC_TIME,              "Zeit wird synchronisiert",              "Syncing time")   \
    X(WEATHER_CLEAR,                  "Klar",                                  "Clear")          \
    X(WEATHER_MAINLY_CLEAR,           "Heiter",                                "Mostly clear")   \
    X(WEATHER_PARTLY_CLOUDY,          "Teilw. bewoelkt",                       "Partly cloudy")  \
    X(WEATHER_OVERCAST,               "Bedeckt",                               "Overcast")       \
    X(WEATHER_FOG,                    "Nebel",                                 "Fog")            \
    X(WEATHER_DRIZZLE_LIGHT,          "Leichter Niesel",                       "Light drizzle")  \
    X(WEATHER_DRIZZLE,                "Niesel",                                "Drizzle")        \
    X(WEATHER_DRIZZLE_HEAVY,          "Starker Niesel",                        "Heavy drizzle")  \
    X(WEATHER_FREEZING_DRIZZLE,       "Gefr. Niesel",                          "Freezing drizzle") \
    X(WEATHER_RAIN_LIGHT,             "Leichter Regen",                        "Light rain")     \
    X(WEATHER_RAIN,                   "Regen",                                 "Rain")           \
    X(WEATHER_RAIN_HEAVY,             "Starker Regen",                         "Heavy rain")     \
    X(WEATHER_FREEZING_RAIN,          "Gefr. Regen",                           "Freezing rain")  \
    X(WEATHER_SNOW_LIGHT,             "Leichter Schnee",                       "Light snow")     \
    X(WEATHER_SNOW,                   "Schnee",                                "Snow")           \
    X(WEATHER_SNOW_HEAVY,             "Starker Schnee",                        "Heavy snow")     \
    X(WEATHER_SNOW_GRAINS,            "Schneegriesel",                         "Snow grains")    \
    X(WEATHER_SHOWERS_LIGHT,          "Leichte Schauer",                       "Light showers")  \
    X(WEATHER_SHOWERS,                "Schauer",                               "Showers")        \
    X(WEATHER_SHOWERS_HEAVY,          "Heftige Schauer",                       "Heavy showers")  \
    X(WEATHER_SNOW_SHOWERS,           "Schneeschauer",                         "Snow showers")   \
    X(WEATHER_THUNDER,                "Gewitter",                              "Thunderstorm")   \
    X(WEATHER_THUNDER_HAIL,           "Gewitter+Hagel",                        "Storm+hail")     \
    X(WEEKDAY_SUN_SHORT,              "So",                                    "Sun")            \
    X(WEEKDAY_MON_SHORT,              "Mo",                                    "Mon")            \
    X(WEEKDAY_TUE_SHORT,              "Di",                                    "Tue")            \
    X(WEEKDAY_WED_SHORT,              "Mi",                                    "Wed")            \
    X(WEEKDAY_THU_SHORT,              "Do",                                    "Thu")            \
    X(WEEKDAY_FRI_SHORT,              "Fr",                                    "Fri")            \
    X(WEEKDAY_SAT_SHORT,              "Sa",                                    "Sat")            \
    X(WEEKDAY_SUN,                    "Sonntag",                               "Sunday")         \
    X(WEEKDAY_MON,                    "Montag",                                "Monday")         \
    X(WEEKDAY_TUE,                    "Dienstag",                              "Tuesday")        \
    X(WEEKDAY_WED,                    "Mittwoch",                              "Wednesday")      \
    X(WEEKDAY_THU,                    "Donnerstag",                            "Thursday")       \
    X(WEEKDAY_FRI,                    "Freitag",                               "Friday")         \
    X(WEEKDAY_SAT,                    "Samstag",                               "Saturday")       \
    X(DIR_N,                          "N",                                     "N")              \
    X(DIR_NE,                         "NO",                                    "NE")             \
    X(DIR_E,                          "O",                                     "E")              \
    X(DIR_SE,                         "SO",                                    "SE")             \
    X(DIR_S,                          "S",                                     "S")              \
    X(DIR_SW,                         "SW",                                    "SW")             \
    X(DIR_W,                          "W",                                     "W")              \
    X(DIR_NW,                         "NW",                                    "NW")             \
    X(LOADING,                        "Laden...",                              "Loading...")     \
    X(STATUS_ERROR_SHORT,             "FEHLER",                                "ERROR")          \
    X(STATUS_HTTP_ERROR,              "HTTP FEHLER",                           "HTTP ERROR")     \
    X(STATUS_JSON_ERROR,              "JSON FEHLER",                           "JSON ERROR")     \
    X(STATUS_PRICE,                   "PREIS",                                 "PRICE")          \
    X(WEATHER_STATUS_OK,              "WETTER: open-meteo",                    "WEATHER: open-meteo") \
    X(WEATHER_STATUS_INIT,            "WETTER: --",                            "WEATHER: --")    \
    X(KLIPPER_NO_JOB,                 "Kein Job",                              "No job")         \
    X(KLIPPER_BED,                    "Bett",                                  "Bed")            \
    X(KLIPPER_MOONRAKER_UNREACHABLE,  "Moonraker nicht erreichbar",            "Moonraker unreachable") \
    X(KLIPPER_POWER_ON_PRINTER,       "Drucker einschalten",                   "Power on printer") \
    X(KLIPPER_WAITING_MAINSAIL,       "Warte auf Mainsail",                    "Waiting for Mainsail") \
    X(KLIPPER_MMU_INACTIVE,           "MMU nicht aktiv",                       "MMU inactive")   \
    X(KLIPPER_PRINTER_OFF_OR_MCU,     "Drucker aus oder MCU nicht verbunden",  "Printer off or MCU not connected") \
    X(KLIPPER_STARTING,               "Klipper startet",                       "Klipper starting") \
    X(KLIPPER_ERROR,                  "Klipper Fehler",                        "Klipper error")  \
    X(KLIPPER_PRINT_DATA_MISSING,     "Druckdaten nicht verfuegbar",           "Print data unavailable") \
    X(KLIPPER_KLIPPY_NOT_READY,       "Klippy nicht bereit",                   "Klippy not ready") \
    X(PRINT_STATE_PRINTING,           "DRUCKT",                                "PRINTING")       \
    X(PRINT_STATE_PAUSED,             "PAUSE",                                 "PAUSED")         \
    X(PRINT_STATE_ERROR,              "FEHLER",                                "ERROR")          \
    X(PRINT_STATE_COMPLETE,           "FERTIG",                                "DONE")           \
    X(PRINT_STATE_CANCELLED,          "ABBRUCH",                               "CANCELLED")      \
    X(PRINT_STATE_READY,              "BEREIT",                                "READY")          \
    X(PRINT_STATE_OFF,                "AUS",                                   "OFF")

typedef enum {
#define X(id, de, en) STR_##id,
    APP_STRINGS(X)
#undef X
    STR_COUNT
} app_str_id_t;

extern const char *const app_strings[STR_COUNT];

#define T(id) (app_strings[STR_##id])

#ifdef __cplusplus
}
#endif
