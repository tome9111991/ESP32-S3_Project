// LVGL-Oberflaeche im Stil der Sonos-App, angepasst auf 800x480 Querformat.

#include "ui_sonos.h"
#include "sonos_client.h"
#include "ui_assets.h"
#include "ui_queue.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#ifndef LV_SYMBOL_SHUFFLE
  #define LV_SYMBOL_SHUFFLE "S"
#endif
#ifndef LV_SYMBOL_LOOP
  #define LV_SYMBOL_LOOP "R"
#endif

static lv_obj_t *s_title_label;
static lv_obj_t *s_artist_label;
static lv_obj_t *s_source_badge;
static lv_obj_t *s_source_icon;
static lv_obj_t *s_source_badge_label;
static lv_obj_t *s_source_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_room_label;
static lv_obj_t *s_position_label;
static lv_obj_t *s_duration_label;
static lv_obj_t *s_volume_label;
static lv_obj_t *s_volume_icon_label;
static lv_obj_t *s_play_label;
static lv_obj_t *s_shuffle_btn;
static lv_obj_t *s_shuffle_label;
static lv_obj_t *s_loop_btn;
static lv_obj_t *s_loop_label;
static lv_obj_t *s_loop_one_label;
static lv_obj_t *s_status_dot;
static lv_obj_t *s_progress_slider;
static lv_obj_t *s_volume_slider;
static lv_obj_t *s_host_buttons[8];
static lv_obj_t *s_player_screen;
static lv_anim_t s_text_scroll_anim;
static bool s_text_scroll_anim_ready;
static bool s_progress_dragging;
static bool s_volume_dragging;
static uint32_t s_volume_hold_until_ms;
static uint32_t s_volume_last_send_ms;
static int s_volume_last_sent = -1;
static lv_obj_t *s_cover_image;
static lv_obj_t *s_cover_placeholder;
static lv_image_dsc_t *s_cover_dsc;
static uint16_t *s_cover_pixels;

#define TEXT_SCROLL_PAUSE_MS 3000
#define PROGRESS_TIMER_MS 250
#define PROGRESS_SCALE_MS 1000
#define PROGRESS_X 330
#define PROGRESS_Y 244
#define PROGRESS_W 420
#define PROGRESS_TOUCH_X 310
#define PROGRESS_TOUCH_Y 222
#define PROGRESS_TOUCH_W 460
#define PROGRESS_TOUCH_H 48
#define VOLUME_SEND_INTERVAL_MS 180
#define VOLUME_HOLD_MS 2500
// Verschiebt die obere Now-Playing-Gruppe im grauen Container etwas nach oben.
#define NOW_PLAYING_Y_SHIFT 35
// Schiebt die Transport-Control-Bar als Gruppe etwas weiter weg vom rechten Rand.
#define TRANSPORT_X_SHIFT 20

static const char *host_short_name(const char *host)
{
    const char *dot = strrchr(host, '.');
    return dot && dot[1] ? dot + 1 : host;
}

static void format_time(int seconds, char *out, size_t out_size)
{
    if (seconds <= 0) {
        snprintf(out, out_size, "--:--");
        return;
    }
    snprintf(out, out_size, "%d:%02d", seconds / 60, seconds % 60);
}

static void format_position_time(int seconds, char *out, size_t out_size)
{
    if (seconds < 0) seconds = 0;
    snprintf(out, out_size, "%d:%02d", seconds / 60, seconds % 60);
}

static int progress_position_ms(const sonos_player_t *snap)
{
    int pos_ms = snap->position_sec * PROGRESS_SCALE_MS;
    int max_ms = snap->duration_sec * PROGRESS_SCALE_MS;

    if (snap->playing && snap->duration_sec > 0 && snap->last_update_ms > 0) {
        uint32_t now = (uint32_t)esp_log_timestamp();
        // Zwischen zwei Sonos-Polls laeuft die Zeitleiste lokal weiter.
        pos_ms += (int)(now - snap->last_update_ms);
    }

    if (pos_ms < 0) pos_ms = 0;
    if (max_ms > 0 && pos_ms > max_ms) pos_ms = max_ms;
    return pos_ms;
}

static int progress_value_from_touch(lv_event_t *e)
{
    if (!s_progress_slider) return 0;

    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) indev = lv_indev_active();
    if (!indev) return (int)lv_slider_get_value(s_progress_slider);

    lv_point_t point;
    lv_area_t slider_coords;
    lv_indev_get_point(indev, &point);
    lv_obj_get_coords(s_progress_slider, &slider_coords);

    int32_t width = lv_area_get_width(&slider_coords);
    int32_t max = lv_slider_get_max_value(s_progress_slider);
    if (width <= 1 || max <= 0) return 0;

    int32_t x = point.x;
    if (x < slider_coords.x1) x = slider_coords.x1;
    if (x > slider_coords.x2) x = slider_coords.x2;

    return (int)(((int64_t)(x - slider_coords.x1) * max + (width - 1) / 2) / (width - 1));
}

static void progress_set_from_touch(lv_event_t *e)
{
    int pos_ms = progress_value_from_touch(e);
    lv_slider_set_value(s_progress_slider, pos_ms, LV_ANIM_OFF);

    char tbuf[16];
    // Grosse Touch-Zone: Fingerposition sofort als Zielzeit spiegeln.
    format_position_time((pos_ms + 500) / PROGRESS_SCALE_MS, tbuf, sizeof(tbuf));
    lv_label_set_text(s_position_label, tbuf);
}

static int volume_clamp(int volume)
{
    if (volume < 0) return 0;
    if (volume > 100) return 100;
    return volume;
}

static void volume_set_local(int volume)
{
    volume = volume_clamp(volume);
    if (s_volume_slider) lv_slider_set_value(s_volume_slider, volume, LV_ANIM_OFF);
    if (s_volume_label) lv_label_set_text_fmt(s_volume_label, "%d", volume);
}

static void volume_queue_live(int volume, bool force)
{
    uint32_t now = (uint32_t)esp_log_timestamp();
    volume = volume_clamp(volume);

    // Beim Ziehen nicht jede Pixelbewegung senden, aber Sonos trotzdem live nachfuehren.
    if (!force && s_volume_last_sent == volume) return;
    if (!force && s_volume_last_send_ms != 0 &&
        now - s_volume_last_send_ms < VOLUME_SEND_INTERVAL_MS) {
        return;
    }

    sonos_queue_cmd(SONOS_CMD_VOLUME, volume);
    s_volume_last_sent = volume;
    s_volume_last_send_ms = now;
    s_volume_hold_until_ms = now + VOLUME_HOLD_MS;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            lv_color_t color, int x, int y, int w, int h)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    return label;
}

static lv_obj_t *make_scroll_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                                   lv_color_t color, int x, int y, int w, int h)
{
    lv_obj_t *label = make_label(parent, text, font, color, x, y, w, h);
    if (!s_text_scroll_anim_ready) {
        lv_anim_init(&s_text_scroll_anim);
        lv_anim_set_delay(&s_text_scroll_anim, TEXT_SCROLL_PAUSE_MS);
        lv_anim_set_reverse_delay(&s_text_scroll_anim, TEXT_SCROLL_PAUSE_MS);
        lv_anim_set_repeat_delay(&s_text_scroll_anim, TEXT_SCROLL_PAUSE_MS);
        lv_anim_set_repeat_count(&s_text_scroll_anim, LV_ANIM_REPEAT_INFINITE);
        s_text_scroll_anim_ready = true;
    }

    // Lange Musiktexte scrollen mit kurzer Pause an beiden Enden.
    lv_obj_set_style_anim(label, &s_text_scroll_anim, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL);
    return label;
}

static void set_label_text_changed(lv_obj_t *label, const char *text)
{
    const char *next = text ? text : "";
    const char *current = lv_label_get_text(label);
    if (!current || strcmp(current, next) != 0) {
        lv_label_set_text(label, next);
    }
}

static void set_mode_button_active(lv_obj_t *btn, lv_obj_t *label, bool active, bool online)
{
    if (!btn || !label) return;

    // Aktive Shuffle/Repeat-Modi muessen auf dem grauen Panel sofort erkennbar sein.
    if (!online) {
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x8A8A8A), 0);
    } else if (active) {
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x202020), 0);
    } else {
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xCFCFCF), 0);
    }
}

static void set_loop_one_visible(bool repeat_one, bool online)
{
    if (!s_loop_one_label) return;

    // Die kleine 1 entspricht dem Sonos-Repeat-One-Zustand.
    if (online && repeat_one) {
        lv_obj_clear_flag(s_loop_one_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(s_loop_one_label, lv_color_hex(0x202020), 0);
    } else {
        lv_obj_add_flag(s_loop_one_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *make_icon_button(lv_obj_t *parent, const char *icon,
                                  int x, int y, int size, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, size / 2, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_34, 0);
    lv_obj_center(label);
    return btn;
}

static const lv_image_dsc_t *source_icon_for_name(const char *source)
{
    if (strcmp(source, "YouTube Music") == 0) return &icon_youtubemusic;
    if (strcmp(source, "Spotify") == 0) return &icon_spotify;
    if (strcmp(source, "SoundCloud") == 0) return &icon_soundcloud;
    if (strcmp(source, "Apple Music") == 0) return &icon_applemusic;
    if (strcmp(source, "Amazon Music") == 0) return &icon_amazonmusic;
    if (strcmp(source, "Deezer") == 0) return &icon_deezer;
    if (strcmp(source, "TIDAL") == 0) return &icon_tidal;
    if (strcmp(source, "TuneIn") == 0 || strcmp(source, "Radio") == 0) return &icon_tunein;
    return NULL;
}

static void set_source_badge(const char *source, bool online)
{
    const char *text = "SO";
    const lv_image_dsc_t *icon = online ? source_icon_for_name(source) : NULL;
    lv_color_t bg = lv_color_hex(0x202020);
    lv_color_t fg = lv_color_hex(0xFFFFFF);

    if (!online) {
        text = "--";
        bg = lv_color_hex(0x5C5C5C);
    } else if (strcmp(source, "YouTube Music") == 0) {
        text = "YT";
        bg = lv_color_hex(0xFF0033);
    } else if (strcmp(source, "Spotify") == 0) {
        text = "SP";
        bg = lv_color_hex(0x1DB954);
        fg = lv_color_hex(0x101010);
    } else if (strcmp(source, "SoundCloud") == 0) {
        text = "SC";
        bg = lv_color_hex(0xFF5500);
    } else if (strcmp(source, "Apple Music") == 0) {
        text = "AM";
        bg = lv_color_hex(0xF5F5F5);
        fg = lv_color_hex(0x101010);
    } else if (strcmp(source, "Amazon Music") == 0) {
        text = "AZ";
        bg = lv_color_hex(0x00A8E1);
        fg = lv_color_hex(0x101010);
    } else if (strcmp(source, "Deezer") == 0) {
        text = "DZ";
        bg = lv_color_hex(0xFF6A00);
    } else if (strcmp(source, "TIDAL") == 0) {
        text = "TD";
        bg = lv_color_hex(0x111111);
    } else if (strcmp(source, "TuneIn") == 0 || strcmp(source, "Radio") == 0) {
        text = "RD";
        bg = lv_color_hex(0x14D8CC);
        fg = lv_color_hex(0x101010);
    } else if (strcmp(source, "AirPlay") == 0) {
        text = "AP";
        bg = lv_color_hex(0xFFFFFF);
        fg = lv_color_hex(0x101010);
    } else if (strcmp(source, "Line-In") == 0) {
        text = "IN";
        bg = lv_color_hex(0x555555);
    } else if (strcmp(source, "TV") == 0) {
        text = "TV";
        bg = lv_color_hex(0x2A6BEA);
    } else if (strcmp(source, "Musikbibliothek") == 0) {
        text = "LB";
        bg = lv_color_hex(0x8E8E93);
        fg = lv_color_hex(0x101010);
    } else if (strcmp(source, "Stream") == 0) {
        text = "WEB";
        bg = lv_color_hex(0x6F4CFF);
    }

    lv_obj_set_style_bg_color(s_source_badge, bg, 0);
    if (icon) {
        lv_image_set_src(s_source_icon, icon);
        lv_obj_clear_flag(s_source_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_source_badge_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(s_source_badge_label, text);
        lv_obj_set_style_text_color(s_source_badge_label, fg, 0);
        lv_obj_add_flag(s_source_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_source_badge_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void play_clicked_cb(lv_event_t *e)
{
    (void)e;
    sonos_player_t snap = sonos_snapshot_active();
    sonos_queue_cmd(snap.playing ? SONOS_CMD_PAUSE : SONOS_CMD_PLAY, 0);
}

static void previous_clicked_cb(lv_event_t *e)
{
    (void)e;
    sonos_queue_cmd(SONOS_CMD_PREVIOUS, 0);
}

static void next_clicked_cb(lv_event_t *e)
{
    (void)e;
    sonos_queue_cmd(SONOS_CMD_NEXT, 0);
}

static void shuffle_clicked_cb(lv_event_t *e)
{
    (void)e;
    sonos_queue_cmd(SONOS_CMD_TOGGLE_SHUFFLE, 0);
}

static void repeat_clicked_cb(lv_event_t *e)
{
    (void)e;
    sonos_queue_cmd(SONOS_CMD_TOGGLE_REPEAT, 0);
}

static void rescan_clicked_cb(lv_event_t *e)
{
    (void)e;
    sonos_queue_cmd(SONOS_CMD_RESCAN, 0);
}

static void queue_clicked_cb(lv_event_t *e)
{
    (void)e;
    ui_queue_show();
}

static void host_clicked_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    sonos_queue_cmd(SONOS_CMD_SELECT, index);
}

static void volume_released_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int volume = (int)lv_slider_get_value(slider);
    s_volume_dragging = false;
    volume_set_local(volume);
    volume_queue_live(volume, true);
}

static void volume_pressed_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int volume = (int)lv_slider_get_value(slider);
    s_volume_dragging = true;
    volume_set_local(volume);
    volume_queue_live(volume, true);
}

static void volume_changed_cb(lv_event_t *e)
{
    if (!s_volume_dragging) return;

    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int volume = (int)lv_slider_get_value(slider);
    volume_set_local(volume);
    volume_queue_live(volume, false);
}

static void volume_press_lost_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int volume = (int)lv_slider_get_value(slider);
    s_volume_dragging = false;
    volume_set_local(volume);
    volume_queue_live(volume, true);
}

static void progress_pressed_cb(lv_event_t *e)
{
    s_progress_dragging = true;
    progress_set_from_touch(e);
}

static void progress_pressing_cb(lv_event_t *e)
{
    if (!s_progress_dragging) return;
    progress_set_from_touch(e);
}

static void progress_released_cb(lv_event_t *e)
{
    sonos_player_t snap = sonos_snapshot_active();
    progress_set_from_touch(e);
    int pos_ms = (int)lv_slider_get_value(s_progress_slider);
    s_progress_dragging = false;

    if (snap.online && snap.duration_sec > 0) {
        sonos_queue_cmd(SONOS_CMD_SEEK, (pos_ms + 500) / PROGRESS_SCALE_MS);
    }
}

static void mute_clicked_cb(lv_event_t *e)
{
    (void)e;
    sonos_queue_cmd(SONOS_CMD_TOGGLE_MUTE, 0);
}

static void ui_cover_cb(const sonos_cover_image_t *image, void *user)
{
    (void)user;

    // Vor dem Tausch alte Pixel + dsc zwischenspeichern und LVGL anhalten.
    if (!lvgl_port_lock(0)) {
        if (image && image->pixels) free(image->pixels);
        return;
    }

    lv_image_dsc_t *old_dsc = s_cover_dsc;
    uint16_t *old_pixels = s_cover_pixels;

    if (image && image->pixels && image->w > 0 && image->h > 0) {
        lv_image_dsc_t *dsc = calloc(1, sizeof(lv_image_dsc_t));
        if (!dsc) {
            free(image->pixels);
            lvgl_port_unlock();
            return;
        }
        dsc->header.w = image->w;
        dsc->header.h = image->h;
        dsc->header.cf = LV_COLOR_FORMAT_RGB565;
        dsc->header.stride = image->w * 2;
        dsc->data_size = (uint32_t)image->w * image->h * 2;
        dsc->data = (const uint8_t *)image->pixels;

        // Nahezu quadratisch (Spotify/Apple-Cover) -> COVER fuellt die Box bis
        // an die Kante (Integer-Rundung wuerde bei CONTAIN 1 px Schwarz lecken).
        // Echte 16:9-Thumbnails -> CONTAIN als Letterbox, damit nichts vom
        // eigentlichen Motiv weggeschnitten wird.
        int dw = image->w, dh = image->h;
        int diff = dw > dh ? dw - dh : dh - dw;
        bool nearly_square = diff * 20 < dw + dh; // ~5% Toleranz
        lv_image_set_inner_align(s_cover_image,
                                 nearly_square ? LV_IMAGE_ALIGN_COVER
                                               : LV_IMAGE_ALIGN_CONTAIN);

        lv_image_set_src(s_cover_image, dsc);
        lv_obj_clear_flag(s_cover_image, LV_OBJ_FLAG_HIDDEN);
        if (s_cover_placeholder) lv_obj_add_flag(s_cover_placeholder, LV_OBJ_FLAG_HIDDEN);
        s_cover_dsc = dsc;
        s_cover_pixels = image->pixels;
    } else {
        // Reset: zurueck auf SONOS-Platzhalter
        lv_image_set_src(s_cover_image, NULL);
        lv_obj_add_flag(s_cover_image, LV_OBJ_FLAG_HIDDEN);
        if (s_cover_placeholder) lv_obj_clear_flag(s_cover_placeholder, LV_OBJ_FLAG_HIDDEN);
        s_cover_dsc = NULL;
        s_cover_pixels = NULL;
    }

    lvgl_port_unlock();

    // LVGL haelt nach dem Unlock keine Referenz mehr auf das alte Bild.
    if (old_dsc) free(old_dsc);
    if (old_pixels) free(old_pixels);
}

static void ui_update_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    sonos_player_t snap = sonos_snapshot_active();

    set_label_text_changed(s_title_label, snap.title[0] ? snap.title : "Kein Titel");
    set_label_text_changed(s_artist_label, snap.artist[0] ? snap.artist : "Sonos");

    const char *source_name = snap.online ? (snap.source[0] ? snap.source : "Sonos") : "Offline";
    set_source_badge(source_name, snap.online);

    // Genug Reserve fuer Quelle und Host-IP mit aktivem -Wformat-truncation.
    char source[128];
    snprintf(source, sizeof(source), "%s  -  %s", source_name, snap.host);
    set_label_text_changed(s_source_label, source);
    set_label_text_changed(s_status_label, snap.status);
    set_label_text_changed(s_room_label, snap.room[0] ? snap.room : "Wohnzimmer");
    if (!s_progress_dragging) {
        lv_label_set_text(s_play_label, snap.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }

    lv_obj_set_style_bg_color(s_status_dot,
                              snap.online ? lv_color_hex(0x35D07F) : lv_color_hex(0x8A8A8A), 0);
    set_mode_button_active(s_shuffle_btn, s_shuffle_label, snap.shuffle, snap.online);
    set_mode_button_active(s_loop_btn, s_loop_label, snap.repeat, snap.online);
    set_loop_one_visible(snap.repeat_one, snap.online);

    int max = snap.duration_sec > 0 ? snap.duration_sec * PROGRESS_SCALE_MS : 100 * PROGRESS_SCALE_MS;
    int pos = progress_position_ms(&snap);
    if (pos > max) pos = max;
    lv_slider_set_range(s_progress_slider, 0, max);
    if (!s_progress_dragging) {
        lv_slider_set_value(s_progress_slider, pos, LV_ANIM_OFF);
    }

    char tbuf[16];
    if (!s_progress_dragging) {
        format_position_time((pos + 500) / PROGRESS_SCALE_MS, tbuf, sizeof(tbuf));
        lv_label_set_text(s_position_label, tbuf);
    }
    format_time(snap.duration_sec, tbuf, sizeof(tbuf));
    lv_label_set_text(s_duration_label, tbuf);

    uint32_t now = (uint32_t)esp_log_timestamp();
    bool volume_hold = s_volume_dragging;
    if (!volume_hold && s_volume_hold_until_ms != 0) {
        if ((int32_t)(s_volume_hold_until_ms - now) > 0) {
            volume_hold = true;
        } else {
            s_volume_hold_until_ms = 0;
        }
    }
    if (!volume_hold) {
        volume_set_local(snap.volume);
    }
    lv_label_set_text(s_volume_icon_label, snap.muted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(s_volume_icon_label,
                                snap.muted ? lv_color_hex(0xD0D0D0) : lv_color_hex(0xFFFFFF), 0);

    int active = sonos_active_index();
    int count = sonos_host_count();
    // Auswahlbuttons nur pflegen, wenn wirklich mehrere Player vorhanden sind.
    if (count > 1) {
        for (int i = 0; i < count && i < 8; i++) {
            lv_obj_set_style_bg_color(s_host_buttons[i],
                                      i == active ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x333333), 0);
            lv_obj_t *label = lv_obj_get_child(s_host_buttons[i], 0);
            if (label) {
                lv_obj_set_style_text_color(label,
                                            i == active ? lv_color_hex(0x202020) : lv_color_hex(0xFFFFFF), 0);
            }
        }
    }
}

void ui_sonos_build(void)
{
    if (!lvgl_port_lock(0)) return;

    lv_obj_t *scr = lv_screen_active();
    s_player_screen = scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x050505), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    // Panel ragt unten heraus, der Screen selbst soll aber fest stehen.
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Kompakte Kopfzeile, damit der Player unten mehr Platz bekommt.
    lv_obj_t *top = lv_obj_create(scr);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_size(top, 800, 56);
    lv_obj_set_style_bg_color(top, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_radius(top, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    // Leicht aus der Kopfzeile herausziehen, damit der Reload-Button wirklich hoeher wirkt.
    lv_obj_t *back = lv_button_create(top);
    lv_obj_set_pos(back, 22, -8);
    lv_obj_set_size(back, 42, 42);
    lv_obj_set_style_radius(back, 21, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x303434), 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, rescan_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(back_lbl);

    lv_obj_t *brand = lv_label_create(top);
    lv_label_set_text(brand, "SONOS");
    lv_obj_set_style_text_color(brand, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_34, 0);
    lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 1);

    s_status_dot = lv_obj_create(top);
    lv_obj_set_pos(s_status_dot, 690, 11);
    lv_obj_set_size(s_status_dot, 14, 14);
    lv_obj_set_style_radius(s_status_dot, 7, 0);
    lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(0x8A8A8A), 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);

    s_status_label = make_label(top, "START", &lv_font_montserrat_14,
                                lv_color_hex(0xD8D8D8), 712, 6, 80, 24);

    // Graue Hauptflaeche wie ein querformatiger Now-Playing-Sheet.
    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_pos(panel, 0, 56);
    // Etwas ueber den unteren Rand ziehen: unten wirken die Ecken dadurch eckig.
    lv_obj_set_size(panel, 800, 456);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x4A4A4A), 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 30, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    int count = sonos_host_count();
    // Bei nur einem Player bleibt die obere Auswahlzeile frei.
    if (count > 1) {
        for (int i = 0; i < count && i < 8; i++) {
            s_host_buttons[i] = lv_button_create(panel);
            lv_obj_set_pos(s_host_buttons[i], 26 + i * 74, 14);
            lv_obj_set_size(s_host_buttons[i], 62, 30);
            lv_obj_set_style_radius(s_host_buttons[i], 15, 0);
            lv_obj_set_style_bg_color(s_host_buttons[i], lv_color_hex(0x333333), 0);
            lv_obj_set_style_shadow_width(s_host_buttons[i], 0, 0);
            lv_obj_add_event_cb(s_host_buttons[i], host_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            lv_obj_t *host_lbl = lv_label_create(s_host_buttons[i]);
            lv_label_set_text(host_lbl, host_short_name(sonos_host_at(i)));
            lv_obj_set_style_text_color(host_lbl, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(host_lbl, &lv_font_montserrat_14, 0);
            lv_obj_center(host_lbl);
        }
    }

    // Cover-Box ist gleichzeitig schwarzer Letterbox-Rahmen und Bildbehaelter.
    // clip_corner sorgt dafuer, dass quadratische Cover sich an die 22 px Rundung
    // anpassen statt mit scharfen Ecken aus der Box herauszuragen.
    lv_obj_t *cover = lv_obj_create(panel);
    lv_obj_set_pos(cover, 38, 68 - NOW_PLAYING_Y_SHIFT);
    lv_obj_set_size(cover, 258, 258);
    lv_obj_set_style_radius(cover, 22, 0);
    lv_obj_set_style_bg_color(cover, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_dir(cover, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_width(cover, 0, 0);
    lv_obj_set_style_pad_all(cover, 0, 0);
    lv_obj_clear_flag(cover, LV_OBJ_FLAG_SCROLLABLE);

    // Platzhalter zuerst anlegen -> liegt unter dem Bild. Sichtbar solange das
    // Image-Widget noch keine echte Source hat (Boot, Player offline, kein Cover).
    s_cover_placeholder = lv_obj_create(cover);
    lv_obj_set_size(s_cover_placeholder, 258, 258);
    lv_obj_align(s_cover_placeholder, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(s_cover_placeholder, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cover_placeholder, 0, 0);
    lv_obj_set_style_pad_all(s_cover_placeholder, 0, 0);
    lv_obj_clear_flag(s_cover_placeholder, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_cover_placeholder, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *ph_icon = lv_label_create(s_cover_placeholder);
    lv_label_set_text(ph_icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(ph_icon, lv_color_hex(0xEDEDED), 0);
    lv_obj_set_style_text_font(ph_icon, &lv_font_montserrat_48, 0);
    lv_obj_align(ph_icon, LV_ALIGN_CENTER, 0, -18);

    lv_obj_t *ph_text = lv_label_create(s_cover_placeholder);
    lv_label_set_text(ph_text, "SONOS");
    lv_obj_set_style_text_color(ph_text, lv_color_hex(0x8C8C8C), 0);
    lv_obj_set_style_text_font(ph_text, &lv_font_montserrat_24, 0);
    lv_obj_align(ph_text, LV_ALIGN_CENTER, 0, 46);

    // Bewusst 4 px groesser als die 258x258-Box: der Cover-Container clippt
    // seine Kinder am Rand, dadurch lecken Integer-Rundungsfehler in LVGLs
    // Skalierungsmathe keinen schwarzen Pixelrand mehr durch.
    s_cover_image = lv_image_create(cover);
    lv_obj_set_size(s_cover_image, 262, 262);
    lv_obj_align(s_cover_image, LV_ALIGN_CENTER, 0, 0);
    lv_image_set_inner_align(s_cover_image, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_add_flag(s_cover_image, LV_OBJ_FLAG_HIDDEN);

    s_title_label = make_scroll_label(panel, "Warte auf Sonos", &lv_font_montserrat_36,
                                      lv_color_hex(0xFFFFFF), 328, 74 - NOW_PLAYING_Y_SHIFT, 438, 52);
    s_artist_label = make_scroll_label(panel, "Verbinde mit WLAN", &lv_font_montserrat_18,
                                       lv_color_hex(0xD5D5D5), 330, 131 - NOW_PLAYING_Y_SHIFT, 416, 28);

    // Source steht direkt unter dem Interpreten; die Album-Zeile wird nicht angezeigt.
    s_source_badge = lv_obj_create(panel);
    lv_obj_set_pos(s_source_badge, 330, 166 - NOW_PLAYING_Y_SHIFT);
    lv_obj_set_size(s_source_badge, 40, 24);
    lv_obj_set_style_radius(s_source_badge, 6, 0);
    lv_obj_set_style_bg_color(s_source_badge, lv_color_hex(0x202020), 0);
    lv_obj_set_style_border_width(s_source_badge, 0, 0);
    lv_obj_clear_flag(s_source_badge, LV_OBJ_FLAG_SCROLLABLE);

    s_source_icon = lv_image_create(s_source_badge);
    lv_image_set_src(s_source_icon, &icon_youtubemusic);
    lv_obj_center(s_source_icon);
    lv_obj_add_flag(s_source_icon, LV_OBJ_FLAG_HIDDEN);

    s_source_badge_label = lv_label_create(s_source_badge);
    lv_label_set_text(s_source_badge_label, "SO");
    lv_obj_set_style_text_color(s_source_badge_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_source_badge_label, &lv_font_montserrat_14, 0);
    lv_obj_center(s_source_badge_label);

    s_source_label = make_scroll_label(panel, "Sonos", &lv_font_montserrat_18,
                                       lv_color_hex(0xFFFFFF), 382, 166 - NOW_PLAYING_Y_SHIFT, 368, 30);

    s_progress_slider = lv_slider_create(panel);
    lv_obj_set_pos(s_progress_slider, PROGRESS_X, PROGRESS_Y);
    lv_obj_set_size(s_progress_slider, PROGRESS_W, 8);
    lv_slider_set_range(s_progress_slider, 0, 100 * PROGRESS_SCALE_MS);
    lv_slider_set_value(s_progress_slider, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_progress_slider, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress_slider, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_progress_slider, lv_color_hex(0x616161), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress_slider, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_progress_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_width(s_progress_slider, 18, LV_PART_KNOB);
    lv_obj_set_style_height(s_progress_slider, 18, LV_PART_KNOB);

    // Unsichtbare, hohe Trefferflaeche: mit dem Finger deutlich leichter zu treffen.
    lv_obj_t *progress_touch = lv_obj_create(panel);
    lv_obj_set_pos(progress_touch, PROGRESS_TOUCH_X, PROGRESS_TOUCH_Y);
    lv_obj_set_size(progress_touch, PROGRESS_TOUCH_W, PROGRESS_TOUCH_H);
    lv_obj_set_style_bg_opa(progress_touch, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(progress_touch, 0, 0);
    lv_obj_set_style_shadow_width(progress_touch, 0, 0);
    lv_obj_clear_flag(progress_touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(progress_touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(progress_touch, progress_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(progress_touch, progress_pressing_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(progress_touch, progress_released_cb, LV_EVENT_RELEASED, NULL);

    s_position_label = make_label(panel, "0:00", &lv_font_montserrat_18,
                                  lv_color_hex(0xDADADA), 328, 262, 90, 24);
    s_duration_label = make_label(panel, "--:--", &lv_font_montserrat_18,
                                  lv_color_hex(0xDADADA), 680, 262, 70, 24);
    lv_obj_set_style_text_align(s_duration_label, LV_TEXT_ALIGN_RIGHT, 0);

    s_shuffle_btn = make_icon_button(panel, LV_SYMBOL_SHUFFLE,
                                     342 - TRANSPORT_X_SHIFT, 288, 54, shuffle_clicked_cb);
    s_shuffle_label = lv_obj_get_child(s_shuffle_btn, 0);
    make_icon_button(panel, LV_SYMBOL_PREV,
                     430 - TRANSPORT_X_SHIFT, 286, 58, previous_clicked_cb);

    lv_obj_t *play_btn = lv_button_create(panel);
    lv_obj_set_pos(play_btn, 518 - TRANSPORT_X_SHIFT, 270);
    lv_obj_set_size(play_btn, 84, 84);
    lv_obj_set_style_radius(play_btn, 42, 0);
    lv_obj_set_style_bg_color(play_btn, lv_color_hex(0x5A5A5A), 0);
    lv_obj_set_style_shadow_width(play_btn, 0, 0);
    lv_obj_add_event_cb(play_btn, play_clicked_cb, LV_EVENT_CLICKED, NULL);
    s_play_label = lv_label_create(play_btn);
    lv_label_set_text(s_play_label, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(s_play_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_play_label, &lv_font_montserrat_48, 0);
    lv_obj_center(s_play_label);

    make_icon_button(panel, LV_SYMBOL_NEXT,
                     632 - TRANSPORT_X_SHIFT, 286, 58, next_clicked_cb);
    s_loop_btn = make_icon_button(panel, LV_SYMBOL_LOOP,
                                  724 - TRANSPORT_X_SHIFT, 288, 54, repeat_clicked_cb);
    s_loop_label = lv_obj_get_child(s_loop_btn, 0);
    s_loop_one_label = lv_label_create(s_loop_btn);
    lv_label_set_text(s_loop_one_label, "1");
    lv_obj_set_style_text_color(s_loop_one_label, lv_color_hex(0x202020), 0);
    lv_obj_set_style_text_font(s_loop_one_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_loop_one_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_loop_one_label, LV_OBJ_FLAG_HIDDEN);

    // Lautsprecher ist ein Button: Tippen schaltet Sonos-Mute um.
    lv_obj_t *vol_btn = lv_button_create(panel);
    lv_obj_set_pos(vol_btn, 324, 352);
    lv_obj_set_size(vol_btn, 56, 56);
    lv_obj_set_style_radius(vol_btn, 28, 0);
    lv_obj_set_style_bg_opa(vol_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vol_btn, 0, 0);
    lv_obj_set_style_shadow_width(vol_btn, 0, 0);
    lv_obj_add_event_cb(vol_btn, mute_clicked_cb, LV_EVENT_CLICKED, NULL);
    s_volume_icon_label = lv_label_create(vol_btn);
    lv_label_set_text(s_volume_icon_label, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(s_volume_icon_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_volume_icon_label, &lv_font_montserrat_24, 0);
    lv_obj_center(s_volume_icon_label);

    // Der Slider braucht wegen des Knobs Reserve unterhalb der eigentlichen 8 px Hoehe.
    s_volume_slider = lv_slider_create(panel);
    lv_obj_set_pos(s_volume_slider, 388, 376);
    lv_obj_set_size(s_volume_slider, 310, 8);
    lv_slider_set_range(s_volume_slider, 0, 100);
    lv_obj_set_ext_click_area(s_volume_slider, 24);
    lv_obj_add_event_cb(s_volume_slider, volume_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_volume_slider, volume_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_volume_slider, volume_released_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_volume_slider, volume_press_lost_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_set_style_bg_color(s_volume_slider, lv_color_hex(0x666666), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_volume_slider, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_volume_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);

    s_volume_label = make_label(panel, "0", &lv_font_montserrat_24,
                                lv_color_hex(0xE8E8E8), 720, 362, 48, 34);
    lv_obj_set_style_text_align(s_volume_label, LV_TEXT_ALIGN_RIGHT, 0);

    // Links in der Lautstaerke-Zeile oeffnet dieser Button die Sonos-Queue.
    lv_obj_t *queue_btn = lv_button_create(panel);
    lv_obj_set_pos(queue_btn, 38, 338);
    lv_obj_set_size(queue_btn, 56, 56);
    lv_obj_set_style_radius(queue_btn, 28, 0);
    lv_obj_set_style_bg_color(queue_btn, lv_color_hex(0x5A5A5A), 0);
    lv_obj_set_style_shadow_width(queue_btn, 0, 0);
    lv_obj_add_event_cb(queue_btn, queue_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *queue_lbl = lv_label_create(queue_btn);
    lv_label_set_text(queue_lbl, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(queue_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(queue_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(queue_lbl);

    // Raumname links unter das Cover setzen, damit er die Lautstaerke-Zeile nicht ueberdeckt.
    s_room_label = make_scroll_label(panel, "Wohnzimmer", &lv_font_montserrat_24,
                                     lv_color_hex(0xFFFFFF), 38, 338 - NOW_PLAYING_Y_SHIFT, 258, 34);
    lv_obj_set_style_text_align(s_room_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_timer_create(ui_update_timer_cb, PROGRESS_TIMER_MS, NULL);
    lvgl_port_unlock();

    sonos_cover_set_callback(ui_cover_cb, NULL);
}

void ui_sonos_show(void)
{
    if (s_player_screen) lv_screen_load(s_player_screen);
}
