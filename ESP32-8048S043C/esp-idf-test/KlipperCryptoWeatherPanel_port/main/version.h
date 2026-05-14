// Firmware-Versions-Infos fuer den "Firmware"-Eintrag im Long-Press-Popup
// und als Vorbereitung fuer spaeteres OTA. APP_FW_VERSION laesst sich vom
// GitHub-Workflow ueberschreiben (idf.py build -DAPP_FW_VERSION="0.1.0"),
// sonst gilt der Fallback hier.
#pragma once

#include "sdkconfig.h"

#ifndef APP_FW_VERSION
#define APP_FW_VERSION "14052026"
#endif

// UI-Sprache wird zur Compile-Zeit gewaehlt (Kconfig: CONFIG_APP_LANG_*).
#if defined(CONFIG_APP_LANG_EN)
#  define APP_FW_LANG "EN"
#elif defined(CONFIG_APP_LANG_DE)
#  define APP_FW_LANG "DE"
#else
#  define APP_FW_LANG "??"
#endif

// Hardware-Board (nicht der Chip-Target aus CONFIG_IDF_TARGET). Lasst sich
// per CMake ueberschreiben, z. B. -DAPP_FW_BOARD="ESP32-8048S043C".
#ifndef APP_FW_BOARD
#define APP_FW_BOARD "ESP32-8048S043C"
#endif
