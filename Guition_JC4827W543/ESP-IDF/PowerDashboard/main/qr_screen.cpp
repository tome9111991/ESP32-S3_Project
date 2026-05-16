// Provisioning-Screen: zwei QR-Codes (WLAN-Connect + Setup-URL) + Klartext.

#include "qr_screen.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "qrcode.h"
#include "lvgl.h"

static const char *TAG = "qr_screen";

// =============================================================================
// QR-Rendering in LVGL-Canvas
// =============================================================================

static lv_obj_t *s_status_lbl = nullptr;

// Display 480x272. Layout:
//   Header (32 px)
//   QR-Bereich 0..200 px hoch, 2x QR mittig
//   Untertitel je QR
//   Status-Footer
struct QrTarget {
    lv_obj_t *canvas;
    int       size_px;     // Kantenlaenge des Canvas in Pixel
    uint16_t *buf;         // RGB565 Buffer (size_px*size_px Pixel)
};

static void qr_render(QrTarget *t, esp_qrcode_handle_t qrcode)
{
    int mods = esp_qrcode_get_size(qrcode);
    if (mods <= 0) return;
    int total = mods + 2;  // 1 Modul Rand auf jeder Seite
    int scale = t->size_px / total;
    if (scale < 1) scale = 1;
    int border_px = (t->size_px - scale * mods) / 2;

    // Direkt in den Buffer schreiben - keine 3000 LVGL-Draw-Tasks.
    const uint16_t WHITE = 0xFFFF;
    const uint16_t BLACK = 0x0000;
    const int W = t->size_px;
    for (int i = 0; i < W * W; ++i) t->buf[i] = WHITE;

    for (int my = 0; my < mods; ++my) {
        for (int mx = 0; mx < mods; ++mx) {
            if (!esp_qrcode_get_module(qrcode, mx, my)) continue;
            int px0 = border_px + mx * scale;
            int py0 = border_px + my * scale;
            for (int dy = 0; dy < scale; ++dy) {
                uint16_t *row = &t->buf[(py0 + dy) * W + px0];
                for (int dx = 0; dx < scale; ++dx) row[dx] = BLACK;
            }
        }
    }
    lv_obj_invalidate(t->canvas);
}

// Trampolin: esp_qrcode_generate ruft display_func mit dem Handle.
// Wir muessen den Zielcanvas ueber globalen Slot weiterreichen.
static QrTarget *s_active_target = nullptr;
static void qr_display_cb(esp_qrcode_handle_t qrcode)
{
    if (s_active_target) qr_render(s_active_target, qrcode);
}

static void render_qr(QrTarget *t, const char *text)
{
    s_active_target = t;
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = qr_display_cb;
    cfg.max_qrcode_version = 10;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    esp_err_t err = esp_qrcode_generate(&cfg, text);
    s_active_target = nullptr;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "QR generate fehlgeschlagen (%d) fuer Text-Laenge %u",
                 err, (unsigned)strlen(text));
        lv_canvas_fill_bg(t->canvas, lv_color_hex(0x402020), LV_OPA_COVER);
    }
}

// =============================================================================
// Screen aufbauen
// =============================================================================

static QrTarget make_qr(lv_obj_t *parent, int size_px, int x, int y)
{
    QrTarget t = {};
    t.size_px = size_px;
    size_t bytes = (size_t)size_px * size_px * 2;
    t.buf = (uint16_t *)heap_caps_malloc(bytes,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!t.buf) {
        ESP_LOGE(TAG, "QR-Canvas-Buffer alloc fehlgeschlagen (%u bytes)",
                 (unsigned)bytes);
        return t;
    }
    t.canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(t.canvas, t.buf, size_px, size_px,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(t.canvas, x, y);
    return t;
}

extern "C" void qr_screen_show(const char *ap_ssid, const char *ap_pass,
                               const char *setup_url)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x071018), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    // Header
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Setup - PowerDashboard");
    lv_obj_set_style_text_color(title, lv_color_hex(0x2EE59D), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 8);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "QR scannen - sonst Browser mit URL oeffnen.");
    lv_obj_set_style_text_color(sub, lv_color_hex(0xA9B4C0), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 14, 36);

    // Grosser zentrierter QR links
    const int QR_PX = 220;
    const int QR_X  = 14;
    const int QR_Y  = 60 - 4;          // unter dem Subtitel
    QrTarget qr_wifi = make_qr(scr, QR_PX, QR_X, QR_Y);
    if (qr_wifi.canvas) {
        char buf[160];
        // WIFI-QR-Format. Sonderzeichen \, ;, , und " muessen escaped sein.
        // SSID/Pass sind hier nur ASCII-Alnum, keine Escape-Notwendigkeit.
        snprintf(buf, sizeof(buf), "WIFI:T:WPA;S:%s;P:%s;;",
                 ap_ssid ? ap_ssid : "", ap_pass ? ap_pass : "");
        render_qr(&qr_wifi, buf);
    }

    // Rechts: Klartext-Fallback fuer manuellen Connect
    const int TEXT_X = QR_X + QR_PX + 24;
    int y = QR_Y + 4;

    auto add_line = [&](const char *label, const char *value,
                        const lv_font_t *font, uint32_t color) {
        lv_obj_t *l = lv_label_create(scr);
        lv_label_set_text(l, label);
        lv_obj_set_style_text_color(l, lv_color_hex(0x7a8793), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(l, TEXT_X, y);
        y += 18;
        lv_obj_t *v = lv_label_create(scr);
        lv_label_set_text(v, value);
        lv_obj_set_style_text_color(v, lv_color_hex(color), 0);
        lv_obj_set_style_text_font(v, font, 0);
        lv_obj_set_pos(v, TEXT_X, y);
        y += 24;
    };

    add_line("WLAN",    ap_ssid  ? ap_ssid  : "?",
            &lv_font_montserrat_20, 0xFFFFFF);
    add_line("Passwort", ap_pass ? ap_pass : "?",
            &lv_font_montserrat_20, 0xFFC857);
    add_line("Manuell",  setup_url ? setup_url : "http://192.168.4.1/",
            &lv_font_montserrat_16, 0xA9B4C0);

    s_status_lbl = lv_label_create(scr);
    lv_label_set_text(s_status_lbl, "Warte auf Verbindung...");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x7a8793), 0);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_status_lbl, 210);
    lv_label_set_long_mode(s_status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_status_lbl, TEXT_X, y + 2);
}

extern "C" void qr_screen_set_status(const char *text)
{
    if (s_status_lbl && text) lv_label_set_text(s_status_lbl, text);
}
