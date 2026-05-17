// Eigener Screen fuer die Sonos-Queue.

#include "ui_queue.h"
#include "sonos_client.h"
#include "ui_sonos.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"

#define QUEUE_TIMER_MS 300
#define QUEUE_ROW_STEP 76
#define QUEUE_ROW_H    70
#define QUEUE_CLICK_MOVE_LIMIT_PX 14
#define QUEUE_CLICK_SCROLL_LIMIT_PX 3
#define QUEUE_SCROLL_CLICK_BLOCK_MS 220

static lv_obj_t *s_queue_screen;
static lv_obj_t *s_queue_list;
static lv_obj_t *s_queue_status;
static lv_obj_t *s_queue_overlay;
static lv_obj_t *s_arrow_left;
static lv_obj_t *s_arrow_right;
static lv_timer_t *s_queue_timer;
static uint32_t s_seen_update_ms;
static int s_seen_count = -1;
static int s_seen_start = -1;
static int s_seen_current_index = -1;
static bool s_seen_loading;
static sonos_queue_track_t s_view_tracks[SONOS_QUEUE_MAX_TRACKS];
static int s_view_start;
static int s_view_total;
static int s_view_current_index;
static bool s_scroll_to_current_on_open;
static int s_scroll_to_current_start = -1;
static lv_obj_t *s_pressed_row;
static lv_point_t s_press_point;
static int s_press_scroll_y;
static bool s_press_moved;
static uint32_t s_scroll_click_block_until_ms;

static int abs_i(int value)
{
    return value < 0 ? -value : value;
}

static bool click_block_active(void)
{
    return (int32_t)(s_scroll_click_block_until_ms - lv_tick_get()) > 0;
}

static void get_touch_point(lv_event_t *e, lv_point_t *point)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) indev = lv_indev_active();
    if (indev) {
        lv_indev_get_point(indev, point);
    } else {
        point->x = 0;
        point->y = 0;
    }
}

static void block_track_click_after_scroll(void)
{
    s_press_moved = true;
    s_scroll_click_block_until_ms = lv_tick_get() + QUEUE_SCROLL_CLICK_BLOCK_MS;
}

static void track_pressed_cb(lv_event_t *e)
{
    s_pressed_row = (lv_obj_t *)lv_event_get_target(e);
    get_touch_point(e, &s_press_point);
    s_press_scroll_y = s_queue_list ? lv_obj_get_scroll_y(s_queue_list) : 0;

    // Direkt nach einem Scroll nicht sofort wieder einen Track ausloesen.
    s_press_moved = click_block_active();
}

static void track_pressing_cb(lv_event_t *e)
{
    if ((lv_obj_t *)lv_event_get_target(e) != s_pressed_row) return;

    lv_point_t point;
    get_touch_point(e, &point);
    int dy = abs_i(point.y - s_press_point.y);
    int dx = abs_i(point.x - s_press_point.x);
    int scroll_delta = s_queue_list ? abs_i(lv_obj_get_scroll_y(s_queue_list) - s_press_scroll_y) : 0;

    if (dy > QUEUE_CLICK_MOVE_LIMIT_PX || dx > QUEUE_CLICK_MOVE_LIMIT_PX ||
        scroll_delta > QUEUE_CLICK_SCROLL_LIMIT_PX) {
        block_track_click_after_scroll();
    }
}

static bool track_click_is_valid(lv_event_t *e)
{
    lv_obj_t *target = (lv_obj_t *)lv_event_get_target(e);
    if (s_pressed_row && target != s_pressed_row) return false;
    if (s_press_moved || click_block_active()) return false;
    if (s_queue_list && lv_obj_has_state(s_queue_list, LV_STATE_SCROLLED)) return false;

    lv_point_t point;
    get_touch_point(e, &point);
    int dy = abs_i(point.y - s_press_point.y);
    int dx = abs_i(point.x - s_press_point.x);
    int scroll_delta = s_queue_list ? abs_i(lv_obj_get_scroll_y(s_queue_list) - s_press_scroll_y) : 0;

    return dy <= QUEUE_CLICK_MOVE_LIMIT_PX && dx <= QUEUE_CLICK_MOVE_LIMIT_PX &&
           scroll_delta <= QUEUE_CLICK_SCROLL_LIMIT_PX;
}

static void queue_scroll_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCROLL_BEGIN || code == LV_EVENT_SCROLL ||
        code == LV_EVENT_SCROLL_THROW_BEGIN || code == LV_EVENT_SCROLL_END) {
        block_track_click_after_scroll();
    }
}

static void format_track_time(int seconds, char *out, size_t out_size)
{
    if (seconds <= 0) {
        snprintf(out, out_size, "--:--");
        return;
    }
    snprintf(out, out_size, "%d:%02d", seconds / 60, seconds % 60);
}

static void back_clicked_cb(lv_event_t *e)
{
    (void)e;
    ui_sonos_show();
}

static void track_clicked_cb(lv_event_t *e)
{
    bool valid_click = track_click_is_valid(e);
    s_pressed_row = NULL;
    if (!valid_click) return;

    int track_number = (int)(intptr_t)lv_event_get_user_data(e);
    if (track_number <= 0) return;

    // Tracknummer ist bei Sonos 1-basiert und entspricht der Anzeige links.
    sonos_queue_cmd(SONOS_CMD_QUEUE_PLAY_TRACK, track_number);
}

static lv_obj_t *make_header_button(lv_obj_t *parent, const char *symbol, int x, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, 7);
    lv_obj_set_size(btn, 42, 42);
    lv_obj_set_style_radius(btn, 21, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x303434), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_center(label);
    return btn;
}

#define EQ_BAR_BASE_Y   32   // Unterkante aller Balken innerhalb der EQ-Box
#define EQ_BAR_MIN_H    4
#define EQ_BAR_MAX_H    30
#define EQ_BAR_WIDTH    6

static void eq_anim_cb(void *var, int32_t h)
{
    lv_obj_t *bar = (lv_obj_t *)var;
    lv_obj_set_height(bar, h);
    lv_obj_set_y(bar, EQ_BAR_BASE_Y - h);
}

static void add_eq_bar(lv_obj_t *parent, int x, uint32_t period_ms, uint32_t delay_ms)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, x, EQ_BAR_BASE_Y - EQ_BAR_MIN_H);
    lv_obj_set_size(bar, EQ_BAR_WIDTH, EQ_BAR_MIN_H);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, eq_anim_cb);
    lv_anim_set_values(&a, EQ_BAR_MIN_H, EQ_BAR_MAX_H);
    lv_anim_set_duration(&a, period_ms);
    lv_anim_set_playback_duration(&a, period_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_start(&a);
}

static void make_eq_indicator(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, 26, 34);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);

    add_eq_bar(box,  2, 520,   0);
    add_eq_bar(box, 10, 380, 130);
    add_eq_bar(box, 18, 640, 260);
}

static void make_track_row(lv_obj_t *parent, int index, int track_number,
                           const sonos_queue_track_t *track, bool is_current)
{
    int y = index * QUEUE_ROW_STEP;
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_pos(row, 8, y);
    lv_obj_set_size(row, 736, 70);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(is_current ? 0x4A4A4A : 0x3A3A3A), 0);
    lv_obj_set_style_border_width(row, is_current ? 2 : 0, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, track_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(row, track_pressing_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(row, track_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)track_number);

    lv_obj_t *nr = lv_label_create(row);
    lv_label_set_text_fmt(nr, "%d", track_number);
    lv_obj_set_style_text_color(nr, lv_color_hex(is_current ? 0xFFFFFF : 0xBDBDBD), 0);
    lv_obj_set_style_text_font(nr, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(nr, 14, 22);
    lv_obj_set_size(nr, 36, 24);
    lv_obj_set_style_text_align(nr, LV_TEXT_ALIGN_CENTER, 0);

    if (is_current) {
        make_eq_indicator(row, 58, 17);
    } else {
        lv_obj_t *icon = lv_label_create(row);
        lv_label_set_text(icon, LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
        lv_obj_set_pos(icon, 62, 20);
    }

    lv_obj_t *title = lv_label_create(row);
    lv_label_set_text(title, track->title);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(title, 106, 9);
    lv_obj_set_size(title, 500, 30);

    lv_obj_t *meta = lv_label_create(row);
    lv_label_set_text_fmt(meta, "%s%s%s",
                          track->artist,
                          track->album[0] ? " - " : "",
                          track->album);
    lv_obj_set_style_text_color(meta, lv_color_hex(0xCFCFCF), 0);
    lv_obj_set_style_text_font(meta, &lv_font_montserrat_18, 0);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(meta, 106, 41);
    lv_obj_set_size(meta, 500, 24);

    char tbuf[16];
    format_track_time(track->duration_sec, tbuf, sizeof(tbuf));
    lv_obj_t *dur = lv_label_create(row);
    lv_label_set_text(dur, tbuf);
    lv_obj_set_style_text_color(dur, lv_color_hex(0xDADADA), 0);
    lv_obj_set_style_text_font(dur, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(dur, 646, 23);
    lv_obj_set_size(dur, 70, 24);
    lv_obj_set_style_text_align(dur, LV_TEXT_ALIGN_RIGHT, 0);
}

static void set_loading_overlay(bool visible)
{
    if (!s_queue_overlay) return;
    if (visible) {
        lv_obj_clear_flag(s_queue_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_queue_overlay);
    } else {
        lv_obj_add_flag(s_queue_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_arrow_states(void)
{
    if (!s_arrow_left || !s_arrow_right) return;
    bool can_prev = s_view_start > 0;
    bool can_next = s_view_total > 0 && (s_view_start + SONOS_QUEUE_MAX_TRACKS) < s_view_total;
    lv_obj_set_style_text_color(s_arrow_left,
                                lv_color_hex(can_prev ? 0xD8D8D8 : 0x303434), 0);
    lv_obj_set_style_text_color(s_arrow_right,
                                lv_color_hex(can_next ? 0xD8D8D8 : 0x303434), 0);
}

static int current_queue_index(void)
{
    sonos_player_t snap = sonos_snapshot_active();
    if (!snap.online) return 0;
    return snap.queue_index;
}

static bool queue_scroll_to_current_track(int count)
{
    if (!s_queue_list || s_view_current_index <= 0 || count <= 0) return false;

    int row_index = s_view_current_index - s_view_start - 1;
    if (row_index < 0 || row_index >= count) return false;

    int list_h = lv_obj_get_height(s_queue_list);
    int target_y = row_index * QUEUE_ROW_STEP - (list_h - QUEUE_ROW_H) / 2;
    if (target_y < 0) target_y = 0;

    // Beim Oeffnen direkt zum laufenden Track springen, ohne Animation.
    lv_obj_scroll_to_y(s_queue_list, target_y, LV_ANIM_OFF);
    return true;
}

static void queue_rebuild_list(bool keep_scroll)
{
    char status[48];
    bool loading = false;
    uint32_t update_ms = 0;
    int start = 0;
    int total = 0;
    int count = sonos_queue_snapshot(s_view_tracks, SONOS_QUEUE_MAX_TRACKS,
                                     status, sizeof(status), &loading, &update_ms,
                                     &start, &total);
    s_view_start = start;
    s_view_total = total;
    s_view_current_index = current_queue_index();

    lv_label_set_text(s_queue_status, loading ? "Lade Trackliste ..." : status);
    update_arrow_states();
    int scroll_y = keep_scroll ? lv_obj_get_scroll_y(s_queue_list) : 0;
    s_pressed_row = NULL;
    s_press_moved = false;
    lv_obj_clean(s_queue_list);

    if (count <= 0) {
        lv_obj_t *empty = lv_label_create(s_queue_list);
        lv_label_set_text(empty, loading ? "Bitte warten" : "Keine Tracks in der Queue");
        lv_obj_set_style_text_color(empty, lv_color_hex(0xDADADA), 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_24, 0);
        lv_obj_set_pos(empty, 0, 120);
        lv_obj_set_size(empty, 752, 40);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        if (keep_scroll) lv_obj_scroll_to_y(s_queue_list, scroll_y, LV_ANIM_OFF);
        if (s_scroll_to_current_on_open && !loading) {
            s_scroll_to_current_on_open = false;
            s_scroll_to_current_start = -1;
        }
        return;
    }

    for (int i = 0; i < count && i < SONOS_QUEUE_MAX_TRACKS; i++) {
        int track_no = start + i + 1;
        bool is_current = (s_view_current_index > 0 && track_no == s_view_current_index);
        make_track_row(s_queue_list, i, track_no, &s_view_tracks[i], is_current);
    }
    if (s_scroll_to_current_on_open) {
        if (queue_scroll_to_current_track(count) || !loading) {
            s_scroll_to_current_on_open = false;
            s_scroll_to_current_start = -1;
        }
    } else {
        // Bei Track-Auswahl nur Markierung aktualisieren, ohne zur Listenoberkante zu springen.
        lv_obj_scroll_to_y(s_queue_list, keep_scroll ? scroll_y : 0, LV_ANIM_OFF);
    }
}

static void queue_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    char status[48];
    bool loading = false;
    uint32_t update_ms = 0;
    int start = 0;
    int total = 0;
    int count = sonos_queue_snapshot(NULL, 0, status, sizeof(status), &loading, &update_ms,
                                     &start, &total);
    int cur = current_queue_index();

    bool open_scroll_ready = s_scroll_to_current_on_open && start == s_scroll_to_current_start;
    if (count != s_seen_count || loading != s_seen_loading
        || update_ms != s_seen_update_ms || start != s_seen_start
        || cur != s_seen_current_index || open_scroll_ready) {
        bool keep_scroll = (start == s_seen_start);
        s_seen_count = count;
        s_seen_loading = loading;
        s_seen_update_ms = update_ms;
        s_seen_start = start;
        s_seen_current_index = cur;
        s_view_total = total;
        queue_rebuild_list(keep_scroll);
    }
    if (!loading) set_loading_overlay(false);
}

static void queue_load_page(int start_index)
{
    if (start_index < 0) start_index = 0;
    if (s_view_total > 0) {
        int max_start = s_view_total - SONOS_QUEUE_MAX_TRACKS;
        if (max_start < 0) max_start = 0;
        if (start_index > max_start) start_index = max_start;
    }
    if (start_index == s_view_start && start_index == s_seen_start) return;
    s_view_start = start_index;

    // Vorgeladene Seite -> sofort uebernehmen, kein Spinner.
    if (sonos_queue_apply_cached(start_index)) {
        set_loading_overlay(false);
        queue_timer_cb(NULL);
        // Naechste Nachbarn rechtzeitig nachschieben.
        sonos_queue_cmd(SONOS_CMD_QUEUE_PRELOAD, start_index - SONOS_QUEUE_MAX_TRACKS);
        sonos_queue_cmd(SONOS_CMD_QUEUE_PRELOAD, start_index + SONOS_QUEUE_MAX_TRACKS);
        return;
    }

    set_loading_overlay(true);
    sonos_queue_cmd(SONOS_CMD_QUEUE_REFRESH, start_index);
}

static void queue_gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) {
        // Wisch nach links = naechste Seite
        if (s_view_total <= 0 || s_view_start + SONOS_QUEUE_MAX_TRACKS >= s_view_total) return;
        lv_indev_wait_release(indev);
        queue_load_page(s_view_start + SONOS_QUEUE_MAX_TRACKS);
    } else if (dir == LV_DIR_RIGHT) {
        // Wisch nach rechts = vorherige Seite
        if (s_view_start <= 0) return;
        lv_indev_wait_release(indev);
        queue_load_page(s_view_start - SONOS_QUEUE_MAX_TRACKS);
    }
}

static void ui_queue_build(void)
{
    s_queue_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_queue_screen, lv_color_hex(0x050505), 0);
    lv_obj_set_style_bg_opa(s_queue_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_queue_screen, 0, 0);
    lv_obj_clear_flag(s_queue_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_queue_screen, queue_gesture_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_t *top = lv_obj_create(s_queue_screen);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_size(top, 800, 56);
    lv_obj_set_style_bg_color(top, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_radius(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    make_header_button(top, LV_SYMBOL_LEFT, 22, back_clicked_cb);

    lv_obj_t *title = lv_label_create(top);
    lv_label_set_text(title, "QUEUE");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_34, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 1);

    s_arrow_left = lv_label_create(s_queue_screen);
    lv_label_set_text(s_arrow_left, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(s_arrow_left, lv_color_hex(0x303434), 0);
    lv_obj_set_style_text_font(s_arrow_left, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(s_arrow_left, 26, 64);
    lv_obj_set_size(s_arrow_left, 32, 24);
    lv_obj_set_style_text_align(s_arrow_left, LV_TEXT_ALIGN_LEFT, 0);

    s_arrow_right = lv_label_create(s_queue_screen);
    lv_label_set_text(s_arrow_right, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(s_arrow_right, lv_color_hex(0x303434), 0);
    lv_obj_set_style_text_font(s_arrow_right, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(s_arrow_right, 742, 64);
    lv_obj_set_size(s_arrow_right, 32, 24);
    lv_obj_set_style_text_align(s_arrow_right, LV_TEXT_ALIGN_RIGHT, 0);

    s_queue_status = lv_label_create(s_queue_screen);
    lv_label_set_text(s_queue_status, "Noch nicht geladen");
    lv_obj_set_style_text_color(s_queue_status, lv_color_hex(0xD8D8D8), 0);
    lv_obj_set_style_text_font(s_queue_status, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(s_queue_status, 64, 62);
    lv_obj_set_size(s_queue_status, 672, 28);
    lv_obj_set_style_text_align(s_queue_status, LV_TEXT_ALIGN_CENTER, 0);

    s_queue_list = lv_obj_create(s_queue_screen);
    lv_obj_set_pos(s_queue_list, 24, 96);
    lv_obj_set_size(s_queue_list, 752, 380);
    lv_obj_set_style_bg_opa(s_queue_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_queue_list, 0, 0);
    lv_obj_set_style_pad_all(s_queue_list, 0, 0);
    lv_obj_set_scroll_dir(s_queue_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_queue_list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_event_cb(s_queue_list, queue_scroll_cb, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(s_queue_list, queue_scroll_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(s_queue_list, queue_scroll_cb, LV_EVENT_SCROLL_THROW_BEGIN, NULL);
    lv_obj_add_event_cb(s_queue_list, queue_scroll_cb, LV_EVENT_SCROLL_END, NULL);

    s_queue_overlay = lv_obj_create(s_queue_screen);
    lv_obj_set_pos(s_queue_overlay, 24, 96);
    lv_obj_set_size(s_queue_overlay, 752, 380);
    lv_obj_set_style_bg_color(s_queue_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_queue_overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_queue_overlay, 0, 0);
    lv_obj_set_style_radius(s_queue_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_queue_overlay, 0, 0);
    lv_obj_clear_flag(s_queue_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_queue_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *spinner = lv_spinner_create(s_queue_overlay);
    lv_spinner_set_anim_params(spinner, 1000, 60);
    lv_obj_set_size(spinner, 64, 64);
    lv_obj_center(spinner);
    lv_obj_set_style_arc_width(spinner, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x303434), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0xD8D8D8), LV_PART_INDICATOR);

    queue_rebuild_list(false);
    s_queue_timer = lv_timer_create(queue_timer_cb, QUEUE_TIMER_MS, NULL);
}

void ui_queue_show(void)
{
    if (!s_queue_screen) ui_queue_build();
    lv_screen_load(s_queue_screen);

    int cur = current_queue_index();
    int start = 0;
    if (cur > 0) {
        start = ((cur - 1) / SONOS_QUEUE_MAX_TRACKS) * SONOS_QUEUE_MAX_TRACKS;
    }
    s_view_start = start;
    s_scroll_to_current_on_open = cur > 0;
    s_scroll_to_current_start = start;
    set_loading_overlay(false);

    if (sonos_queue_apply_cached(start)) {
        // Idle-Preload hatte die Seite schon im Cache -> sofort rendern,
        // nur Nachbarseiten leise nachschieben.
        sonos_queue_cmd(SONOS_CMD_QUEUE_PRELOAD, start - SONOS_QUEUE_MAX_TRACKS);
        sonos_queue_cmd(SONOS_CMD_QUEUE_PRELOAD, start + SONOS_QUEUE_MAX_TRACKS);
    } else {
        sonos_queue_cmd(SONOS_CMD_QUEUE_REFRESH, start);
    }
    queue_timer_cb(s_queue_timer);
}
