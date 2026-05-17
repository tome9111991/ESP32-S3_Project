// Sonos UPnP/SOAP Client: pollt Playerdaten und fuehrt UI-Kommandos aus.

#include "sonos_client.h"
#include "wifi_sta.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "rom/tjpgd.h"

#if __has_include("config_private.h")
  #include "config_private.h"
#endif

#ifndef SONOS_PLAYER_IPS
  #define SONOS_PLAYER_IPS "192.168.178.28"
#endif

#define HTTP_TIMEOUT_MS 2500
#define SONOS_PORT      1400
#define SONOS_POLL_MS   2500
#define SONOS_CMD_QUEUE 8
#define SONOS_SEEK_HOLD_MS 3000
#define HOST_COUNT      ((int)(sizeof(s_sonos_hosts) / sizeof(s_sonos_hosts[0])))

#define COVER_DEBOUNCE_MS   1500
#define COVER_MAX_BYTES     (300 * 1024)   // hartes Limit, falls Sonos mal was Riesiges liefert
#define COVER_TJPGD_POOL    3500           // Arbeitsspeicher fuer ROM TJpgDec
#define COVER_DECODE_MAX_DIM 400           // groesste Kantenlaenge nach jd_decomp-Skalierung

static const char *TAG = "sonos";
static const char *s_sonos_hosts[] = { SONOS_PLAYER_IPS };
static sonos_player_t s_players[HOST_COUNT];
static int s_active_player;
static SemaphoreHandle_t s_state_mutex;
static QueueHandle_t s_cmd_queue;
static bool s_task_started;
static uint32_t s_seek_hold_until_ms[HOST_COUNT];
static bool s_seek_hold_playing[HOST_COUNT];

// Cover-Pipeline-State - alle Zugriffe nur aus dem sonos_task heraus,
// ausser dem Callback-Setter, der den Mutex nimmt.
static char s_cover_active_url[sizeof(((sonos_player_t *)0)->cover_url)];
static char s_cover_pending_url[sizeof(((sonos_player_t *)0)->cover_url)];
static uint32_t s_cover_pending_since_ms;
static sonos_cover_cb_t s_cover_cb;
static void *s_cover_cb_user;

typedef struct {
    sonos_cmd_type_t type;
    int value;
} sonos_cmd_t;

typedef struct {
    char *data;
    int len;
    int cap;
} http_buf_t;

static bool xml_extract_tag(const char *xml, const char *tag, char *out, size_t out_size);
static void clean_text(char *s);

static void str_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static const char *host_short_name(const char *host)
{
    const char *dot = strrchr(host, '.');
    return dot && dot[1] ? dot + 1 : host;
}

static void format_room_name(const char *host, char *out, size_t out_size)
{
    snprintf(out, out_size, "Sonos %s", host_short_name(host));
}

int sonos_host_count(void)
{
    return HOST_COUNT;
}

const char *sonos_host_at(int index)
{
    if (index < 0 || index >= HOST_COUNT) return "";
    return s_sonos_hosts[index];
}

int sonos_active_index(void)
{
    int idx = 0;
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    idx = s_active_player;
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
    return idx;
}

sonos_player_t sonos_snapshot_active(void)
{
    sonos_player_t snap = {0};
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    int idx = s_active_player;
    if (idx < 0 || idx >= HOST_COUNT) idx = 0;
    snap = s_players[idx];
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
    return snap;
}

void sonos_service_init(void)
{
    if (!s_state_mutex) s_state_mutex = xSemaphoreCreateMutex();
    if (!s_cmd_queue) s_cmd_queue = xQueueCreate(SONOS_CMD_QUEUE, sizeof(sonos_cmd_t));

    for (int i = 0; i < HOST_COUNT; i++) {
        str_copy(s_players[i].host, sizeof(s_players[i].host), s_sonos_hosts[i]);
        format_room_name(s_sonos_hosts[i], s_players[i].room, sizeof(s_players[i].room));
        str_copy(s_players[i].title, sizeof(s_players[i].title), "Warte auf Sonos");
        str_copy(s_players[i].artist, sizeof(s_players[i].artist), "Verbinde mit WLAN und suche Player");
        str_copy(s_players[i].source, sizeof(s_players[i].source), "Sonos");
        str_copy(s_players[i].status, sizeof(s_players[i].status), "Offline");
    }
}

static void mark_wifi_waiting(void)
{
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    for (int i = 0; i < HOST_COUNT; i++) {
        s_players[i].online = false;
        s_players[i].playing = false;
        str_copy(s_players[i].status, sizeof(s_players[i].status), "WLAN wartet");
    }
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buf_t *buf = (http_buf_t *)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || !buf || !evt->data || evt->data_len <= 0) {
        return ESP_OK;
    }

    int needed = buf->len + evt->data_len + 1;
    if (needed > buf->cap) {
        int new_cap = buf->cap ? buf->cap * 2 : 4096;
        while (new_cap < needed) new_cap *= 2;
        char *next = realloc(buf->data, new_cap);
        if (!next) return ESP_ERR_NO_MEM;
        buf->data = next;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->len, evt->data, evt->data_len);
    buf->len += evt->data_len;
    buf->data[buf->len] = '\0';
    return ESP_OK;
}

static bool http_get_text(const char *url, char **response_out)
{
    if (response_out) *response_out = NULL;

    http_buf_t buf = {0};
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .user_data = &buf,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "GET failed status=%d err=%s url=%s",
                 status, esp_err_to_name(err), url);
        free(buf.data);
        return false;
    }

    if (!buf.data) buf.data = calloc(1, 1);
    if (response_out) {
        *response_out = buf.data;
    } else {
        free(buf.data);
    }
    return response_out ? *response_out != NULL : true;
}

static bool soap_post(const char *host, const char *service, const char *action,
                      const char *inner, char **response_out)
{
    if (response_out) *response_out = NULL;

    char url[160];
    snprintf(url, sizeof(url), "http://%s:%d/MediaRenderer/%s/Control",
             host, SONOS_PORT, service);

    char soap_action[128];
    snprintf(soap_action, sizeof(soap_action),
             "\"urn:schemas-upnp-org:service:%s:1#%s\"", service, action);

    const char *prefix =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>";
    const char *suffix = "</s:Body></s:Envelope>";
    char body[640];
    int body_len = snprintf(body, sizeof(body),
                            "%s<u:%s xmlns:u=\"urn:schemas-upnp-org:service:%s:1\">%s</u:%s>%s",
                            prefix, action, service, inner ? inner : "", action, suffix);
    if (body_len <= 0 || body_len >= (int)sizeof(body)) return false;

    http_buf_t buf = {0};
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .user_data = &buf,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return false;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "text/xml; charset=\"utf-8\"");
    esp_http_client_set_header(client, "SOAPACTION", soap_action);
    esp_http_client_set_post_field(client, body, body_len);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "SOAP %s/%s %s failed status=%d err=%s",
                 host, service, action, status, esp_err_to_name(err));
        free(buf.data);
        return false;
    }

    if (response_out) {
        if (!buf.data) buf.data = calloc(1, 1);
        *response_out = buf.data;
    } else {
        free(buf.data);
    }
    return true;
}

static bool sonos_get_device_room(const char *host, char *room, size_t room_size)
{
    char url[96];
    snprintf(url, sizeof(url), "http://%s:%d/xml/device_description.xml", host, SONOS_PORT);

    char *xml = NULL;
    if (!http_get_text(url, &xml)) return false;

    char room_name[sizeof(((sonos_player_t *)0)->room)] = {0};
    bool ok = xml_extract_tag(xml, "roomName", room_name, sizeof(room_name));
    free(xml);

    clean_text(room_name);
    if (!ok || !room_name[0]) return false;
    str_copy(room, room_size, room_name);
    return true;
}

static bool xml_extract_tag(const char *xml, const char *tag, char *out, size_t out_size)
{
    if (!xml || !tag || !out || out_size == 0) return false;
    out[0] = '\0';

    char open[64];
    char close[64];
    snprintf(open, sizeof(open), "<%s", tag);
    snprintf(close, sizeof(close), "</%s>", tag);

    const char *p = strstr(xml, open);
    if (!p) return false;
    p = strchr(p, '>');
    if (!p) return false;
    p++;

    const char *q = strstr(p, close);
    if (!q) return false;

    size_t n = (size_t)(q - p);
    if (n >= out_size) n = out_size - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

static bool contains_ci(const char *s, const char *needle)
{
    if (!s || !needle || !needle[0]) return false;
    size_t needle_len = strlen(needle);
    for (const char *p = s; *p; p++) {
        size_t i = 0;
        while (i < needle_len && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == needle_len) return true;
    }
    return false;
}

static int sonos_sid_from_uri(const char *uri)
{
    if (!uri) return -1;

    const char *p = strstr(uri, "sid=");
    if (!p) p = strstr(uri, "sid%3d");
    if (!p) return -1;

    p += (p[3] == '=') ? 4 : 6;
    if (!isdigit((unsigned char)*p)) return -1;
    return atoi(p);
}

static bool source_from_sonos_sid(int sid, char *out, size_t out_size)
{
    // Sonos SMAPI service IDs, soweit fuer die lokale Quellenanzeige relevant.
    switch (sid) {
    case 2:
        str_copy(out, out_size, "Deezer");
        return true;
    case 9:
        str_copy(out, out_size, "Spotify");
        return true;
    case 160:
        str_copy(out, out_size, "SoundCloud");
        return true;
    case 174:
        str_copy(out, out_size, "TIDAL");
        return true;
    case 201:
        str_copy(out, out_size, "Amazon Music");
        return true;
    case 204:
        str_copy(out, out_size, "Apple Music");
        return true;
    case 254:
    case 333:
        str_copy(out, out_size, "TuneIn");
        return true;
    case 284:
        str_copy(out, out_size, "YouTube Music");
        return true;
    default:
        return false;
    }
}

static void xml_unescape(char *s)
{
    if (!s) return;
    char *r = s;
    char *w = s;
    while (*r) {
        if (strncmp(r, "&lt;", 4) == 0) {
            *w++ = '<';
            r += 4;
        } else if (strncmp(r, "&gt;", 4) == 0) {
            *w++ = '>';
            r += 4;
        } else if (strncmp(r, "&amp;", 5) == 0) {
            *w++ = '&';
            r += 5;
        } else if (strncmp(r, "&quot;", 6) == 0) {
            *w++ = '"';
            r += 6;
        } else if (strncmp(r, "&apos;", 6) == 0) {
            *w++ = '\'';
            r += 6;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

static void clean_text(char *s)
{
    if (!s) return;
    xml_unescape(s);
    for (char *p = s; *p; p++) {
        if ((unsigned char)*p < 32 && *p != '\n') *p = ' ';
    }
    while (*s && isspace((unsigned char)*s)) memmove(s, s + 1, strlen(s));
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

static void sanitize_music_text(char *s)
{
    if (!s) return;
    clean_text(s);

    // Sonos liefert Coverbilder als /getaa?...; solche URIs gehoeren nie in Titel/Artist.
    char *art = strstr(s, "/getaa?");
    if (art) {
        while (art > s && isspace((unsigned char)art[-1])) art--;
        *art = '\0';
    }

    if (strncmp(s, "/getaa", 6) == 0 ||
        contains_ci(s, "albumArtURI") ||
        contains_ci(s, "x-sonos-http:") ||
        contains_ci(s, "x-sonos-spotify:") ||
        contains_ci(s, "http://") ||
        contains_ci(s, "https://")) {
        s[0] = '\0';
    }

    clean_text(s);
}

static bool detect_source_text(const char *uri, const char *metadata, char *out, size_t out_size)
{
    if (!out || out_size == 0) return false;

    int sid = sonos_sid_from_uri(uri);
    if (sid < 0) sid = sonos_sid_from_uri(metadata);

    if (source_from_sonos_sid(sid, out, out_size)) {
        return true;
    } else if (contains_ci(uri, "spotify") || contains_ci(metadata, "spotify")) {
        str_copy(out, out_size, "Spotify");
    } else if (contains_ci(uri, "soundcloud") || contains_ci(metadata, "soundcloud")) {
        str_copy(out, out_size, "SoundCloud");
    } else if (contains_ci(uri, "apple") || contains_ci(metadata, "apple music")) {
        str_copy(out, out_size, "Apple Music");
    } else if (contains_ci(uri, "amazon") || contains_ci(metadata, "amazon music")) {
        str_copy(out, out_size, "Amazon Music");
    } else if (contains_ci(uri, "deezer") || contains_ci(metadata, "deezer")) {
        str_copy(out, out_size, "Deezer");
    } else if (contains_ci(uri, "tidal") || contains_ci(metadata, "tidal")) {
        str_copy(out, out_size, "TIDAL");
    } else if (contains_ci(uri, "audible") || contains_ci(metadata, "audible")) {
        str_copy(out, out_size, "Audible");
    } else if (contains_ci(uri, "tunein") || contains_ci(metadata, "tunein") ||
               contains_ci(uri, "radio")) {
        str_copy(out, out_size, "Radio");
    } else if (contains_ci(uri, "airplay") || contains_ci(metadata, "airplay")) {
        str_copy(out, out_size, "AirPlay");
    } else if (contains_ci(uri, "line-in") || contains_ci(uri, "linein")) {
        str_copy(out, out_size, "Line-In");
    } else if (contains_ci(uri, "spdif") || contains_ci(uri, "tv")) {
        str_copy(out, out_size, "TV");
    } else if (contains_ci(uri, "x-file-cifs") || contains_ci(uri, "library")) {
        str_copy(out, out_size, "Musikbibliothek");
    } else if (contains_ci(uri, "x-rincon-queue")) {
        str_copy(out, out_size, "Sonos Queue");
    } else if (contains_ci(uri, "http://") || contains_ci(uri, "https://")) {
        str_copy(out, out_size, "Stream");
    } else {
        return false;
    }

    return true;
}

static int parse_sonos_time(const char *s)
{
    if (!s || !s[0] || strcmp(s, "NOT_IMPLEMENTED") == 0) return 0;
    int h = 0, m = 0, sec = 0;
    if (sscanf(s, "%d:%d:%d", &h, &m, &sec) != 3) return 0;
    return h * 3600 + m * 60 + sec;
}

static bool sonos_get_transport(const char *host, bool *playing_out, char *status, size_t status_size)
{
    char *xml = NULL;
    bool ok = soap_post(host, "AVTransport", "GetTransportInfo",
                        "<InstanceID>0</InstanceID>", &xml);
    if (!ok) return false;

    char state[48] = {0};
    xml_extract_tag(xml, "CurrentTransportState", state, sizeof(state));
    free(xml);

    bool playing = strcmp(state, "PLAYING") == 0;
    if (playing_out) *playing_out = playing;
    if (status) {
        if (playing) str_copy(status, status_size, "Spielt");
        else if (strcmp(state, "PAUSED_PLAYBACK") == 0) str_copy(status, status_size, "Pause");
        else if (state[0]) str_copy(status, status_size, state);
        else str_copy(status, status_size, "Verbunden");
    }
    return true;
}

static bool sonos_get_position(const char *host, sonos_player_t *out)
{
    char *xml = NULL;
    bool ok = soap_post(host, "AVTransport", "GetPositionInfo",
                        "<InstanceID>0</InstanceID>", &xml);
    if (!ok) return false;

    char duration[32] = {0};
    char rel_time[32] = {0};
    char uri[256] = {0};
    char metadata[4096] = {0};
    char title[sizeof(out->title)] = {0};
    char artist[sizeof(out->artist)] = {0};
    char album[sizeof(out->album)] = {0};
    char source[sizeof(out->source)] = {0};
    char art_path[sizeof(out->cover_url)] = {0};

    xml_extract_tag(xml, "TrackDuration", duration, sizeof(duration));
    xml_extract_tag(xml, "RelTime", rel_time, sizeof(rel_time));
    xml_extract_tag(xml, "TrackURI", uri, sizeof(uri));
    xml_extract_tag(xml, "TrackMetaData", metadata, sizeof(metadata));
    free(xml);

    clean_text(metadata);
    clean_text(uri);
    xml_extract_tag(metadata, "dc:title", title, sizeof(title));
    xml_extract_tag(metadata, "dc:creator", artist, sizeof(artist));
    if (!artist[0]) xml_extract_tag(metadata, "upnp:artist", artist, sizeof(artist));
    xml_extract_tag(metadata, "upnp:album", album, sizeof(album));
    xml_extract_tag(metadata, "upnp:albumArtURI", art_path, sizeof(art_path));
    clean_text(art_path);
    if (art_path[0]) {
        if (art_path[0] == '/') {
            snprintf(out->cover_url, sizeof(out->cover_url),
                     "http://%s:%d%s", host, SONOS_PORT, art_path);
        } else {
            str_copy(out->cover_url, sizeof(out->cover_url), art_path);
        }
    }
    sanitize_music_text(title);
    sanitize_music_text(artist);
    sanitize_music_text(album);
    if (detect_source_text(uri, metadata, source, sizeof(source))) {
        str_copy(out->source, sizeof(out->source), source);
    }

    if (!title[0]) str_copy(title, sizeof(title), "Kein Titel");
    if (!artist[0]) str_copy(artist, sizeof(artist), "Sonos");

    str_copy(out->title, sizeof(out->title), title);
    str_copy(out->artist, sizeof(out->artist), artist);
    str_copy(out->album, sizeof(out->album), album);
    out->duration_sec = parse_sonos_time(duration);
    out->position_sec = parse_sonos_time(rel_time);
    return true;
}

static bool sonos_get_media_source(const char *host, char *source, size_t source_size)
{
    char *xml = NULL;
    bool ok = soap_post(host, "AVTransport", "GetMediaInfo",
                        "<InstanceID>0</InstanceID>", &xml);
    if (!ok) return false;

    char uri[256] = {0};
    char metadata[512] = {0};
    xml_extract_tag(xml, "CurrentURI", uri, sizeof(uri));
    xml_extract_tag(xml, "CurrentURIMetaData", metadata, sizeof(metadata));
    free(xml);

    clean_text(uri);
    clean_text(metadata);
    return detect_source_text(uri, metadata, source, source_size);
}

static bool sonos_get_volume(const char *host, int *volume_out)
{
    char *xml = NULL;
    bool ok = soap_post(host, "RenderingControl", "GetVolume",
                        "<InstanceID>0</InstanceID><Channel>Master</Channel>", &xml);
    if (!ok) return false;

    char volume[16] = {0};
    xml_extract_tag(xml, "CurrentVolume", volume, sizeof(volume));
    free(xml);
    int v = atoi(volume);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (volume_out) *volume_out = v;
    return volume[0] != '\0';
}

static bool sonos_get_mute(const char *host, bool *muted_out)
{
    char *xml = NULL;
    bool ok = soap_post(host, "RenderingControl", "GetMute",
                        "<InstanceID>0</InstanceID><Channel>Master</Channel>", &xml);
    if (!ok) return false;

    char muted[16] = {0};
    xml_extract_tag(xml, "CurrentMute", muted, sizeof(muted));
    free(xml);

    // Sonos liefert hier normalerweise 0/1; true bleibt als robuster Fallback drin.
    if (muted_out) *muted_out = atoi(muted) != 0 || contains_ci(muted, "true");
    return muted[0] != '\0';
}

static void update_player(int index, const sonos_player_t *fresh)
{
    if (index < 0 || index >= HOST_COUNT || !fresh) return;

    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_players[index] = *fresh;
    if (fresh->playing || !s_players[s_active_player].online) {
        s_active_player = index;
    }
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
}

static void apply_seek_playing_hold(int index, sonos_player_t *p)
{
    if (index < 0 || index >= HOST_COUNT || !p || !s_state_mutex) return;
    if (!p->online) return;

    uint32_t now = (uint32_t)esp_log_timestamp();
    bool hold_active = false;
    bool hold_playing = false;

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    if (s_seek_hold_until_ms[index] != 0 && now < s_seek_hold_until_ms[index]) {
        hold_active = true;
        hold_playing = s_seek_hold_playing[index];
    } else {
        s_seek_hold_until_ms[index] = 0;
    }
    xSemaphoreGive(s_state_mutex);

    if (hold_active) {
        // Sonos meldet beim Seek oft kurz TRANSITIONING; das soll das Icon nicht flackern lassen.
        p->playing = hold_playing;
    }
}

static bool poll_player(int index)
{
    sonos_player_t p = {0};
    const char *host = s_sonos_hosts[index];
    str_copy(p.host, sizeof(p.host), host);
    str_copy(p.source, sizeof(p.source), "Sonos");
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    str_copy(p.room, sizeof(p.room), s_players[index].room);
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
    if (!p.room[0]) format_room_name(host, p.room, sizeof(p.room));

    bool transport_ok = sonos_get_transport(host, &p.playing, p.status, sizeof(p.status));
    bool position_ok = false;
    bool volume_ok = false;
    bool mute_ok = false;
    if (transport_ok) {
        // Raumname kommt direkt vom Sonos-Geraet; Fallback bleibt die IP.
        sonos_get_device_room(host, p.room, sizeof(p.room));
        sonos_get_media_source(host, p.source, sizeof(p.source));
        position_ok = sonos_get_position(host, &p);
        volume_ok = sonos_get_volume(host, &p.volume);
        mute_ok = sonos_get_mute(host, &p.muted);
    }

    p.online = transport_ok;
    p.last_update_ms = (uint32_t)esp_log_timestamp();
    if (!position_ok) {
        str_copy(p.title, sizeof(p.title), "Sonos verbunden");
        str_copy(p.artist, sizeof(p.artist), host);
    }
    if (!volume_ok) p.volume = 0;
    if (!mute_ok) p.muted = false;
    apply_seek_playing_hold(index, &p);
    update_player(index, &p);
    return transport_ok;
}

static void set_volume(const char *host, int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    char inner[128];
    snprintf(inner, sizeof(inner),
             "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredVolume>%d</DesiredVolume>",
             volume);
    soap_post(host, "RenderingControl", "SetVolume", inner, NULL);
}

static void set_mute(const char *host, bool muted)
{
    char inner[128];
    snprintf(inner, sizeof(inner),
             "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredMute>%d</DesiredMute>",
             muted ? 1 : 0);
    soap_post(host, "RenderingControl", "SetMute", inner, NULL);
}

static void transport_action(const char *host, const char *action)
{
    const char *inner = "<InstanceID>0</InstanceID>";
    if (strcmp(action, "Play") == 0) {
        inner = "<InstanceID>0</InstanceID><Speed>1</Speed>";
    }
    soap_post(host, "AVTransport", action, inner, NULL);
}

static void seek_to_second(const char *host, int seconds)
{
    if (seconds < 0) seconds = 0;

    int h = seconds / 3600;
    int m = (seconds / 60) % 60;
    int s = seconds % 60;

    char inner[160];
    // Sonos erwartet absolute Trackzeit als HH:MM:SS.
    snprintf(inner, sizeof(inner),
             "<InstanceID>0</InstanceID><Unit>REL_TIME</Unit>"
             "<Target>%02d:%02d:%02d</Target>", h, m, s);
    soap_post(host, "AVTransport", "Seek", inner, NULL);
}

static void set_active_seek_hint(int seconds, bool keep_playing)
{
    if (seconds < 0) seconds = 0;
    if (!s_state_mutex) return;

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    int idx = s_active_player;
    if (idx >= 0 && idx < HOST_COUNT) {
        if (s_players[idx].duration_sec > 0 && seconds > s_players[idx].duration_sec) {
            seconds = s_players[idx].duration_sec;
        }
        // UI sofort auf die Zielposition setzen; der naechste Poll bestaetigt Sonos.
        s_players[idx].position_sec = seconds;
        s_players[idx].playing = keep_playing;
        s_players[idx].last_update_ms = (uint32_t)esp_log_timestamp();
        s_seek_hold_playing[idx] = keep_playing;
        s_seek_hold_until_ms[idx] = s_players[idx].last_update_ms + SONOS_SEEK_HOLD_MS;
    }
    xSemaphoreGive(s_state_mutex);
}

void sonos_queue_cmd(sonos_cmd_type_t type, int value)
{
    if (!s_cmd_queue) return;
    sonos_cmd_t cmd = {.type = type, .value = value};
    xQueueSend(s_cmd_queue, &cmd, 0);
}

void sonos_cover_set_callback(sonos_cover_cb_t cb, void *user)
{
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_cover_cb = cb;
    s_cover_cb_user = user;
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
}

static void cover_publish(const sonos_cover_image_t *img)
{
    sonos_cover_cb_t cb;
    void *user;
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    cb = s_cover_cb;
    user = s_cover_cb_user;
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
    if (cb) cb(img, user);
    else if (img && img->pixels) free(img->pixels); // niemand interessiert -> nicht lecken
}

typedef struct {
    uint8_t *data;
    int len;
    int cap;
    int max;
    bool overflow;
} cover_dl_buf_t;

static esp_err_t cover_dl_event_handler(esp_http_client_event_t *evt)
{
    cover_dl_buf_t *b = (cover_dl_buf_t *)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || !b || !evt->data || evt->data_len <= 0) {
        return ESP_OK;
    }
    if (b->overflow) return ESP_OK;
    int needed = b->len + evt->data_len;
    if (needed > b->max) {
        b->overflow = true;
        return ESP_OK;
    }
    if (needed > b->cap) {
        int new_cap = b->cap ? b->cap : 8192;
        while (new_cap < needed) new_cap *= 2;
        if (new_cap > b->max) new_cap = b->max;
        uint8_t *next = heap_caps_realloc(b->data, new_cap,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!next) {
            b->overflow = true;
            return ESP_OK;
        }
        b->data = next;
        b->cap = new_cap;
    }
    memcpy(b->data + b->len, evt->data, evt->data_len);
    b->len += evt->data_len;
    return ESP_OK;
}

static bool cover_download(const char *url, uint8_t **out, int *out_len)
{
    *out = NULL;
    *out_len = 0;

    cover_dl_buf_t buf = {.max = COVER_MAX_BYTES};
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 5000,
        .event_handler = cover_dl_event_handler,
        .user_data = &buf,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300 || buf.overflow || buf.len < 4) {
        ESP_LOGW(TAG, "cover dl failed status=%d err=%s len=%d overflow=%d",
                 status, esp_err_to_name(err), buf.len, buf.overflow);
        free(buf.data);
        return false;
    }
    *out = buf.data;
    *out_len = buf.len;
    return true;
}

typedef struct {
    const uint8_t *jpeg;
    int jpeg_len;
    int read_pos;
    uint16_t *out_pixels;
    int out_w;
    int out_h;
} cover_jd_ctx_t;

static UINT cover_jd_input(JDEC *jd, BYTE *buf, UINT len)
{
    cover_jd_ctx_t *c = (cover_jd_ctx_t *)jd->device;
    int remaining = c->jpeg_len - c->read_pos;
    if (remaining <= 0) return 0;
    int take = (int)len;
    if (take > remaining) take = remaining;
    if (buf) {
        memcpy(buf, c->jpeg + c->read_pos, take);
    }
    // Ohne buf: nur "skippen" (Spec von TJpgDec verlangt das)
    c->read_pos += take;
    return (UINT)take;
}

static UINT cover_jd_output(JDEC *jd, void *bitmap, JRECT *rect)
{
    cover_jd_ctx_t *c = (cover_jd_ctx_t *)jd->device;
    const uint8_t *src = (const uint8_t *)bitmap;
    int x0 = rect->left, y0 = rect->top;
    int x1 = rect->right, y1 = rect->bottom;
    int bw = x1 - x0 + 1;

    for (int y = y0; y <= y1; y++) {
        if (y >= c->out_h) break;
        uint16_t *dst = c->out_pixels + y * c->out_w + x0;
        const uint8_t *s = src + (y - y0) * bw * 3;
        int xmax = x1 < c->out_w - 1 ? x1 : c->out_w - 1;
        for (int x = x0; x <= xmax; x++) {
            uint8_t r = s[0], g = s[1], b = s[2];
            *dst++ = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            s += 3;
        }
    }
    return 1;
}

static bool cover_decode(const uint8_t *jpeg, int jpeg_len, sonos_cover_image_t *out)
{
    out->pixels = NULL;
    out->w = out->h = 0;

    void *work = heap_caps_malloc(COVER_TJPGD_POOL, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!work) {
        ESP_LOGW(TAG, "cover decode: pool alloc failed");
        return false;
    }

    cover_jd_ctx_t ctx = {.jpeg = jpeg, .jpeg_len = jpeg_len};
    JDEC jd;
    JRESULT r = jd_prepare(&jd, cover_jd_input, work, COVER_TJPGD_POOL, &ctx);
    if (r != JDR_OK) {
        ESP_LOGW(TAG, "jd_prepare failed: %d", (int)r);
        free(work);
        return false;
    }

    // Skala so klein wie noetig: groesste Kante nach Skalierung <= COVER_DECODE_MAX_DIM
    BYTE scale = 0;
    int max_dim = (int)(jd.width > jd.height ? jd.width : jd.height);
    while (scale < 3 && (max_dim >> scale) > COVER_DECODE_MAX_DIM) scale++;

    int dec_w = (int)(jd.width >> scale);
    int dec_h = (int)(jd.height >> scale);
    if (dec_w <= 0 || dec_h <= 0) {
        free(work);
        return false;
    }

    size_t pixel_bytes = (size_t)dec_w * dec_h * 2;
    uint16_t *pixels = heap_caps_malloc(pixel_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        ESP_LOGW(TAG, "cover decode: pixel alloc %u failed", (unsigned)pixel_bytes);
        free(work);
        return false;
    }
    memset(pixels, 0, pixel_bytes);
    ctx.out_pixels = pixels;
    ctx.out_w = dec_w;
    ctx.out_h = dec_h;

    r = jd_decomp(&jd, cover_jd_output, scale);
    free(work);
    if (r != JDR_OK) {
        ESP_LOGW(TAG, "jd_decomp failed: %d", (int)r);
        free(pixels);
        return false;
    }

    out->pixels = pixels;
    out->w = dec_w;
    out->h = dec_h;
    ESP_LOGI(TAG, "cover decoded src=%ux%u -> %dx%d (scale 1/%d, %u KB)",
             jd.width, jd.height, dec_w, dec_h, 1 << scale, (unsigned)(pixel_bytes / 1024));
    return true;
}

static bool cover_current_url_matches(const char *expected)
{
    sonos_player_t snap = sonos_snapshot_active();
    return strcmp(snap.cover_url, expected) == 0;
}

static void cover_run_pipeline(const char *url)
{
    uint8_t *jpeg = NULL;
    int jpeg_len = 0;
    if (!cover_download(url, &jpeg, &jpeg_len)) return;

    // Track koennte zwischen Download und Decode gewechselt haben.
    if (!cover_current_url_matches(url)) {
        free(jpeg);
        return;
    }

    sonos_cover_image_t img = {0};
    bool ok = cover_decode(jpeg, jpeg_len, &img);
    free(jpeg);
    if (!ok) return;

    if (!cover_current_url_matches(url)) {
        free(img.pixels);
        return;
    }
    cover_publish(&img);
}

static void cover_tick(void)
{
    sonos_player_t snap = sonos_snapshot_active();
    uint32_t now = (uint32_t)esp_log_timestamp();

    // Player offline -> Platzhalter wiederherstellen
    if (!snap.online) {
        if (s_cover_active_url[0]) {
            s_cover_active_url[0] = '\0';
            s_cover_pending_url[0] = '\0';
            cover_publish(NULL);
        }
        return;
    }

    // Leere URL kommt waehrend TRANSITIONING vor; altes Cover stehen lassen.
    if (!snap.cover_url[0]) return;

    // URL hat sich geaendert -> Debounce-Timer neu starten
    if (strcmp(snap.cover_url, s_cover_pending_url) != 0) {
        str_copy(s_cover_pending_url, sizeof(s_cover_pending_url), snap.cover_url);
        s_cover_pending_since_ms = now;
        return;
    }

    // Stabil genug und unterschiedlich zum aktiven Cover -> fetchen
    if (strcmp(s_cover_pending_url, s_cover_active_url) == 0) return;
    if (now - s_cover_pending_since_ms < COVER_DEBOUNCE_MS) return;

    char url_local[sizeof(s_cover_pending_url)];
    str_copy(url_local, sizeof(url_local), s_cover_pending_url);
    str_copy(s_cover_active_url, sizeof(s_cover_active_url), url_local);
    cover_run_pipeline(url_local);
}

static void sonos_task(void *arg)
{
    (void)arg;
    sonos_cmd_t cmd;
    uint32_t next_poll = 0;

    while (true) {
        if (!wifi_sta_is_connected()) {
            mark_wifi_waiting();
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        while (xQueueReceive(s_cmd_queue, &cmd, 0) == pdTRUE) {
            sonos_player_t snap = sonos_snapshot_active();
            if (cmd.type == SONOS_CMD_SELECT) {
                if (cmd.value >= 0 && cmd.value < HOST_COUNT) {
                    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
                    s_active_player = cmd.value;
                    xSemaphoreGive(s_state_mutex);
                }
                continue;
            }
            if (cmd.type == SONOS_CMD_RESCAN) {
                next_poll = 0;
                continue;
            }
            if (!snap.online) continue;

            switch (cmd.type) {
            case SONOS_CMD_PLAY:
                transport_action(snap.host, "Play");
                break;
            case SONOS_CMD_PAUSE:
                transport_action(snap.host, "Pause");
                break;
            case SONOS_CMD_NEXT:
                transport_action(snap.host, "Next");
                break;
            case SONOS_CMD_PREVIOUS:
                transport_action(snap.host, "Previous");
                break;
            case SONOS_CMD_VOLUME:
                set_volume(snap.host, cmd.value);
                break;
            case SONOS_CMD_TOGGLE_MUTE:
                set_mute(snap.host, !snap.muted);
                break;
            case SONOS_CMD_SEEK:
                if (snap.duration_sec > 0) {
                    if (cmd.value > snap.duration_sec) cmd.value = snap.duration_sec;
                    seek_to_second(snap.host, cmd.value);
                    set_active_seek_hint(cmd.value, snap.playing);
                }
                break;
            default:
                break;
            }
            next_poll = 0;
        }

        uint32_t now = (uint32_t)esp_log_timestamp();
        if (next_poll == 0 || now >= next_poll) {
            bool any_online = false;
            for (int i = 0; i < HOST_COUNT; i++) {
                if (poll_player(i)) any_online = true;
                vTaskDelay(pdMS_TO_TICKS(80));
            }
            if (!any_online) {
                xSemaphoreTake(s_state_mutex, portMAX_DELAY);
                for (int i = 0; i < HOST_COUNT; i++) {
                    s_players[i].online = false;
                    s_players[i].playing = false;
                    str_copy(s_players[i].status, sizeof(s_players[i].status), "Nicht erreichbar");
                }
                xSemaphoreGive(s_state_mutex);
            }
            next_poll = now + SONOS_POLL_MS;
        }

        cover_tick();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void sonos_service_start(void)
{
    if (s_task_started) return;
    if (!s_state_mutex || !s_cmd_queue) sonos_service_init();
    xTaskCreatePinnedToCore(sonos_task, "sonos_fetch", 16384, NULL, 4, NULL, 1);
    s_task_started = true;
}
