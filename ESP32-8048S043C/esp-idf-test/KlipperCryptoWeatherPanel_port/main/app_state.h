// Shared state, constants, and forward declarations for the dashboard port.
#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_lcd_touch.h"
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

// --- Runtime crypto settings ------------------------------------------------
#define CRYPTO_SYMBOL_MAX_LEN     7
#define CRYPTO_PREFIX_MAX_LEN     7
#define CRYPTO_SERVICE_MAX_LEN    15
#define CRYPTO_TIMEFRAME_MAX_LEN  7

typedef struct {
    char base[CRYPTO_SYMBOL_MAX_LEN + 1];
    char quote[CRYPTO_SYMBOL_MAX_LEN + 1];
    char price_prefix[CRYPTO_PREFIX_MAX_LEN + 1];
    char service[CRYPTO_SERVICE_MAX_LEN + 1];
    char timeframe[CRYPTO_TIMEFRAME_MAX_LEN + 1];
} crypto_config_t;

// --- Forecast storage --------------------------------------------------------
#define WEATHER_DAILY_COUNT     5

typedef struct {
    int   code;            // WMO, -1 wenn ungueltig
    float tmin;            // NaN wenn ungueltig
    float tmax;            // NaN wenn ungueltig
    int   precip_prob_max; // 0..100, -1 wenn ungueltig
    int   sunrise_min;     // Minuten seit Mitternacht, -1 wenn ungueltig
    int   sunset_min;      // Minuten seit Mitternacht, -1 wenn ungueltig
    int   weekday;         // 0=So..6=Sa (passt zu WEEKDAYS_DE in ui_screens.c)
} weather_daily_slot_t;

// --- Shared, mutex-protected app state --------------------------------------
typedef struct {
    SemaphoreHandle_t mutex;

    // Time / weather
    char current_temp[16];
    char weather_status[32];
    char weather_location[48];
    int  weather_code;

    // Wetter-Extras (Open-Meteo current=) fuer Detail-Screen.
    // NaN bzw. -1 wenn noch nicht abgerufen.
    float weather_apparent_temp;
    float weather_wind_speed;       // km/h
    int   weather_wind_dir;         // Grad, 0..359; -1 wenn ungueltig
    int   weather_humidity;         // %, 0..100; -1 wenn ungueltig
    int   weather_is_day;           // 0/1, -1 wenn ungueltig

    // Forecast (Open-Meteo daily) fuer Detail-Screen.
    // Slot-Inhalt 0-initialisiert; weather_forecast_ready erst nach erstem Fetch.
    weather_daily_slot_t  weather_daily[WEATHER_DAILY_COUNT];
    int  weather_daily_count;
    bool weather_forecast_ready;

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
    char klipper_connection_state[24];
    char klipper_connection_message[96];
    char klipper_state[24];
    char klipper_file[96];
    char klipper_progress[8];
    char klipper_nozzle[24];
    char klipper_bed[24];
    char klipper_duration[40];
    char klipper_status[64];
    char klipper_printer_name[48];
    char klipper_display_message[64];
    char klipper_metadata_filename[192];
    float klipper_estimated_duration_seconds;
    uint32_t klipper_metadata_retry_after;
    bool klipper_mmu_available;
    char klipper_mmu_info[64];
    int klipper_mmu_tool;
    int klipper_mmu_gate;
    int klipper_mmu_gate_count;
    uint32_t klipper_mmu_gate_colors[MMU_GATE_MAX];
    int klipper_mmu_gate_status[MMU_GATE_MAX];

    // Network / WiFi
    bool wifi_connected;
    bool internet_available;

    // Location (runtime, NVS-gepuffert mit Fallback auf LOCATION_LATITUDE/LONGITUDE)
    float location_latitude;
    float location_longitude;
} app_state_t;

extern app_state_t g_app;
extern crypto_config_t g_crypto;

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
void ui_load_current_screen_no_anim(void);
screen_state_t ui_current_screen(void);
screen_state_t ui_next_screen(screen_state_t state);
screen_state_t ui_previous_screen(screen_state_t state);
bool ui_screen_is_available(screen_state_t state);

// Hitbox des Wetter-Icons auf dem Time-Screen (User-Space-Koordinaten).
// main.c benutzt das, um einen Tap aufs Icon vom Screen-Swipe zu trennen.
bool ui_time_weather_hitbox_contains(int x, int y);

// --- Wetter-Detail-Screen (implemented in ui_weather_detail.c) -------------
void ui_weather_detail_open(void);
bool ui_weather_detail_is_open(void);

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

// --- Firmware-Info-Screen (implemented in ui_firmware_info.c) --------------
void ui_firmware_info_open(void);
bool ui_firmware_info_is_open(void);

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

// Nacht-Helligkeits-Feature (sonnenstandsgesteuert) deaktivierbar; Default an.
bool display_night_mode_enabled_load(void);
bool display_night_mode_get(void);
void display_night_mode_save(bool enabled);  // schreibt NVS und triggert Sun-Refresh

// Geteilte Sonnenstand-Berechnung (genutzt von Brightness und Sun-Icon).
bool calculate_sun_times(const struct tm *t, int *sunrise_min, int *sunset_min);

// --- Display-Settings-Screen (implemented in ui_display_settings.c) --------
void ui_display_settings_open(void);
bool ui_display_settings_is_open(void);

// --- Screen-Settings (implemented in ui_screen_settings.c) -----------------
#define SCREEN_DURATION_MIN_S      5
#define SCREEN_DURATION_MAX_S      120
#define SCREEN_DURATION_TIME_DEF   30
#define SCREEN_DURATION_OTHER_DEF  15

void screen_settings_init_defaults(void);
bool screen_settings_load(void);
bool screen_settings_save(void);
bool screen_settings_is_enabled(screen_state_t state);
void screen_settings_set_enabled(screen_state_t state, bool enabled);
int  screen_settings_enabled_count(void);
void screen_settings_ensure_one_enabled(void);
uint8_t screen_settings_duration_seconds(screen_state_t state);
void    screen_settings_set_duration_seconds(screen_state_t state, uint8_t seconds);
void ui_screen_settings_open(void);
bool ui_screen_settings_is_open(void);

// --- Settings-Menue (implemented in ui_settings_menu.c) --------------------
void ui_settings_menu_open(void);
void ui_settings_menu_reopen(void);    // Re-Entry aus Sub-Screen, ohne return_target zu verlieren
bool ui_settings_menu_is_open(void);
lv_obj_t *ui_settings_menu_return_target(void);

// --- Touch-Kalibrierung (implemented in touch_calibration.c) ---------------
bool touch_calibration_load(void);
void touch_calibration_set_rotation(bool rotate_180);
void touch_calibration_set_handle(esp_lcd_touch_handle_t touch);
bool touch_calibration_is_open(void);
void touch_calibration_process_coords(esp_lcd_touch_handle_t tp,
                                      uint16_t *x, uint16_t *y, uint16_t *strength,
                                      uint8_t *point_num, uint8_t max_point_num);
void ui_touch_calibration_open(void);

// --- WLAN-Setup (implemented in ui_wifi_setup.c) ---------------------------
void ui_wifi_setup_open(void);
bool ui_wifi_setup_is_open(void);

// --- Network API (implemented in net_fetcher.c) -----------------------------
void net_start(void);
// WLAN-Credentials persistent in NVS (Fallback: Compile-Time WIFI_SSID/PASSWORD).
// load schreibt SSID/Passwort in die Puffer und liefert true, wenn SSID nicht leer.
bool wifi_credentials_load(char *ssid_out, size_t ssid_size,
                           char *pass_out, size_t pass_size);
bool wifi_credentials_save(const char *ssid, const char *pass);
void crypto_settings_init_defaults(void);
bool crypto_settings_load(void);
bool crypto_settings_save(void);
void crypto_settings_apply(const char *base, const char *quote, const char *timeframe);
void crypto_reset_data_state(const char *status_text);
void crypto_request_refresh(void);
void crypto_pair_title_text(char *out, size_t out_size);
void crypto_day_title_text(char *out, size_t out_size);
const char *crypto_ok_status(void);
uint32_t crypto_chart_granularity_seconds(void);
int      crypto_chart_candle_count(void);
const char *crypto_chart_timeframe_label(void);
void     format_quote_compact(float value, char *buffer, size_t bufferSize);

// --- Crypto-Settings-Screen (implemented in ui_crypto_settings.c) -----------
void ui_crypto_settings_open(void);
bool ui_crypto_settings_is_open(void);

// --- Location-Settings (implemented in net_fetcher.c / ui_location_settings.c)
// Lat/Lon werden zur Laufzeit aus NVS geladen; Fallback sind die Compile-Time-
// Werte LOCATION_LATITUDE/LONGITUDE. snapshot ist mutex-sicher.
void location_settings_init_defaults(void);
bool location_settings_load(void);
bool location_settings_save(float latitude, float longitude);
void location_snapshot(float *latitude, float *longitude);
void ui_location_settings_open(void);
bool ui_location_settings_is_open(void);

#ifdef __cplusplus
}
#endif

#endif // APP_STATE_H
