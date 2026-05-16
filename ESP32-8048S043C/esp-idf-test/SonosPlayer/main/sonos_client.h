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
    uint32_t last_update_ms;
} sonos_player_t;

typedef enum {
    SONOS_CMD_PLAY,
    SONOS_CMD_PAUSE,
    SONOS_CMD_NEXT,
    SONOS_CMD_PREVIOUS,
    SONOS_CMD_VOLUME,
    SONOS_CMD_TOGGLE_MUTE,
    SONOS_CMD_SEEK,
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
