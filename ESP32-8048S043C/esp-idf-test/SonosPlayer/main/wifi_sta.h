#pragma once

#include <stdbool.h>

// Startet WLAN im STA-Modus mit main/config_private.h.
bool wifi_sta_start(void);
bool wifi_sta_is_connected(void);
