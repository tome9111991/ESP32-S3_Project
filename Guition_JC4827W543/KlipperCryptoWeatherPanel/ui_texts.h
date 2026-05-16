#pragma once

#include <Arduino.h>

// Zentrale UI-Texte. Auswahl erfolgt in config_private.h per UI_LANGUAGE.
#ifndef UI_LANGUAGE_DE
  #define UI_LANGUAGE_DE 0
#endif
#ifndef UI_LANGUAGE_EN
  #define UI_LANGUAGE_EN 1
#endif
#ifndef UI_LANGUAGE
  #define UI_LANGUAGE UI_LANGUAGE_DE
#endif

#if (UI_LANGUAGE != UI_LANGUAGE_DE) && (UI_LANGUAGE != UI_LANGUAGE_EN)
  #error "UI_LANGUAGE muss UI_LANGUAGE_DE oder UI_LANGUAGE_EN sein."
#endif

#if UI_LANGUAGE == UI_LANGUAGE_EN
static const char* const UI_WEEKDAYS[7] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char* const UI_REVERSE_GEOCODE_LANGUAGE = "en";
static const char* const UI_TEXT_LOADING = "Loading...";
static const char* const UI_TEXT_BOOTING = "Booting";
static const char* const UI_TEXT_LOCATION_FALLBACK = "Location";
static const char* const UI_TEXT_WEATHER_INITIAL = "WEATHER: --";
static const char* const UI_TEXT_WEATHER_OK = "WEATHER: OM";
static const char* const UI_TEXT_WEATHER_NO_TEMP = "OM: NO TEMP";
static const char* const UI_TEXT_WIFI_CONNECTING = "WiFi connecting";
static const char* const UI_TEXT_WIFI_WAIT = "WIFI WAIT";
static const char* const UI_TEXT_TIME_SYNCING = "Syncing time";
static const char* const UI_TEXT_STATUS = "Status";
static const char* const UI_TEXT_KLIPPER = "KLIPPER";
static const char* const UI_TEXT_NOZZLE = "Nozzle";
static const char* const UI_TEXT_BED = "Bed";
static const char* const UI_TEXT_OFFLINE = "OFFLINE";
static const char* const UI_TEXT_MOONRAKER_UNREACHABLE = "Moonraker unreachable";
static const char* const UI_TEXT_MAINSAIL_WAIT = "Waiting for Mainsail";
static const char* const UI_TEXT_TURN_PRINTER_ON = "Turn printer on";
static const char* const UI_TEXT_MMU_INACTIVE = "MMU inactive";
static const char* const UI_TEXT_NO_JOB = "No job";
static const char* const UI_TEXT_IDLE = "Idle";
static const char* const UI_TEXT_DURATION = "DUR";
static const char* const UI_TEXT_ERROR = "ERROR";
static const char* const UI_TEXT_PRINTING = "PRINTING";
static const char* const UI_TEXT_PAUSE = "PAUSE";
static const char* const UI_TEXT_COMPLETE = "DONE";
static const char* const UI_TEXT_CANCELLED = "CANCEL";
static const char* const UI_TEXT_READY = "READY";
static const char* const UI_TEXT_START = "START";
static const char* const UI_TEXT_OFF = "OFF";
static const char* const UI_TEXT_PRINTER_OFF_DETAIL = "Printer off or MCU disconnected";
static const char* const UI_TEXT_KLIPPER_STARTING = "Klipper starting";
static const char* const UI_TEXT_KLIPPER_ERROR = "Klipper error";
static const char* const UI_TEXT_PRINT_DATA_UNAVAILABLE = "Print data unavailable";
static const char* const UI_TEXT_KLIPPY_NOT_READY = "Klippy not ready";
static const char* const UI_TEXT_FIRMWARE_RESTART_HINT = "Run FIRMWARE_RESTART";
static const char* const UI_TEXT_CANDLE_WAIT_TIME = "CANDLE WAIT TIME";
#else
static const char* const UI_WEEKDAYS[7] = {
  "Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"
};
static const char* const UI_REVERSE_GEOCODE_LANGUAGE = "de";
static const char* const UI_TEXT_LOADING = "Laden...";
static const char* const UI_TEXT_BOOTING = "Booting";
static const char* const UI_TEXT_LOCATION_FALLBACK = "Standort";
static const char* const UI_TEXT_WEATHER_INITIAL = "WETTER: --";
static const char* const UI_TEXT_WEATHER_OK = "WETTER: OM";
static const char* const UI_TEXT_WEATHER_NO_TEMP = "OM: KEIN TEMP";
static const char* const UI_TEXT_WIFI_CONNECTING = "WLAN verbindet";
static const char* const UI_TEXT_WIFI_WAIT = "WLAN WARTET";
static const char* const UI_TEXT_TIME_SYNCING = "Zeit wird synchronisiert";
static const char* const UI_TEXT_STATUS = "Status";
static const char* const UI_TEXT_KLIPPER = "KLIPPER";
static const char* const UI_TEXT_NOZZLE = "Nozzle";
static const char* const UI_TEXT_BED = "Bett";
static const char* const UI_TEXT_OFFLINE = "OFFLINE";
static const char* const UI_TEXT_MOONRAKER_UNREACHABLE = "Moonraker nicht erreichbar";
static const char* const UI_TEXT_MAINSAIL_WAIT = "Warte auf Mainsail";
static const char* const UI_TEXT_TURN_PRINTER_ON = "Drucker einschalten";
static const char* const UI_TEXT_MMU_INACTIVE = "MMU nicht aktiv";
static const char* const UI_TEXT_NO_JOB = "Kein Job";
static const char* const UI_TEXT_IDLE = "Idle";
static const char* const UI_TEXT_DURATION = "DAUER";
static const char* const UI_TEXT_ERROR = "FEHLER";
static const char* const UI_TEXT_PRINTING = "DRUCKT";
static const char* const UI_TEXT_PAUSE = "PAUSE";
static const char* const UI_TEXT_COMPLETE = "FERTIG";
static const char* const UI_TEXT_CANCELLED = "ABBRUCH";
static const char* const UI_TEXT_READY = "BEREIT";
static const char* const UI_TEXT_START = "START";
static const char* const UI_TEXT_OFF = "AUS";
static const char* const UI_TEXT_PRINTER_OFF_DETAIL = "Drucker aus oder MCU nicht verbunden";
static const char* const UI_TEXT_KLIPPER_STARTING = "Klipper startet";
static const char* const UI_TEXT_KLIPPER_ERROR = "Klipper Fehler";
static const char* const UI_TEXT_PRINT_DATA_UNAVAILABLE = "Druckdaten nicht verfuegbar";
static const char* const UI_TEXT_KLIPPY_NOT_READY = "Klippy nicht bereit";
static const char* const UI_TEXT_FIRMWARE_RESTART_HINT = "FIRMWARE_RESTART ausfuehren";
static const char* const UI_TEXT_CANDLE_WAIT_TIME = "CANDLE WARTET ZEIT";
#endif

static inline String uiDurationText() {
  return String(UI_TEXT_DURATION) + " --";
}

static inline String uiDurationText(unsigned long minutes) {
  return String(UI_TEXT_DURATION) + " " + String(minutes) + "M";
}

static inline String uiDurationText(unsigned long hours, unsigned long minutes) {
  return String(UI_TEXT_DURATION) + " " + String(hours) + "H " + String(minutes) + "M";
}
