// LCD-Backlight via LEDC + Tag/Nacht-Steuerung anhand Sonnenstand.
// Pendant zum Arduino-Original (initBacklightPwm, setDisplayBrightness,
// updateBrightnessBySun aus 01_SystemTime.ino / KlipperCryptoWeatherPanel.ino).

#include "app_state.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "brightness";

// Hardware-Konfiguration ------------------------------------------------------
#define BL_GPIO              GPIO_NUM_2
#define BL_LEDC_MODE         LEDC_LOW_SPEED_MODE
#define BL_LEDC_TIMER        LEDC_TIMER_0
#define BL_LEDC_CHANNEL      LEDC_CHANNEL_0
#define BL_LEDC_FREQ_HZ      250
#define BL_LEDC_RES_BITS     LEDC_TIMER_8_BIT
#define BL_DUTY_MAX          255U
// Untergrenze fuer sichtbares Backlight (verhindert komplettes Schwarz im
// Nacht-Modus). 1:1 aus dem Arduino-Original uebernommen.
#define BL_DUTY_MIN_VISIBLE  12U

// Tag/Nacht-Logik -------------------------------------------------------------
#define NIGHT_BRIGHTNESS         12
#define SUNRISE_DELAY_MINUTES    90

// NVS -------------------------------------------------------------------------
#define NVS_NS         "display"
#define NVS_KEY_DAY    "day_bright"
#define NVS_KEY_ROT180 "rotate180"
#define NVS_KEY_NIGHT  "night_en"

static bool    s_ledc_ready = false;
static uint8_t s_day_brightness = DAY_BRIGHTNESS_DEFAULT;
static uint8_t s_applied_brightness = 0;
static bool    s_sun_state_known = false;
static bool    s_night_mode_enabled = true;

static uint32_t duty_for_brightness(uint8_t brightness)
{
    if (brightness == 0) return 0;
    return BL_DUTY_MIN_VISIBLE +
           (((uint32_t)brightness * (BL_DUTY_MAX - BL_DUTY_MIN_VISIBLE)) / 255U);
}

static void write_duty(uint8_t brightness)
{
    if (!s_ledc_ready) return;
    s_applied_brightness = brightness;
    uint32_t duty = duty_for_brightness(brightness);
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
}

static uint8_t clamp_day(int v)
{
    if (v < DAY_BRIGHTNESS_MIN) return DAY_BRIGHTNESS_MIN;
    if (v > DAY_BRIGHTNESS_MAX) return DAY_BRIGHTNESS_MAX;
    return (uint8_t)v;
}

static int clamp_minute_of_day(int m)
{
    if (m < 0) return 0;
    if (m > 1439) return 1439;
    return m;
}

// NVS -------------------------------------------------------------------------
static bool nvs_load_day_brightness(uint8_t *out)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t v = 0;
    esp_err_t err = nvs_get_u8(h, NVS_KEY_DAY, &v);
    nvs_close(h);
    if (err != ESP_OK) return false;
    // DAY_BRIGHTNESS_MAX == 255 == UINT8_MAX, daher reicht die Untergrenze.
    if (v < DAY_BRIGHTNESS_MIN) return false;
    *out = v;
    return true;
}

bool display_rotate_180_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return DISPLAY_ROTATE_180_DEFAULT ? true : false;
    }
    uint8_t v = 0;
    esp_err_t err = nvs_get_u8(h, NVS_KEY_ROT180, &v);
    nvs_close(h);
    if (err != ESP_OK) return DISPLAY_ROTATE_180_DEFAULT ? true : false;
    return v != 0;
}

void display_rotate_180_save(bool enabled)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open RW: %s", esp_err_to_name(err));
        return;
    }
    nvs_set_u8(h, NVS_KEY_ROT180, enabled ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "rotate180=%u gespeichert", enabled ? 1u : 0u);
}

bool display_night_mode_enabled_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return true;
    uint8_t v = 1;
    esp_err_t err = nvs_get_u8(h, NVS_KEY_NIGHT, &v);
    nvs_close(h);
    if (err != ESP_OK) return true;
    return v != 0;
}

bool display_night_mode_get(void)
{
    return s_night_mode_enabled;
}

void display_night_mode_save(bool enabled)
{
    s_night_mode_enabled = enabled;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open RW: %s", esp_err_to_name(err));
    } else {
        nvs_set_u8(h, NVS_KEY_NIGHT, enabled ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "night_enable=%u gespeichert", enabled ? 1u : 0u);
    }
    // Sun-Tick neu auswerten, damit der Wechsel sofort sichtbar wird.
    s_sun_state_known = false;
    display_brightness_update_by_sun();
    // Falls update_by_sun nicht aktiv werden konnte (NTP noch nicht gelaufen),
    // mindestens die Tag-Helligkeit anschreiben, wenn Nacht-Modus deaktiviert ist.
    if (!enabled) {
        write_duty(s_day_brightness);
    }
}

// Sonnenstand (geteilt mit ui_screens.c) -------------------------------------
bool calculate_sun_times(const struct tm *t, int *sunrise_min, int *sunset_min)
{
    const double pi = 3.14159265358979323846;
    const double deg_to_rad = pi / 180.0;
    const double rad_to_deg = 180.0 / pi;
    const int day_of_year = t->tm_yday + 1;
    const double gamma = (2.0 * pi / 365.0) * (day_of_year - 1);

    const double eq_of_time = 229.18 * (0.000075 +
                              0.001868 * cos(gamma) -
                              0.032077 * sin(gamma) -
                              0.014615 * cos(2.0 * gamma) -
                              0.040849 * sin(2.0 * gamma));
    const double decl = 0.006918 -
                        0.399912 * cos(gamma) +
                        0.070257 * sin(gamma) -
                        0.006758 * cos(2.0 * gamma) +
                        0.000907 * sin(2.0 * gamma) -
                        0.002697 * cos(3.0 * gamma) +
                        0.00148  * sin(3.0 * gamma);
    float lat_f = 0.0f, lon_f = 0.0f;
    location_snapshot(&lat_f, &lon_f);
    const double lat_rad = (double)lat_f * deg_to_rad;
    const double zen_rad = 90.833 * deg_to_rad;
    const double hour_arg = (cos(zen_rad) / (cos(lat_rad) * cos(decl))) -
                            (tan(lat_rad) * tan(decl));
    if (hour_arg < -1.0 || hour_arg > 1.0) return false;
    const double hour_deg = acos(hour_arg) * rad_to_deg;
    const int utc_off = (t->tm_isdst > 0) ? 120 : 60;
    const double solar_noon = 720.0 - (4.0 * (double)lon_f) - eq_of_time + utc_off;
    *sunrise_min = clamp_minute_of_day((int)round(solar_noon - (4.0 * hour_deg)));
    *sunset_min  = clamp_minute_of_day((int)round(solar_noon + (4.0 * hour_deg)));
    return *sunrise_min < *sunset_min;
}

// Public API ------------------------------------------------------------------
void display_brightness_init(void)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode      = BL_LEDC_MODE,
        .timer_num       = BL_LEDC_TIMER,
        .duty_resolution = BL_LEDC_RES_BITS,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config: %s", esp_err_to_name(err));
        return;
    }

    const ledc_channel_config_t ch_cfg = {
        .gpio_num   = BL_GPIO,
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .duty       = 0,
        .hpoint     = 0,
    };
    err = ledc_channel_config(&ch_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config: %s", esp_err_to_name(err));
        return;
    }
    s_ledc_ready = true;

    uint8_t stored = 0;
    if (nvs_load_day_brightness(&stored)) {
        s_day_brightness = stored;
        ESP_LOGI(TAG, "NVS day_brightness=%u", s_day_brightness);
    }
    s_night_mode_enabled = display_night_mode_enabled_load();
    ESP_LOGI(TAG, "NVS night_enable=%u", s_night_mode_enabled ? 1u : 0u);
    // Startwert: Tag-Helligkeit. updateBrightnessBySun() korrigiert spaeter,
    // sobald NTP gelaufen ist und der Nacht-Modus erkannt wird.
    write_duty(s_day_brightness);
}

uint8_t display_brightness_get_day(void)
{
    return s_day_brightness;
}

void display_brightness_set_day(uint8_t value)
{
    s_day_brightness = clamp_day(value);
    // Live-Preview: ueberschreibt einen evtl. aktiven Nacht-Wert. Der Sun-Tick
    // korrigiert das beim naechsten Lauf wieder, wenn Settings geschlossen ist
    // (request_sun_refresh forciert das).
    write_duty(s_day_brightness);
    s_sun_state_known = false;
}

void display_brightness_commit_day(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open RW: %s", esp_err_to_name(err));
        return;
    }
    nvs_set_u8(h, NVS_KEY_DAY, s_day_brightness);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "day_brightness=%u gespeichert", s_day_brightness);
}

void display_brightness_request_sun_refresh(void)
{
    s_sun_state_known = false;
}

void display_brightness_update_by_sun(void)
{
    if (!s_ledc_ready) return;

    time_t now = time(NULL);
    if (now < 100000) return;  // NTP noch nicht gelaufen

    struct tm lt;
    localtime_r(&now, &lt);

    int sunrise_min = 0, sunset_min = 0;
    if (!calculate_sun_times(&lt, &sunrise_min, &sunset_min)) return;

    const int now_min = (lt.tm_hour * 60) + lt.tm_min;
    int day_start = sunrise_min + SUNRISE_DELAY_MINUTES;
    if (day_start > sunset_min) day_start = sunset_min;
    day_start = clamp_minute_of_day(day_start);

    const bool night = (now_min < day_start) || (now_min >= sunset_min);
    const uint8_t target = (night && s_night_mode_enabled) ? NIGHT_BRIGHTNESS
                                                           : s_day_brightness;

    if (s_sun_state_known && target == s_applied_brightness) return;

    write_duty(target);
    s_sun_state_known = true;
    ESP_LOGI(TAG,
             "Helligkeit %s (%u), Sonnenaufgang %02d:%02d, hell ab %02d:%02d, Sonnenuntergang %02d:%02d",
             night ? "Nacht" : "Tag", target,
             sunrise_min / 60, sunrise_min % 60,
             day_start / 60,   day_start % 60,
             sunset_min / 60,  sunset_min % 60);
}
