#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool online;
    bool playing;
    char host[24];
    char room[32];
    char title[112];
    char artist[96];
    char album[96];
    char source[64];
    char status[48];
    int position_sec;
    int duration_sec;
    int volume;
    bool muted;
    bool shuffle;
    bool repeat;
    bool repeat_one;
    uint32_t last_update_ms;
    char cover_url[320];
} sonos_player_t;

typedef struct {
    uint16_t *pixels;        // RGB565 LE, w*h Worte, in PSRAM
    int w;
    int h;
} sonos_cover_image_t;

// Wird mit dem dekodierten Cover oder NULL (Reset) aufgerufen. Aufrufer uebernimmt
// das Bild komplett (Pixel werden via free() freigegeben). Callback laeuft NICHT
// im LVGL-Kontext, der Empfaenger muss den Tausch selbst schuetzen.
typedef void (*sonos_cover_cb_t)(const sonos_cover_image_t *image, void *user);
void sonos_cover_set_callback(sonos_cover_cb_t cb, void *user);

typedef enum {
    SONOS_CMD_PLAY,
    SONOS_CMD_PAUSE,
    SONOS_CMD_NEXT,
    SONOS_CMD_PREVIOUS,
    SONOS_CMD_VOLUME,
    SONOS_CMD_TOGGLE_MUTE,
    SONOS_CMD_SEEK,
    SONOS_CMD_TOGGLE_SHUFFLE,
    SONOS_CMD_TOGGLE_REPEAT,
    SONOS_CMD_SELECT,
    SONOS_CMD_RESCAN,
} sonos_cmd_type_t;

void sonos_service_init(void);
void sonos_service_start(void);
void sonos_queue_cmd(sonos_cmd_type_t type, int value);

sonos_player_t sonos_snapshot_active(void);
int sonos_active_index(void);
int sonos_host_count(void);
const char *sonos_host_at(int index);
