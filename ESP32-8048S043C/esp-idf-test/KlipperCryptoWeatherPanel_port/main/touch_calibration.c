// Touch-Kalibrierung fuer GT911: UI, NVS-Persistenz und affine Rohwert-Map.
#include "app_state.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "touch_cal";

#define TOUCH_CAL_NVS_NS       "touch"
#define TOUCH_CAL_NVS_KEY      "cal"
#define TOUCH_CAL_MAGIC        0x5443414cU
#define TOUCH_CAL_VERSION      1U
#define TOUCH_CAL_TARGET_COUNT 5
#define TOUCH_CAL_ABORT_MS     2500
#define TOUCH_CAL_BAR_DELAY_MS 600
#define TOUCH_CAL_RELEASE_MS   70
#define TOUCH_CAL_MIN_SAMPLES  1
#define TOUCH_CAL_MAX_ERR_PX   80.0
#define TOUCH_CAL_POLL_MS      25
#define TOUCH_CAL_RAW_FRESH_MS 90
#define TOUCH_CAL_BAR_W        480
#define TOUCH_CAL_BAR_H        12

typedef struct {
    int x;
    int y;
} touch_cal_target_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    float x_rx;
    float x_ry;
    float x_c;
    float y_rx;
    float y_ry;
    float y_c;
} touch_cal_blob_t;

static const touch_cal_target_t s_targets[TOUCH_CAL_TARGET_COUNT] = {
    {60, 60},
    {LCD_H_RES - 60, 60},
    {60, LCD_V_RES - 60},
    {LCD_H_RES - 60, LCD_V_RES - 60},
    {LCD_H_RES / 2, LCD_V_RES / 2},
};

static touch_cal_blob_t s_cal = {
    .magic = TOUCH_CAL_MAGIC,
    .version = TOUCH_CAL_VERSION,
    .x_rx =  1.65867031f,
    .x_ry = -0.02261823f,
    .x_c  =  2.12817001f,
    .y_rx =  0.02082564f,
    .y_ry =  1.79517055f,
    .y_c  = 10.62223816f,
};

static esp_lcd_touch_handle_t s_touch = NULL;
static bool s_rotate_180 = DISPLAY_ROTATE_180_DEFAULT != 0;
static bool s_raw_seen = false;
static int16_t s_raw_x = 0;
static int16_t s_raw_y = 0;
static int64_t s_raw_seen_us = 0;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_progress_label = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_target_marker = NULL;
static lv_obj_t *s_abort_bar = NULL;
static lv_obj_t *s_abort_fill = NULL;
static lv_timer_t *s_timer = NULL;

static int s_step = 0;
static int16_t s_avg_x[TOUCH_CAL_TARGET_COUNT];
static int16_t s_avg_y[TOUCH_CAL_TARGET_COUNT];
static bool s_pressed = false;
static int64_t s_press_start_us = 0;
static int64_t s_last_seen_us = 0;
static int64_t s_auto_close_us = 0;
static int32_t s_sum_x = 0;
static int32_t s_sum_y = 0;
static int s_sample_count = 0;

static int clamp_int_local(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static bool cal_values_valid(const touch_cal_blob_t *cal)
{
    return cal &&
           cal->magic == TOUCH_CAL_MAGIC &&
           cal->version == TOUCH_CAL_VERSION &&
           isfinite(cal->x_rx) && isfinite(cal->x_ry) && isfinite(cal->x_c) &&
           isfinite(cal->y_rx) && isfinite(cal->y_ry) && isfinite(cal->y_c);
}

void touch_calibration_set_rotation(bool rotate_180)
{
    s_rotate_180 = rotate_180;
}

void touch_calibration_set_handle(esp_lcd_touch_handle_t touch)
{
    s_touch = touch;
}

bool touch_calibration_is_open(void)
{
    return s_screen != NULL;
}

bool touch_calibration_load(void)
{
    nvs_handle_t h;
    if (nvs_open(TOUCH_CAL_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "keine NVS-Kalibrierung, Default aktiv");
        return false;
    }

    touch_cal_blob_t loaded = {0};
    size_t size = sizeof(loaded);
    esp_err_t err = nvs_get_blob(h, TOUCH_CAL_NVS_KEY, &loaded, &size);
    nvs_close(h);
    if (err != ESP_OK || size != sizeof(loaded) || !cal_values_valid(&loaded)) {
        ESP_LOGW(TAG, "NVS-Kalibrierung ungueltig: %s", esp_err_to_name(err));
        return false;
    }

    s_cal = loaded;
    ESP_LOGI(TAG, "Kalibrierung geladen: X=(%.5f,%.5f,%.3f) Y=(%.5f,%.5f,%.3f)",
             s_cal.x_rx, s_cal.x_ry, s_cal.x_c,
             s_cal.y_rx, s_cal.y_ry, s_cal.y_c);
    return true;
}

static bool touch_calibration_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(TOUCH_CAL_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open RW: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(h, TOUCH_CAL_NVS_KEY, &s_cal, sizeof(s_cal));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS speichern fehlgeschlagen: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void touch_calibration_process_coords(esp_lcd_touch_handle_t tp,
                                      uint16_t *x, uint16_t *y, uint16_t *strength,
                                      uint8_t *point_num, uint8_t max_point_num)
{
    (void)tp;
    (void)strength;
    uint8_t n = *point_num;
    if (n > max_point_num) n = max_point_num;
    *point_num = n;

    if (n > 0) {
        // Rohwert merken, bevor die affine Map die Koordinaten fuer LVGL ersetzt.
        // LVGL liest den GT911 ebenfalls; dieser Cache verhindert verpasste Taps.
        s_raw_x = (int16_t)x[0];
        s_raw_y = (int16_t)y[0];
        s_raw_seen_us = esp_timer_get_time();
        s_raw_seen = true;
    }

    for (uint8_t i = 0; i < n; i++) {
        const float rx = (float)x[i];
        const float ry = (float)y[i];
        int sx = (int)(s_cal.x_rx * rx + s_cal.x_ry * ry + s_cal.x_c + 0.5f);
        int sy = (int)(s_cal.y_rx * rx + s_cal.y_ry * ry + s_cal.y_c + 0.5f);
        x[i] = (uint16_t)clamp_int_local(sx, 0, LCD_H_RES - 1);
        y[i] = (uint16_t)clamp_int_local(sy, 0, LCD_V_RES - 1);
    }
}

static void style_filled_rect(lv_obj_t *obj, uint32_t color, int radius)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                            lv_text_align_t align, int x, int y, int w, int h,
                            const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_size(label, w, h);
    lv_obj_set_pos(label, x, y);
    lv_label_set_text(label, text);
    return label;
}

static void destroy_screen(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_screen) {
        lv_obj_delete_async(s_screen);
        s_screen = NULL;
    }
    s_progress_label = NULL;
    s_status_label = NULL;
    s_target_marker = NULL;
    s_abort_bar = NULL;
    s_abort_fill = NULL;
}

static void update_abort_bar(int64_t held_ms)
{
    if (!s_abort_bar || !s_abort_fill) return;
    if (held_ms < TOUCH_CAL_BAR_DELAY_MS) {
        lv_obj_add_flag(s_abort_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(s_abort_fill, 0);
        return;
    }
    if (held_ms > TOUCH_CAL_ABORT_MS) held_ms = TOUCH_CAL_ABORT_MS;
    lv_obj_remove_flag(s_abort_bar, LV_OBJ_FLAG_HIDDEN);
    const int64_t span = TOUCH_CAL_ABORT_MS - TOUCH_CAL_BAR_DELAY_MS;
    const int64_t progress = held_ms - TOUCH_CAL_BAR_DELAY_MS;
    const int width = (int)((progress * (TOUCH_CAL_BAR_W - 4)) / span);
    lv_obj_set_width(s_abort_fill, width);
}

static void set_target_visible(int step)
{
    if (!s_target_marker) return;
    if (step < 0 || step >= TOUCH_CAL_TARGET_COUNT) {
        lv_obj_add_flag(s_target_marker, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const int marker_size = 50;
    lv_obj_remove_flag(s_target_marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_target_marker,
                   s_targets[step].x - marker_size / 2,
                   s_targets[step].y - marker_size / 2);
}

static void update_progress(void)
{
    if (!s_progress_label) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "Punkt %d / %d", s_step + 1, TOUCH_CAL_TARGET_COUNT);
    lv_label_set_text(s_progress_label, buf);
}

static void set_status(const char *text, uint32_t color)
{
    if (!s_status_label) return;
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(color), 0);
    lv_label_set_text(s_status_label, text);
}

static void create_screen(void)
{
    destroy_screen();

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    make_label(s_screen, &lv_font_montserrat_40, COLOR_TEXT, LV_TEXT_ALIGN_CENTER,
               0, 140, LCD_H_RES, 52, "Touch kalibrieren");

    s_progress_label = make_label(s_screen, &lv_font_montserrat_30, COLOR_CYAN,
                                  LV_TEXT_ALIGN_CENTER, 0, 200, LCD_H_RES, 40, "");

    make_label(s_screen, &lv_font_montserrat_24, COLOR_MUTED, LV_TEXT_ALIGN_CENTER,
               0, 250, LCD_H_RES, 32, "Punkt antippen. Halten zum Abbrechen.");

    s_status_label = make_label(s_screen, &lv_font_montserrat_24, COLOR_MUTED,
                                LV_TEXT_ALIGN_CENTER, 0, 290, LCD_H_RES, 32, "");

    s_abort_bar = lv_obj_create(s_screen);
    style_filled_rect(s_abort_bar, 0x232b38, TOUCH_CAL_BAR_H / 2);
    lv_obj_set_size(s_abort_bar, TOUCH_CAL_BAR_W, TOUCH_CAL_BAR_H);
    lv_obj_set_pos(s_abort_bar, (LCD_H_RES - TOUCH_CAL_BAR_W) / 2, 336);
    lv_obj_set_style_border_color(s_abort_bar, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_border_width(s_abort_bar, 2, 0);
    lv_obj_set_style_pad_all(s_abort_bar, 0, 0);
    lv_obj_remove_flag(s_abort_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_abort_bar, LV_OBJ_FLAG_HIDDEN);

    s_abort_fill = lv_obj_create(s_abort_bar);
    style_filled_rect(s_abort_fill, COLOR_LOSS, (TOUCH_CAL_BAR_H - 4) / 2);
    lv_obj_set_size(s_abort_fill, 0, TOUCH_CAL_BAR_H - 4);
    lv_obj_set_pos(s_abort_fill, 0, 0);
    lv_obj_set_style_border_width(s_abort_fill, 0, 0);
    lv_obj_set_style_pad_all(s_abort_fill, 0, 0);
    lv_obj_remove_flag(s_abort_fill, LV_OBJ_FLAG_CLICKABLE);

    s_target_marker = lv_obj_create(s_screen);
    style_filled_rect(s_target_marker, COLOR_CYAN, LV_RADIUS_CIRCLE);
    lv_obj_set_size(s_target_marker, 50, 50);
    lv_obj_set_style_border_color(s_target_marker, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_border_width(s_target_marker, 3, 0);
    lv_obj_set_style_pad_all(s_target_marker, 0, 0);
    lv_obj_remove_flag(s_target_marker, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *dot = lv_obj_create(s_target_marker);
    style_filled_rect(dot, COLOR_BG, LV_RADIUS_CIRCLE);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_center(dot);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
}

static void reset_state(void)
{
    s_step = 0;
    s_sum_x = 0;
    s_sum_y = 0;
    s_sample_count = 0;
    s_pressed = false;
    s_press_start_us = 0;
    s_last_seen_us = 0;
    s_auto_close_us = 0;
    memset(s_avg_x, 0, sizeof(s_avg_x));
    memset(s_avg_y, 0, sizeof(s_avg_y));
}

static void return_to_settings(void)
{
    // Erst Settings-Menue laden, dann den aktiven Kalibrier-Screen loeschen.
    ui_settings_menu_reopen();
    destroy_screen();
}

static bool solve3x3(const double m[3][3], const double rhs[3], double out[3])
{
    const double a = m[0][0], b = m[0][1], c = m[0][2];
    const double d = m[1][0], e = m[1][1], f = m[1][2];
    const double g = m[2][0], h = m[2][1], i = m[2][2];
    const double det = a * (e * i - f * h) -
                       b * (d * i - f * g) +
                       c * (d * h - e * g);
    if (fabs(det) < 1e-6) return false;

    out[0] = ((e * i - f * h) * rhs[0] + (c * h - b * i) * rhs[1] + (b * f - c * e) * rhs[2]) / det;
    out[1] = ((f * g - d * i) * rhs[0] + (a * i - c * g) * rhs[1] + (c * d - a * f) * rhs[2]) / det;
    out[2] = ((d * h - e * g) * rhs[0] + (b * g - a * h) * rhs[1] + (a * e - b * d) * rhs[2]) / det;
    return true;
}

static void expected_from_target(int index, double *ex, double *ey)
{
    // Bei LVGL-180-Rotation erwartet der Touch-Treiber Panel-Koordinaten;
    // LVGL spiegelt sie danach in den sichtbaren User-Space zurueck.
    if (s_rotate_180) {
        *ex = (double)((LCD_H_RES - 1) - s_targets[index].x);
        *ey = (double)((LCD_V_RES - 1) - s_targets[index].y);
    } else {
        *ex = (double)s_targets[index].x;
        *ey = (double)s_targets[index].y;
    }
}

static bool fit_calibration(touch_cal_blob_t *out)
{
    double sxx = 0.0, sxy = 0.0, sx = 0.0, syy = 0.0, sy = 0.0;
    double x_exp_rx = 0.0, x_exp_ry = 0.0, x_exp = 0.0;
    double y_exp_rx = 0.0, y_exp_ry = 0.0, y_exp = 0.0;

    for (int i = 0; i < TOUCH_CAL_TARGET_COUNT; i++) {
        const double rx = (double)s_avg_x[i];
        const double ry = (double)s_avg_y[i];
        double ex = 0.0, ey = 0.0;
        expected_from_target(i, &ex, &ey);

        sxx += rx * rx;
        sxy += rx * ry;
        sx  += rx;
        syy += ry * ry;
        sy  += ry;
        x_exp_rx += ex * rx;
        x_exp_ry += ex * ry;
        x_exp    += ex;
        y_exp_rx += ey * rx;
        y_exp_ry += ey * ry;
        y_exp    += ey;
    }

    const double m[3][3] = {
        {sxx, sxy, sx},
        {sxy, syy, sy},
        {sx,  sy,  (double)TOUCH_CAL_TARGET_COUNT},
    };
    const double rhs_x[3] = {x_exp_rx, x_exp_ry, x_exp};
    const double rhs_y[3] = {y_exp_rx, y_exp_ry, y_exp};
    double x_res[3] = {0};
    double y_res[3] = {0};
    if (!solve3x3(m, rhs_x, x_res) || !solve3x3(m, rhs_y, y_res)) return false;

    double max_err = 0.0;
    for (int i = 0; i < TOUCH_CAL_TARGET_COUNT; i++) {
        double ex = 0.0, ey = 0.0;
        expected_from_target(i, &ex, &ey);
        const double mx = x_res[0] * s_avg_x[i] + x_res[1] * s_avg_y[i] + x_res[2];
        const double my = y_res[0] * s_avg_x[i] + y_res[1] * s_avg_y[i] + y_res[2];
        const double dx = mx - ex;
        const double dy = my - ey;
        const double err = sqrt(dx * dx + dy * dy);
        if (err > max_err) max_err = err;
    }
    if (max_err > TOUCH_CAL_MAX_ERR_PX) {
        ESP_LOGW(TAG, "Restfehler %.1f px > %.1f px", max_err, TOUCH_CAL_MAX_ERR_PX);
        return false;
    }

    *out = (touch_cal_blob_t) {
        .magic = TOUCH_CAL_MAGIC,
        .version = TOUCH_CAL_VERSION,
        .x_rx = (float)x_res[0],
        .x_ry = (float)x_res[1],
        .x_c  = (float)x_res[2],
        .y_rx = (float)y_res[0],
        .y_ry = (float)y_res[1],
        .y_c  = (float)y_res[2],
    };
    return true;
}

static void finish_calibration(void)
{
    touch_cal_blob_t fitted = {0};
    set_target_visible(-1);

    if (!fit_calibration(&fitted)) {
        set_status("Kalibrierung fehlgeschlagen, bitte erneut versuchen", COLOR_LOSS);
        s_auto_close_us = esp_timer_get_time() + 2500LL * 1000LL;
        return;
    }

    s_cal = fitted;
    ESP_LOGI(TAG, "Neue Kalibrierung: X=(%.5f,%.5f,%.3f) Y=(%.5f,%.5f,%.3f)",
             s_cal.x_rx, s_cal.x_ry, s_cal.x_c,
             s_cal.y_rx, s_cal.y_ry, s_cal.y_c);

    const bool saved = touch_calibration_save();
    set_status(saved ? "Kalibrierung gespeichert" : "Speichern fehlgeschlagen",
               saved ? COLOR_GREEN : COLOR_LOSS);
    s_auto_close_us = esp_timer_get_time() + 1500LL * 1000LL;
}

static bool read_raw_touch(int16_t *raw_x, int16_t *raw_y)
{
    if (!s_touch) return false;

    const int64_t now_us = esp_timer_get_time();
    if (s_raw_seen && (now_us - s_raw_seen_us) / 1000 <= TOUCH_CAL_RAW_FRESH_MS) {
        *raw_x = s_raw_x;
        *raw_y = s_raw_y;
        return true;
    }

    esp_lcd_touch_point_data_t pts[1];
    uint8_t count = 0;
    s_raw_seen = false;
    esp_lcd_touch_read_data(s_touch);
    const bool down = esp_lcd_touch_get_data(s_touch, pts, &count, 1) == ESP_OK && count > 0;
    if (!down || !s_raw_seen) return false;

    *raw_x = s_raw_x;
    *raw_y = s_raw_y;
    return true;
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    const int64_t now_us = esp_timer_get_time();

    if (s_auto_close_us != 0) {
        if (now_us >= s_auto_close_us) return_to_settings();
        return;
    }

    int16_t raw_x = 0;
    int16_t raw_y = 0;
    if (read_raw_touch(&raw_x, &raw_y)) {
        if (!s_pressed) {
            s_pressed = true;
            s_press_start_us = now_us;
            s_sum_x = 0;
            s_sum_y = 0;
            s_sample_count = 0;
            set_status("Halten...", COLOR_MUTED);
        }
        s_sum_x += raw_x;
        s_sum_y += raw_y;
        s_sample_count++;
        s_last_seen_us = now_us;

        const int64_t held_ms = (now_us - s_press_start_us) / 1000;
        update_abort_bar(held_ms);
        if (held_ms >= TOUCH_CAL_ABORT_MS) {
            set_target_visible(-1);
            update_abort_bar(0);
            set_status("Kalibrierung abgebrochen", COLOR_LOSS);
            s_auto_close_us = now_us + 1200LL * 1000LL;
            s_pressed = false;
        }
        return;
    }

    if (s_pressed && (now_us - s_last_seen_us) / 1000 >= TOUCH_CAL_RELEASE_MS) {
        s_pressed = false;
        update_abort_bar(0);
        if (s_sample_count >= TOUCH_CAL_MIN_SAMPLES) {
            s_avg_x[s_step] = (int16_t)(s_sum_x / s_sample_count);
            s_avg_y[s_step] = (int16_t)(s_sum_y / s_sample_count);
            s_step++;
            if (s_step >= TOUCH_CAL_TARGET_COUNT) {
                finish_calibration();
            } else {
                update_progress();
                set_target_visible(s_step);
                set_status("", COLOR_MUTED);
            }
        } else {
            set_status("Zu kurz, bitte erneut tippen", COLOR_LOSS);
        }
    }
}

void ui_touch_calibration_open(void)
{
    create_screen();
    reset_state();
    update_progress();
    set_target_visible(0);
    set_status("", COLOR_MUTED);
    lv_screen_load(s_screen);
    s_timer = lv_timer_create(timer_cb, TOUCH_CAL_POLL_MS, NULL);
}
