// Firmware-Versions-Infos fuer den "Firmware"-Eintrag im Long-Press-Popup
// und das OTA-Update aus dem GitHub-Release. APP_FW_VERSION laesst sich vom
// Workflow ueberschreiben (idf.py build -DAPP_FW_VERSION="20260514"),
// sonst gilt der Fallback hier.
//
// Format MUSS lexikographisch sortierbar sein, damit der OTA-Client
// new > current vergleichen kann. YYYYMMDD funktioniert mit strcmp.
#pragma once

#include "sdkconfig.h"

#ifndef APP_FW_NAME
#define APP_FW_NAME "KCWPv2"
#endif

#ifndef APP_FW_VERSION
#define APP_FW_VERSION "20260514"
#endif

// GitHub-Repo, in dem die Releases liegen. Per CMake ueberschreibbar:
// -DAPP_OTA_REPO_OWNER="user" -DAPP_OTA_REPO_NAME="repo"
// Der Release-Workflow setzt das ohnehin aus den GitHub-Env-Vars; hier sind
// nur die Fallbacks fuer lokale Builds.
#ifndef APP_OTA_REPO_OWNER
#define APP_OTA_REPO_OWNER "tome9111991"
#endif
#ifndef APP_OTA_REPO_NAME
#define APP_OTA_REPO_NAME "ESP32-S3_Project"
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
