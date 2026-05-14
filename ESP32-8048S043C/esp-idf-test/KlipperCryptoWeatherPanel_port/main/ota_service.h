// OTA-Update gegen GitHub-Releases.
//
// Workflow:
//   1. ota_service_init() einmal beim Boot (in main.c, nach nvs/wifi-Setup).
//      Markiert den aktuell laufenden Slot als "valid", falls noch pending.
//   2. ota_service_start_check() startet einen Background-Task, der die
//      GitHub-Releases-API abfragt, das hoechste Release mit Tag-Prefix
//      "APP_FW_NAME-" sucht und ein Asset findet, das APP_FW_BOARD und
//      APP_FW_LANG im Namen enthaelt.
//   3. UI pollt ota_service_get_status() (mutex-sicher) und stellt
//      OTA_STATE_UPDATE_AVAILABLE -> Install-Button frei.
//   4. ota_service_start_install() startet esp_https_ota und reboot bei
//      Erfolg. Bei Boot-Fehler greift Rollback (siehe sdkconfig.defaults).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_STATE_IDLE              = 0,
    OTA_STATE_CHECKING          = 1,
    OTA_STATE_NO_UPDATE         = 2,
    OTA_STATE_UPDATE_AVAILABLE  = 3,
    OTA_STATE_DOWNLOADING       = 4,
    OTA_STATE_SUCCESS           = 5,  // Reboot folgt, UI sollte das anzeigen
    OTA_STATE_ERROR             = 6,
    OTA_STATE_NOT_CONFIGURED    = 7,  // APP_OTA_REPO_OWNER/_NAME leer
} ota_state_t;

typedef struct {
    ota_state_t state;
    char        message[96];           // Status-Text, frei lesbar
    char        available_version[24]; // gesetzt bei UPDATE_AVAILABLE
    int         progress_percent;      // 0..100 bei DOWNLOADING
} ota_status_t;

void ota_service_init(void);
bool ota_service_start_check(void);    // false = bereits laufend
bool ota_service_start_install(void);  // false = kein Update bereit / busy
void ota_service_get_status(ota_status_t *out);
bool ota_service_is_installing(void);

#ifdef __cplusplus
}
#endif
