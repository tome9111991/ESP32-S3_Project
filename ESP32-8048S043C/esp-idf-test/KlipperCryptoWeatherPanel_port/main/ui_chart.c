#include "app_state.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static btc_candle_t *s_chart_candles;

// Dirty-Cache: vermeidet das teure Pixel-Pixel-Rendering, wenn sich an den
// sichtbaren Daten nichts geaendert hat. Spart auf dem BTC-Screen ~50-100 ms
// pro Sekunde LVGL-Task-Last und damit verlorene Touches.
static struct {
    int      window_size;
    int      visible_count;
    uint32_t last_time;
    float    last_open;
    float    last_high;
    float    last_low;
    float    last_close;
    float    raw_low;
    float    raw_high;
    int      progress_width;
    bool     valid;
} s_chart_cache;

void ui_chart_invalidate(void)
{
    s_chart_cache.valid = false;
}

static int maxi(int a, int b) { return a > b ? a : b; }
static int mini(int a, int b) { return a < b ? a : b; }

static int price_to_chart_y(float price, float low, float high, int y, int h)
{
    if (high <= low) return y + (h / 2);
    float normalized = (price - low) / (high - low);
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    return y + h - 1 - (int)roundf(normalized * (float)(h - 1));
}

static void canvas_fill_rect(lv_obj_t *canvas, int x, int y, int w, int h, uint32_t color)
{
    if (!canvas || w <= 0 || h <= 0) return;
    int x2 = x + w - 1;
    int y2 = y + h - 1;
    if (x2 < 0 || y2 < 0 || x >= BTC_CHART_W || y >= BTC_CHART_CANVAS_H) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 >= BTC_CHART_W) x2 = BTC_CHART_W - 1;
    if (y2 >= BTC_CHART_CANVAS_H) y2 = BTC_CHART_CANVAS_H - 1;

    lv_color_t lv_color = lv_color_hex(color);
    for (int yy = y; yy <= y2; yy++) {
        for (int xx = x; xx <= x2; xx++) {
            lv_canvas_set_px(canvas, xx, yy, lv_color, LV_OPA_COVER);
        }
    }
}

static void canvas_hline(lv_obj_t *canvas, int x, int y, int w, uint32_t color)
{
    canvas_fill_rect(canvas, x, y, w, 1, color);
}

static void canvas_vline(lv_obj_t *canvas, int x, int y, int h, uint32_t color)
{
    canvas_fill_rect(canvas, x, y, 1, h, color);
}

static void canvas_rect(lv_obj_t *canvas, int x, int y, int w, int h, uint32_t color)
{
    canvas_hline(canvas, x, y, w, color);
    canvas_hline(canvas, x, y + h - 1, w, color);
    canvas_vline(canvas, x, y, h, color);
    canvas_vline(canvas, x + w - 1, y, h, color);
}

static void canvas_dashed_hline(lv_obj_t *canvas, int y, int x_from, int x_to,
                                int dash_on, int dash_off, uint32_t color)
{
    if (dash_on <= 0) return;
    if (dash_off < 0) dash_off = 0;
    int x = x_from;
    while (x < x_to) {
        int seg_end = x + dash_on;
        if (seg_end > x_to) seg_end = x_to;
        canvas_hline(canvas, x, y, seg_end - x, color);
        x = seg_end + dash_off;
    }
}

static int compute_progress_width(const btc_candle_t *last)
{
    time_t now_time = time(NULL);
    uint32_t candle_seconds = crypto_chart_granularity_seconds();
    if (now_time <= 100000 || !last || last->time == 0 || candle_seconds == 0) return 0;
    int elapsed = (int)(now_time - last->time);
    if (elapsed < 0) elapsed = 0;
    if (elapsed > (int)candle_seconds) elapsed = (int)candle_seconds;
    return (BTC_CHART_W * elapsed) / (int)candle_seconds;
}

static void draw_progress_bar(lv_obj_t *canvas, int progress_width)
{
    canvas_fill_rect(canvas, 0, BTC_CHART_PROGRESS_Y, BTC_CHART_W, BTC_CHART_PROGRESS_H, 0x1d2530);
    if (progress_width > 0) {
        canvas_fill_rect(canvas, 0, BTC_CHART_PROGRESS_Y, progress_width, BTC_CHART_PROGRESS_H, COLOR_BTC);
    }
}

void ui_chart_draw(void)
{
    if (ui_current_screen() != SCREEN_BTC_DAY) return;

    lv_obj_t *canvas = ui_get_btc_chart_canvas();
    if (!canvas) return;

    if (!s_chart_candles) {
        s_chart_candles = heap_caps_calloc(BTC_CANDLE_CAPACITY, sizeof(btc_candle_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_chart_candles) {
            s_chart_candles = heap_caps_calloc(BTC_CANDLE_CAPACITY, sizeof(btc_candle_t),
                                               MALLOC_CAP_8BIT);
        }
    }
    if (!s_chart_candles) return;

    int source_count = 0;
    bool ready = false;
    app_lock();
    ready = g_app.btc_day_data_ready;
    source_count = g_app.candle_count;
    if (source_count > BTC_CANDLE_CAPACITY) source_count = BTC_CANDLE_CAPACITY;
    if (g_app.candles) {
        memcpy(s_chart_candles, g_app.candles, sizeof(btc_candle_t) * source_count);
    } else {
        source_count = 0;
    }
    app_unlock();

    if (!ready || source_count < 2) {
        lv_canvas_fill_bg(canvas, lv_color_hex(COLOR_BG), LV_OPA_COVER);
        canvas_rect(canvas, 0, 0, BTC_CHART_W, BTC_CHART_H, COLOR_DIM);
        lv_obj_add_flag(ui_get_btc_price_last_label(), LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui_get_btc_price_high_label(), "--");
        lv_label_set_text(ui_get_btc_price_low_label(), "--");
        lv_obj_invalidate(canvas);
        s_chart_cache.valid = false;
        return;
    }

    int window_size = crypto_chart_candle_count();
    int start = source_count - window_size;
    if (start < 0) start = 0;
    int count = source_count - start;

    float low = s_chart_candles[start].low;
    float high = s_chart_candles[start].high;
    for (int i = start; i < source_count; i++) {
        if (s_chart_candles[i].low < low) low = s_chart_candles[i].low;
        if (s_chart_candles[i].high > high) high = s_chart_candles[i].high;
    }
    float raw_low = low;
    float raw_high = high;
    float padding = (high - low) * 0.08f;
    if (padding < 1.0f) padding = 1.0f;
    low -= padding;
    high += padding;

    const btc_candle_t *last_candle = &s_chart_candles[source_count - 1];
    int progress_width = compute_progress_width(last_candle);

    bool data_unchanged = s_chart_cache.valid
        && s_chart_cache.window_size   == window_size
        && s_chart_cache.visible_count == count
        && s_chart_cache.last_time     == last_candle->time
        && s_chart_cache.last_open     == last_candle->open
        && s_chart_cache.last_high     == last_candle->high
        && s_chart_cache.last_low      == last_candle->low
        && s_chart_cache.last_close    == last_candle->close
        && s_chart_cache.raw_low       == raw_low
        && s_chart_cache.raw_high      == raw_high;

    if (data_unchanged) {
        if (s_chart_cache.progress_width == progress_width) return;
        draw_progress_bar(canvas, progress_width);
        s_chart_cache.progress_width = progress_width;
        lv_obj_invalidate(canvas);
        return;
    }

    lv_canvas_fill_bg(canvas, lv_color_hex(COLOR_BG), LV_OPA_COVER);
    canvas_rect(canvas, 0, 0, BTC_CHART_W, BTC_CHART_H, COLOR_DIM);

    for (int i = 1; i < 4; i++) {
        canvas_hline(canvas, 1, BTC_CHART_H * i / 4, BTC_CHART_W - 2, 0x1d2530);
        canvas_vline(canvas, BTC_CHART_W * i / 4, 1, BTC_CHART_H - 2, 0x1d2530);
    }

    int candle_step = maxi(1, (BTC_CHART_W - 5) / maxi(1, count - 1));
    int body_width = candle_step >= 6 ? 5 : (candle_step >= 4 ? 3 : 1);

    for (int i = start; i < source_count; i++) {
        const btc_candle_t *c = &s_chart_candles[i];
        int relative = i - start;
        int x = 2 + ((relative * (BTC_CHART_W - 5)) / maxi(1, count - 1));
        int y_high = price_to_chart_y(c->high, low, high, 3, BTC_CHART_H - 6);
        int y_low = price_to_chart_y(c->low, low, high, 3, BTC_CHART_H - 6);
        int y_open = price_to_chart_y(c->open, low, high, 3, BTC_CHART_H - 6);
        int y_close = price_to_chart_y(c->close, low, high, 3, BTC_CHART_H - 6);
        int body_top = mini(y_open, y_close);
        int body_height = maxi(1, abs(y_close - y_open) + 1);
        int body_x = maxi(1, x - (body_width / 2));
        if (body_x + body_width >= BTC_CHART_W) body_x = BTC_CHART_W - body_width - 1;
        uint32_t candle_color = c->close >= c->open ? COLOR_GREEN : COLOR_LOSS;

        canvas_vline(canvas, x, y_high, maxi(1, y_low - y_high + 1), candle_color);
        canvas_fill_rect(canvas, body_x, body_top, body_width, body_height, candle_color);
        if (i == source_count - 1) {
            canvas_rect(canvas, maxi(1, body_x - 1), maxi(1, body_top - 1),
                        body_width + 2, body_height + 2, COLOR_BTC);
        }
    }

    int y_last_close = price_to_chart_y(last_candle->close, low, high, 3, BTC_CHART_H - 6);
    canvas_dashed_hline(canvas, y_last_close, 1, BTC_CHART_W - 1, 6, 4, COLOR_BTC);

    char high_text[16], low_text[16], last_text[16];
    format_quote_compact(raw_high, high_text, sizeof(high_text));
    format_quote_compact(raw_low, low_text, sizeof(low_text));
    format_quote_compact(last_candle->close, last_text, sizeof(last_text));
    lv_label_set_text(ui_get_btc_price_high_label(), high_text);
    lv_label_set_text(ui_get_btc_price_low_label(), low_text);
    lv_label_set_text(ui_get_btc_price_last_label(), last_text);

    lv_obj_t *last_label = ui_get_btc_price_last_label();
    int chart_x = lv_obj_get_x(canvas);
    int chart_y = lv_obj_get_y(canvas);
    const int label_w = 96;
    const int label_h = 22;
    int label_x = chart_x + (BTC_CHART_W - label_w) / 2;
    int label_y = chart_y + y_last_close - label_h / 2;
    int min_y = chart_y + 2;
    int max_y = chart_y + BTC_CHART_H - label_h - 2;
    if (label_y < min_y) label_y = min_y;
    if (label_y > max_y) label_y = max_y;
    lv_obj_set_pos(last_label, label_x, label_y);
    lv_obj_remove_flag(last_label, LV_OBJ_FLAG_HIDDEN);

    draw_progress_bar(canvas, progress_width);
    lv_obj_invalidate(canvas);

    s_chart_cache.window_size    = window_size;
    s_chart_cache.visible_count  = count;
    s_chart_cache.last_time      = last_candle->time;
    s_chart_cache.last_open      = last_candle->open;
    s_chart_cache.last_high      = last_candle->high;
    s_chart_cache.last_low       = last_candle->low;
    s_chart_cache.last_close     = last_candle->close;
    s_chart_cache.raw_low        = raw_low;
    s_chart_cache.raw_high       = raw_high;
    s_chart_cache.progress_width = progress_width;
    s_chart_cache.valid          = true;
}
