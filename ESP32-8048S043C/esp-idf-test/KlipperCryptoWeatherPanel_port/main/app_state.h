// Shared state, constants, and forward declarations for the dashboard port.
#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Display constants -------------------------------------------------------
#define LCD_H_RES           800
#define LCD_V_RES           480
#define DISPLAY_ROTATE_180_DEFAULT 1
#define UI_LOGICAL_W        LCD_H_RES
#define UI_LOGICAL_H        LCD_V_RES

// --- Colors (Arduino-port-Werte 1:1 uebernommen) ----------------------------
#define COLOR_BG        0x0c0f14
#define COLOR_TEXT      0xeef2f7
#define COLOR_MUTED     0x8490a0
#define COLOR_DIM       0x4e5868
#define COLOR_CYAN      0x5dd6ff
#define COLOR_BTC       0xf7931a
#define COLOR_GREEN     0x2ee59d
#define COLOR_LOSS      0xff5d6c
#define COLOR_RED       0xff0000
#define COLOR_ORANGE    0xffa500
#define COLOR_SUN       0xffd166
#define COLOR_SETTINGS  0xc084fc

#define TEMP_UNIT       " \xc2\xb0""C"

// --- Screen state ------------------------------------------------------------
typedef enum {
    SCREEN_TIME    = 0,
    SCREEN_CRYPTO  = 1,
    SCREEN_BTC_DAY = 2,
    SCREEN_KLIPPER = 3,
} screen_state_t;

typedef enum {
    WEATHER_VISUAL_UNKNOWN,
    WEATHER_VISUAL_CLEAR,
    WEATHER_VISUAL_PARTLY,
    WEATHER_VISUAL_CLOUD,
    WEATHER_VISUAL_RAIN,
    WEATHER_VISUAL_SNOW,
    WEATHER_VISUAL_FOG,
    WEATHER_VISUAL_THUNDER,
} weather_visual_t;

typedef enum {
    SUN_STATUS_VISUAL_UNKNOWN,
    SUN_STATUS_VISUAL_SUNRISE,
    SUN_STATUS_VISUAL_DAY,
    SUN_STATUS_VISUAL_SUNSET,
    SUN_STATUS_VISUAL_NIGHT,
} sun_status_visual_t;

// --- BTC candle storage ------------------------------------------------------
#define BTC_CANDLE_CAPACITY     300
#define BTC_DAY_CANDLE_COUNT    90
#define BTC_CANDLE_SECONDS      86400U
#define BTC_CHART_W             720
#define BTC_CHART_H             250
#define BTC_CHART_CANVAS_H      264
#define BTC_CHART_PROGRESS_Y    258
#define BTC_CHART_PROGRESS_H    5
#define MMU_GATE_MAX            8

typedef struct {
    uint32_t time;
    float low;
    float high;
    float open;
    float close;
    float volume;
} btc_candle_t;

// --- Shared, mutex-protected app state --------------------------------------
typedef struct {
    SemaphoreHandle_t mutex;

    // Time / weather
    char current_temp[16];
    char weather_status[32];
    char weather_location[48];
    int  weather_code;

    // Crypto
    char crypto_price[24];
    char crypto_status[32];
    float crypto_live_price;
    int   crypto_price_direction;
    float crypto_24h_open;
    bool  crypto_24h_ready;

    // BTC day chart
    char  btc_day_change[24];
    char  btc_day_time_range[64];
    char  btc_day_volume[40];
    char  btc_candle_status[24];
    float btc_day_change_percent;
    bool  btc_day_change_positive;
    bool  btc_day_data_ready;
    btc_candle_t *candles;
    int candle_count;

    // Klipper
    bool klipper_available;
    bool klipper_host_available;
    char klipper_state[24];
    char klipper_file[96];
    char klipper_progress[8];
    char klipper_nozzle[24];
    char klipper_bed[24];
    char klipper_duration[40];
    char klipper_status[64];
    char klipper_printer_name[48];
    char klipper_display_message[64];

    // Network / WiFi
    bool wifi_connected;
    bool internet_available;
} app_state_t;

extern app_state_t g_app;

static inline void app_lock(void)   { xSemaphoreTake(g_app.mutex, portMAX_DELAY); }
static inline void app_unlock(void) { xSemaphoreGive(g_app.mutex); }

// --- Configuration (compile-time WiFi/locations) ----------------------------
#if __has_include("config_private.h")
  #include "config_private.h"
#endif
#ifndef WIFI_SSID
  #define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
  #define WIFI_PASSWORD ""
#endif
#ifndef LOCATION_LATITUDE
  #define LOCATION_LATITUDE  52.520007f
#endif
#ifndef LOCATION_LONGITUDE
  #define LOCATION_LONGITUDE 13.404954f
#endif
#ifndef TIMEZONE_POSIX
  #define TIMEZONE_POSIX "CET-1CEST,M3.5.0,M10.5.0/3"
#endif
#ifndef CRYPTO_BASE_SYMBOL
  #define CRYPTO_BASE_SYMBOL "BTC"
#endif
#ifndef CRYPTO_QUOTE_SYMBOL
  #define CRYPTO_QUOTE_SYMBOL "USD"
#endif
#ifndef CRYPTO_PRICE_PREFIX
  #define CRYPTO_PRICE_PREFIX ""
#endif
#ifndef CRYPTO_SERVICE_NAME
  #define CRYPTO_SERVICE_NAME "COINBASE"
#endif
#ifndef CRYPTO_CHART_TIMEFRAME
  #define CRYPTO_CHART_TIMEFRAME "1D"
#endif
#ifndef KLIPPER_BASE_URL
  #define KLIPPER_BASE_URL "http://mainsail"
#endif

// --- UI API (implemented in ui_screens.c) -----------------------------------
void ui_init(void);
void ui_refresh_current(void);
void ui_switch_screen(screen_state_t target);
screen_state_t ui_current_screen(void);
screen_state_t ui_next_screen(screen_state_t state);
screen_state_t ui_previous_screen(screen_state_t state);

// Used by chart module to access labels.
lv_obj_t *ui_get_btc_chart_canvas(void);
lv_obj_t *ui_get_btc_price_high_label(void);
lv_obj_t *ui_get_btc_price_low_label(void);
lv_obj_t *ui_get_btc_price_last_label(void);

// --- Chart API (implemented in ui_chart.c) ----------------------------------
void ui_chart_draw(void);
void ui_chart_invalidate(void);

// --- Popup-Menue API (implemented in ui_popup.c) ----------------------------
void ui_popup_open(void);
void ui_popup_close(void);
bool ui_popup_is_open(void);
void ui_perform_clean_reboot(void);
void ui_perform_factory_reset(void);

// --- Display-Brightness API (implemented in display_brightness.c) ----------
#define DAY_BRIGHTNESS_MIN       32
#define DAY_BRIGHTNESS_MAX       255
#define DAY_BRIGHTNESS_DEFAULT   176

void    display_brightness_init(void);
uint8_t display_brightness_get_day(void);
void    display_brightness_set_day(uint8_t value);     // live preview, no NVS write
void    display_brightness_commit_day(void);           // persist current day value
void    display_brightness_update_by_sun(void);        // re-evaluate day/night
void    display_brightness_request_sun_refresh(void);  // force next update_by_sun to act

// Rotate-180 wird beim Boot aus NVS gelesen; Toggle macht Reboot.
bool display_rotate_180_load(void);          // liest NVS, fallback DISPLAY_ROTATE_180_DEFAULT
void display_rotate_180_save(bool enabled);  // schreibt NVS

// Geteilte Sonnenstand-Berechnung (genutzt von Brightness und Sun-Icon).
bool calculate_sun_times(const struct tm *t, int *sunrise_min, int *sunset_min);

// --- Display-Settings-Screen (implemented in ui_display_settings.c) --------
void ui_display_settings_open(void);
bool ui_display_settings_is_open(void);

// --- Settings-Menue (implemented in ui_settings_menu.c) --------------------
void ui_settings_menu_open(void);
void ui_settings_menu_reopen(void);    // Re-Entry aus Sub-Screen, ohne return_target zu verlieren
bool ui_settings_menu_is_open(void);
lv_obj_t *ui_settings_menu_return_target(void);

// --- Network API (implemented in net_fetcher.c) -----------------------------
void net_start(void);
uint32_t crypto_chart_granularity_seconds(void);
int      crypto_chart_candle_count(void);
const char *crypto_chart_timeframe_label(void);
void     format_quote_compact(float value, char *buffer, size_t bufferSize);

#ifdef __cplusplus
}
#endif

#endif // APP_STATE_H
