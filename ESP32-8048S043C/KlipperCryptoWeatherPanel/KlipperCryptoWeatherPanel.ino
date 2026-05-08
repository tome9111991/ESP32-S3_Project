#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <StreamString.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#if __has_include(<esp_arduino_version.h>)
  #include <esp_arduino_version.h>
#endif

#ifndef ESP_ARDUINO_VERSION_MAJOR
  #define ESP_ARDUINO_VERSION_MAJOR 3
#endif

#define LV_CONF_INCLUDE_SIMPLE
#include <lvgl.h>
#include "ui_assets.h"
#include "ui_font_price_digits.h"
#include "ui_font_time_digits.h"

// Compatibility for generated LVGL assets/fonts.
#ifndef LV_ATTRIBUTE_LARGE_CONST
  #define LV_ATTRIBUTE_LARGE_CONST
#endif

#include <ArduinoJson.h>

#ifndef RGB565
  #define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#endif

static HardwareSerial& DebugSerial = Serial0;
#define Serial DebugSerial

static constexpr int LCD_W = 800;
static constexpr int LCD_H = 480;
static constexpr int LCD_BL = 2;
static constexpr bool LCD_BL_ACTIVE_HIGH = true;
static constexpr int LCD_BL_PWM_FREQ_HZ = 1000;
static constexpr int LCD_BL_PWM_RESOLUTION_BITS = 8;
static constexpr uint32_t LCD_BL_PWM_MAX_DUTY = 255;
static constexpr uint32_t LCD_BL_PWM_MIN_VISIBLE_DUTY = 44;
static constexpr int LCD_BL_PWM_CHANNEL = 0;
static constexpr int LCD_PCLK_HZ = 14000000;
static constexpr int LCD_BOUNCE_LINES = 10;
static constexpr bool LCD_ROTATE_180 = true;
static constexpr int UI_LOGICAL_W = LCD_W;
static constexpr int UI_LOGICAL_H = LCD_H;
static constexpr int UI_OFFSET_X = (LCD_W - UI_LOGICAL_W) / 2;
static constexpr int UI_OFFSET_Y = (LCD_H - UI_LOGICAL_H) / 2;

static esp_lcd_panel_handle_t panel = nullptr;
static SemaphoreHandle_t colorDoneSem = nullptr;
static lv_display_t* lvDisplay = nullptr;
static lv_color_t* lvDrawBuf = nullptr;
static uint32_t lvDrawBufBytes = 0;
static bool lvDrawBufFullMode = false;
static volatile uint32_t lvFlushCount = 0;
static volatile uint32_t lvFlushTimeoutCount = 0;
static volatile uint32_t lvFlushErrorCount = 0;
static bool backlightPwmReady = false;

static uint32_t backlightDutyForBrightness(uint8_t brightness) {
  uint32_t duty = 0;
  if (brightness > 0) {
    duty = LCD_BL_PWM_MIN_VISIBLE_DUTY +
      (((uint32_t)brightness * (LCD_BL_PWM_MAX_DUTY - LCD_BL_PWM_MIN_VISIBLE_DUTY)) / 255U);
  }
  if (!LCD_BL_ACTIVE_HIGH) {
    duty = LCD_BL_PWM_MAX_DUTY - duty;
  }
  return duty;
}

static bool initBacklightPwm(uint8_t initialBrightness) {
  const uint32_t duty = backlightDutyForBrightness(initialBrightness);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (!ledcAttach(LCD_BL, LCD_BL_PWM_FREQ_HZ, LCD_BL_PWM_RESOLUTION_BITS)) {
    Serial.println("Backlight PWM Init fehlgeschlagen");
    return false;
  }
  ledcWrite(LCD_BL, duty);
#else
  ledcSetup(LCD_BL_PWM_CHANNEL, LCD_BL_PWM_FREQ_HZ, LCD_BL_PWM_RESOLUTION_BITS);
  ledcAttachPin(LCD_BL, LCD_BL_PWM_CHANNEL);
  ledcWrite(LCD_BL_PWM_CHANNEL, duty);
#endif
  return true;
}

static void writeBacklightLevel(uint8_t brightness) {
  if (!backlightPwmReady) {
    return;
  }

  const uint32_t duty = backlightDutyForBrightness(brightness);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(LCD_BL, duty);
#else
  ledcWrite(LCD_BL_PWM_CHANNEL, duty);
#endif
}

static bool IRAM_ATTR onColorTransferDone(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  BaseType_t highTaskWoken = pdFALSE;
  if (colorDoneSem != nullptr) {
    xSemaphoreGiveFromISR(colorDoneSem, &highTaskWoken);
  }
  return highTaskWoken == pdTRUE;
}

static bool initBoardDisplay() {
  colorDoneSem = xSemaphoreCreateBinary();
  if (colorDoneSem == nullptr) {
    Serial.println("Display Semaphore Init fehlgeschlagen!");
    return false;
  }

  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = LCD_PCLK_HZ;
  cfg.timings.h_res = LCD_W;
  cfg.timings.v_res = LCD_H;
  cfg.timings.hsync_pulse_width = 4;
  cfg.timings.hsync_back_porch = 16;
  cfg.timings.hsync_front_porch = 8;
  cfg.timings.vsync_pulse_width = 4;
  cfg.timings.vsync_back_porch = 4;
  cfg.timings.vsync_front_porch = 4;
  cfg.timings.flags.hsync_idle_low = 1;
  cfg.timings.flags.vsync_idle_low = 1;
  cfg.timings.flags.de_idle_high = 0;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.timings.flags.pclk_idle_high = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 2;
  cfg.bounce_buffer_size_px = LCD_W * LCD_BOUNCE_LINES;
  cfg.sram_trans_align = 8;
  cfg.psram_trans_align = 64;
  cfg.hsync_gpio_num = 39;
  cfg.vsync_gpio_num = 41;
  cfg.de_gpio_num = 40;
  cfg.pclk_gpio_num = 42;
  cfg.disp_gpio_num = GPIO_NUM_NC;
  cfg.data_gpio_nums[0] = 8;   // B0
  cfg.data_gpio_nums[1] = 3;   // B1
  cfg.data_gpio_nums[2] = 46;  // B2
  cfg.data_gpio_nums[3] = 9;   // B3
  cfg.data_gpio_nums[4] = 1;   // B4
  cfg.data_gpio_nums[5] = 5;   // G0
  cfg.data_gpio_nums[6] = 6;   // G1
  cfg.data_gpio_nums[7] = 7;   // G2
  cfg.data_gpio_nums[8] = 15;  // G3
  cfg.data_gpio_nums[9] = 16;  // G4
  cfg.data_gpio_nums[10] = 4;  // G5
  cfg.data_gpio_nums[11] = 45; // R0
  cfg.data_gpio_nums[12] = 48; // R1
  cfg.data_gpio_nums[13] = 47; // R2
  cfg.data_gpio_nums[14] = 21; // R3
  cfg.data_gpio_nums[15] = 14; // R4
  cfg.flags.disp_active_low = true;
  cfg.flags.refresh_on_demand = false;
  cfg.flags.fb_in_psram = true;
  cfg.flags.double_fb = false;
  cfg.flags.no_fb = false;
  cfg.flags.bb_invalidate_cache = false;

  esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &panel);
  if (err != ESP_OK) {
    Serial.printf("esp_lcd_new_rgb_panel fehlgeschlagen: 0x%X\n", err);
    return false;
  }

  esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
  callbacks.on_color_trans_done = onColorTransferDone;
  err = esp_lcd_rgb_panel_register_event_callbacks(panel, &callbacks, nullptr);
  if (err != ESP_OK) {
    Serial.printf("esp_lcd_rgb_panel_register_event_callbacks fehlgeschlagen: 0x%X\n", err);
    return false;
  }

  err = esp_lcd_panel_reset(panel);
  if (err != ESP_OK) {
    Serial.printf("esp_lcd_panel_reset fehlgeschlagen: 0x%X\n", err);
    return false;
  }

  err = esp_lcd_panel_init(panel);
  if (err != ESP_OK) {
    Serial.printf("esp_lcd_panel_init fehlgeschlagen: 0x%X\n", err);
    return false;
  }

  if (LCD_ROTATE_180) {
    err = esp_lcd_panel_mirror(panel, true, true);
    if (err != ESP_OK) {
      Serial.printf("esp_lcd_panel_mirror fehlgeschlagen: 0x%X\n", err);
    }
  }

  esp_lcd_panel_disp_on_off(panel, true);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, LCD_BL_ACTIVE_HIGH ? HIGH : LOW);
  backlightPwmReady = initBacklightPwm(255);
  return true;
}

const uint32_t COLOR_BG = 0x0c0f14;
const uint32_t COLOR_TEXT = 0xeef2f7;
const uint32_t COLOR_MUTED = 0x8490a0;
const uint32_t COLOR_DIM = 0x4e5868;
const uint32_t COLOR_CYAN = 0x5dd6ff;
const uint32_t COLOR_BTC = 0xf7931a;
const uint32_t COLOR_GREEN = 0x2ee59d;
const uint32_t COLOR_LOSS = 0xff5d6c;
const uint32_t COLOR_RED = 0xff0000;
const uint32_t COLOR_ORANGE = 0xffa500;
const uint32_t COLOR_SUN = 0xffd166;
const uint32_t COLOR_MOON = 0xf4f8ff;
const uint32_t COLOR_CLOUD = 0xaeb8c5;
const uint32_t COLOR_RAIN = 0x58c8ff;
const uint32_t COLOR_FOG = 0x6f7b8d;
const char* TEMP_UNIT = " \xc2\xb0""C";
const char* WEEKDAYS_DE[] = {
  "Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"
};

// --- KONFIGURATION ---
#include "config_private.h"

#ifndef WIFI_SSID
  #error "WIFI_SSID fehlt. Kopiere config_private.example.h nach config_private.h und trage deine Daten ein."
#endif
#ifndef WIFI_PASSWORD
  #error "WIFI_PASSWORD fehlt. Kopiere config_private.example.h nach config_private.h und trage deine Daten ein."
#endif
#ifndef LOCATION_LATITUDE
  #error "LOCATION_LATITUDE fehlt. Kopiere config_private.example.h nach config_private.h und trage deine Daten ein."
#endif
#ifndef LOCATION_LONGITUDE
  #error "LOCATION_LONGITUDE fehlt. Kopiere config_private.example.h nach config_private.h und trage deine Daten ein."
#endif
#ifndef TIMEZONE_POSIX
  #error "TIMEZONE_POSIX fehlt. Kopiere config_private.example.h nach config_private.h und trage deine Zeitzone ein."
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
#ifndef KLIPPER_BASE_URL
  #define KLIPPER_BASE_URL "http://mainsail"
#endif

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// Bright Sky liefert DWD-Daten ohne API-Key.
const float locationLatitude = LOCATION_LATITUDE;
const float locationLongitude = LOCATION_LONGITUDE;
const char* timezonePosix = TIMEZONE_POSIX;
// Coinbase Crypto-Paar. Fuer SOL/EUR z.B. cryptoBaseSymbol="SOL", cryptoQuoteSymbol="EUR".
const char* cryptoBaseSymbol = CRYPTO_BASE_SYMBOL;
const char* cryptoQuoteSymbol = CRYPTO_QUOTE_SYMBOL;
const char* cryptoPricePrefix = CRYPTO_PRICE_PREFIX; // leer = automatisch: USD/EUR/GBP/JPY/BTC Symbol, sonst Quote-Code
const char* cryptoServiceName = CRYPTO_SERVICE_NAME;
const char* klipperBaseUrl = KLIPPER_BASE_URL;
const unsigned long btcRefreshInterval = 60000; // 60 Sekunden
const unsigned long btcRetryInterval = 30000; // 30 Sekunden bei Fehlern
const unsigned long btcCandleRefreshInterval = 300000; // 5 Minuten
const unsigned long btcCandleRetryInterval = 60000; // 60 Sekunden bei Fehlern
const unsigned long weatherRefreshInterval = 300000; // 5 Minuten
const unsigned long weatherRetryInterval = 120000; // 2 Minuten bei Fehlern
const unsigned long klipperRefreshInterval = 7000; // 7 Sekunden
const unsigned long klipperRetryInterval = 30000; // 30 Sekunden bei Fehlern
const unsigned long klipperNameRefreshInterval = 300000; // 5 Minuten
const unsigned long klipperNameRetryInterval = 120000; // 2 Minuten bei Fehlern
const uint32_t BTC_CANDLE_SECONDS = 86400; // Tageskerzen
const int WEATHER_ICON_W = 56;
const int WEATHER_ICON_H = 48;
const int DIVIDER_H = 4;
const int TIME_SECOND_BAR_W = 360;
const int MMU_GATE_MAX = 8;

// --- GLOBALE VARIABLEN (Thread-Safe) ---
SemaphoreHandle_t dataMutex;
SemaphoreHandle_t networkMutex;
TaskHandle_t fetchTaskHandle = NULL;
String currentBtcPrice = "Laden...";
float currentBtcLivePrice = 0.0f;
int currentBtcPriceDirection = 0;
String currentBtcStatus = String(cryptoServiceName) + " " + cryptoQuoteSymbol;
String btcDayChange = "1D --";
String btcDayRange = "H --  L --";
String btcDayVolume = "VOL --";
String btcCandleStatus = "CANDLE --";
String currentTemp = "--";
String weatherStatus = "WETTER: --";
String weatherLocation = "DWD Station";
int weatherCode = -1;
bool klipperAvailable = false;
bool klipperHostAvailable = false;
String klipperConnectionState = "--";
String klipperConnectionMessage = "";
String klipperState = "--";
String klipperFile = "Kein Job";
String klipperProgress = "--";
String klipperNozzle = "--";
String klipperBed = "--";
String klipperDuration = "--";
String klipperStatus = "KLIPPER --";
String klipperDisplayMessage = "";
String klipperPrinterName = "KLIPPER";
String klipperMetadataFilename = "";
float klipperEstimatedDurationSeconds = 0.0f;
unsigned long klipperMetadataRetryAfter = 0;
bool klipperMmuAvailable = false;
String klipperMmuInfo = "MMU --";
int klipperMmuTool = -1;
int klipperMmuGate = -1;
int klipperMmuGateCount = 0;
uint32_t klipperMmuGateColors[MMU_GATE_MAX] = {
  COLOR_DIM, COLOR_DIM, COLOR_DIM, COLOR_DIM,
  COLOR_DIM, COLOR_DIM, COLOR_DIM, COLOR_DIM
};
int klipperMmuGateStatus[MMU_GATE_MAX] = {
  -1, -1, -1, -1, -1, -1, -1, -1
};
volatile bool wifiConnected = false;
bool timeConfigured = false;
volatile unsigned long wifiConnectedSince = 0;
bool internetAvailable = false;
unsigned long lastInternetProbe = 0;
unsigned long lastWifiReconnectAttempt = 0;
const unsigned long wifiReconnectInterval = 10000;
const unsigned long wifiConnectAttemptTimeout = 30000;
const unsigned long wifiStableBeforeFetchInterval = 8000;
const unsigned long apiRequestGapInterval = 1500;
const unsigned long internetProbeRetryInterval = 30000;
unsigned long lastHealthLog = 0;
const unsigned long healthLogInterval = 60000;
const uint32_t fetchTaskStackSize = 24576;
const UBaseType_t fetchTaskPriority = 1;
// Auf diesem Setup nicht auf Core 0 legen: dort laufen WLAN/TCPIP-Anteile und IDLE0-WDT.
const BaseType_t fetchTaskCore = 1;

struct BtcCandle {
  uint32_t time;
  float low;
  float high;
  float open;
  float close;
  float volume;
};

const int BTC_CANDLE_CAPACITY = 300;
const int BTC_DAY_CANDLE_COUNT = 90; // 90 Tageskerzen
const int BTC_CHART_W = 720;
const int BTC_CHART_H = 250;
const int BTC_CHART_CANVAS_H = 264;
const int BTC_CHART_PROGRESS_Y = 258;
const int BTC_CHART_PROGRESS_H = 5;
BtcCandle* btcCandles = nullptr;
BtcCandle* parsedBtcCandles = nullptr;
BtcCandle* chartBtcCandles = nullptr;
bool btcCandleStorageInPsram = false;
bool parsedBtcCandleStorageInPsram = false;
bool chartBtcCandleStorageInPsram = false;
int btcCandleCount = 0;
float btcDayChangePercent = 0.0f;
bool btcDayChangePositive = true;
bool btcDayDataReady = false;
unsigned long lastBtcChartDraw = 0;
const unsigned long btcChartDrawInterval = 1000;

// UI State Machine
enum ScreenState { SCREEN_TIME, SCREEN_CRYPTO, SCREEN_BTC_DAY, SCREEN_KLIPPER };
enum WeatherVisual {
  WEATHER_VISUAL_UNKNOWN,
  WEATHER_VISUAL_CLEAR,
  WEATHER_VISUAL_PARTLY,
  WEATHER_VISUAL_CLOUD,
  WEATHER_VISUAL_RAIN,
  WEATHER_VISUAL_SNOW,
  WEATHER_VISUAL_FOG,
  WEATHER_VISUAL_THUNDER
};
enum SunStatusVisual {
  SUN_STATUS_VISUAL_UNKNOWN,
  SUN_STATUS_VISUAL_SUNRISE,
  SUN_STATUS_VISUAL_DAY,
  SUN_STATUS_VISUAL_SUNSET,
  SUN_STATUS_VISUAL_NIGHT
};

ScreenState currentScreen = SCREEN_TIME;
unsigned long lastScreenSwitch = 0;
unsigned long lastUiRefresh = 0;
const unsigned long defaultScreenInterval = 10000; // 10 Sekunden fuer alle normalen Screens
const unsigned long timeScreenInterval = 25000;    // Uhrzeit-Screen 25 Sekunden anzeigen
const unsigned long uiRefreshInterval = 500;
const unsigned long screenTransitionDuration = 180;
const unsigned long screenSwitchSettleInterval = 450; // Chart erst nach Screenwechsel/Animation neu zeichnen
const unsigned long brightnessRefreshInterval = 30000;
const uint8_t dayBrightness = 176;
const uint8_t nightBrightness = 24;
const int sunriseBrightnessDelayMinutes = 90; // Display morgens erst 90 Minuten nach Sonnenaufgang hell schalten
uint8_t currentBrightness = dayBrightness;
unsigned long lastBrightnessRefresh = 0;

static lv_obj_t* bootScreen = nullptr;
static lv_obj_t* timeScreen = nullptr;
static lv_obj_t* cryptoScreen = nullptr;
static lv_obj_t* btcDayScreen = nullptr;
static lv_obj_t* klipperScreen = nullptr;
static lv_obj_t* timeAccent = nullptr;
static lv_obj_t* timeLocationLabel = nullptr;
static lv_obj_t* timeLabel = nullptr;
static lv_obj_t* weekdayLabel = nullptr;
static lv_obj_t* dateLabel = nullptr;
static lv_obj_t* tempLabel = nullptr;
static lv_obj_t* weatherIconRoot = nullptr;
static lv_obj_t* timeDivider = nullptr;
static lv_obj_t* timeSecondFill = nullptr;
static lv_obj_t* timeStatusTitle = nullptr;
static lv_obj_t* timeStatusDetail = nullptr;
static lv_obj_t* cryptoPriceLabel = nullptr;
static lv_obj_t* cryptoStatusLabel = nullptr;
static lv_obj_t* btcDayPriceLabel = nullptr;
static lv_obj_t* btcDayChangeLabel = nullptr;
static lv_obj_t* btcDayRangeLabel = nullptr;
static lv_obj_t* btcDayVolumeLabel = nullptr;
static lv_obj_t* btcDayCandleLabel = nullptr;
static lv_obj_t* btcDayChartCanvas = nullptr;
static lv_obj_t* klipperAccent = nullptr;
static lv_obj_t* klipperDivider = nullptr;
static lv_obj_t* klipperOfflineRing = nullptr;
static lv_obj_t* klipperOfflineStem = nullptr;
static lv_obj_t* klipperOfflineLine = nullptr;
static lv_obj_t* klipperTitleLabel = nullptr;
static lv_obj_t* klipperStateLabel = nullptr;
static lv_obj_t* klipperFileLabel = nullptr;
static lv_obj_t* klipperProgressLabel = nullptr;
static lv_obj_t* klipperProgressArc = nullptr;
static lv_obj_t* klipperNozzleTitleLabel = nullptr;
static lv_obj_t* klipperNozzleLabel = nullptr;
static lv_obj_t* klipperBedTitleLabel = nullptr;
static lv_obj_t* klipperBedLabel = nullptr;
static lv_obj_t* klipperDurationLabel = nullptr;
static lv_obj_t* klipperStatusLabel = nullptr;
static lv_obj_t* klipperMmuLabel = nullptr;
static lv_obj_t* klipperMmuGateBox[MMU_GATE_MAX] = {};
static lv_obj_t* klipperMmuGateLabel[MMU_GATE_MAX] = {};
static uint8_t* btcDayChartCanvasBuf = nullptr;
static bool btcDayChartCanvasBufInPsram = false;
static lv_obj_t* weatherImage = nullptr;
static WeatherVisual lastWeatherVisual = WEATHER_VISUAL_UNKNOWN;
static bool weatherImageDrawn = false;
static int lastTimeSecondProgress = -1;

struct SunStatusIcon {
  lv_obj_t* root;
  SunStatusVisual lastVisual;
};

static SunStatusIcon timeSunIcon = {};

// --- Funktions-Prototypen fuer ausgelagerte Arduino-Tabs ---
void yieldFetchTask();
static uint32_t lvTickMillis();
const char* resetReasonName(esp_reset_reason_t reason);
void printBootDiagnostics();
void printDisplayDiagnostics();
void printRuntimeHealth();
bool getLocalTimeFast(struct tm& timeinfo);
bool configureTimeOnce();
void lvFlush(lv_display_t* disp, const lv_area_t* area, uint8_t* pxMap);
void setDisplayBrightness(uint8_t brightness);
int clampMinuteOfDay(int minute);
int localUtcOffsetMinutes(const struct tm& timeinfo);
bool calculateSunTimes(const struct tm& timeinfo, int& sunriseMinute, int& sunsetMinute);
int calculateSunStrength(const struct tm& timeinfo, int sunriseMinute, int sunsetMinute);
void updateBrightnessBySun();
lv_obj_t* createScreen();
lv_obj_t* createLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t color, lv_text_align_t align);
lv_obj_t* createAccent(lv_obj_t* parent, uint32_t color);
lv_obj_t* createDivider(lv_obj_t* parent, int x, int y, int w, uint32_t color);
void updateTimeSecondProgress(int second);
void setHidden(lv_obj_t* obj, bool hidden);
bool setLabelTextIfChanged(lv_obj_t* label, const char* text);
void setTimeNormalVisible(bool visible);
void styleFilledRect(lv_obj_t* obj, uint32_t color, int radius);
lv_obj_t* createSunRay(lv_obj_t* parent, int x, int y, int w, int h);
WeatherVisual weatherVisualFromCode(int code);
const lv_image_dsc_t* weatherImageForVisual(WeatherVisual visual);
int weatherVisualLeftInset(WeatherVisual visual);
int timeTextPixelWidth(const char* text);
void placeWeatherImage(const char* timeText);
lv_obj_t* createWeatherImage(lv_obj_t* parent);
void updateWeatherImage(int code);
void updateWeatherImagePositionForTime(const char* timeText);
SunStatusIcon createSunStatusIcon(lv_obj_t* parent);
const lv_image_dsc_t* sunStatusImageForVisual(SunStatusVisual visual);
SunStatusVisual sunStatusVisualFromTime(int nowMinute, int sunriseMinute, int sunsetMinute);
void updateSunStatusIcon(SunStatusIcon& icon, SunStatusVisual visual);
void updateSunStatusIcon(const struct tm& timeinfo);
void createTimeScreen();
void createCryptoScreen();
void createBtcDayScreen();
void createKlipperScreen();
void createBootScreen();
void createUi();
void refreshTimeUi();
void refreshCryptoUi();
String formatBtcCandleCountdown(uint32_t candleTime);
void refreshBtcDayUi();
int progressPercentFromText(const String& progress);
void refreshKlipperUi();
void refreshUi();
bool isKlipperScreenAvailable();
lv_obj_t* screenForState(ScreenState state);
ScreenState nextScreenState(ScreenState state);
void switchScreen(ScreenState nextScreen);
int priceToChartY(float price, float low, float high, int y, int h);
void canvasSetPixel(int x, int y, uint32_t color);
void canvasFillRect(int x, int y, int w, int h, uint32_t color);
void canvasHLine(int x, int y, int w, uint32_t color);
void canvasVLine(int x, int y, int h, uint32_t color);
void canvasDrawRect(int x, int y, int w, int h, uint32_t color);
void drawBtcDayChart();
void updateWifiState();
void beginWiFi();
bool checkInternetConnectivity();
String extractJsonNumber(const String& payload, const String& key);
String extractJsonString(const String& payload, const String& key);
String weatherStationNameForSource(JsonArrayConst sources, int sourceId);
int weatherCodeFromText(String text);
bool weatherHasRecentPrecipitation(JsonVariantConst weather);
int weatherPriorityCodeFromCondition(String text);
int weatherCodeFromBrightSky(JsonVariantConst weather, const String& iconText, const String& conditionText);
String cryptoProductId();
String cryptoPairTitle();
String cryptoDayTitle();
String cryptoPricePrefixText();
String cryptoSpotUrl();
String cryptoCandlesBaseUrl();
String cryptoOkStatus();
void configureSecureClient(WiFiClientSecure& client);
void formatQuoteCompact(float value, char* buffer, size_t bufferSize);
void updateBtcDayStatsLocked();
bool parseNextNumber(const String& payload, int& index, double& value);
void sortCandlesAscending(BtcCandle* candles, int count);
int parseCoinbaseCandles(const String& payload, BtcCandle* outCandles, int maxCandles);
static BtcCandle* allocateBtcCandleBuffer(const char* name, bool& inPsram);
bool initBtcStorage();
void storeBtcCandles(const BtcCandle* candles, int count);
void updateLiveCandleFromPrice(float price);
bool fetchWeatherValue();
String buildBrightSkyUrl();
bool fetchBtcPrice();
String readHttpPayloadChunked(HTTPClient& http, size_t reserveBytes);
String buildKlipperQueryUrl();
String urlEncodeQueryParam(const String& text);
String formatKlipperTemperature(const String& currentValue, const String& targetValue);
String formatKlipperProgress(const String& progressValue);
String formatKlipperDuration(const String& secondsValue);
String formatDurationClock(float seconds, bool forceHours);
String formatKlipperDurationProgress(const String& secondsValue, float estimatedSeconds);
String formatKlipperState(String state);
String formatKlippyConnectionState(String state);
String formatKlippyMessageForScreen(String message);
String formatKlipperDisplayMessage(String message);
String klippyOfflineDetail(const String& state, const String& message);
String formatKlipperFile(String filename);
uint32_t parseKlipperColor(String colorText);
String formatKlipperMmuInfo(int tool, int gate, const String& action, const String& operation, const String& filament);
String jsonStringValue(JsonVariantConst value);
String jsonNumberText(JsonVariantConst value);
int jsonIntValue(JsonVariantConst value, int fallback);
bool fetchKlipperFileMetadata(const String& filename, float& estimatedSeconds);
float klipperEstimatedSecondsForFile(const String& filename);
bool fetchKlipperPrinterName();
bool fetchKlipperServerInfo(String& klippyState, String& klippyMessage);
bool fetchKlipperPrinterInfo(String& klippyState, String& klippyMessage);
bool fetchKlipperStatus();
String formatUtcIsoTime(time_t value);
String buildBtcCandlesUrl();
bool fetchBtcCandles();
void fetchDataTask(void *pvParameters);
void initLvglDisplay();

unsigned long currentScreenInterval() {
  return currentScreen == SCREEN_TIME ? timeScreenInterval : defaultScreenInterval;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  printBootDiagnostics();

  dataMutex = xSemaphoreCreateMutex();
  if (dataMutex == NULL) {
    Serial.println("Mutex Init fehlgeschlagen!");
    while (true) {
      delay(1000);
    }
  }

  networkMutex = xSemaphoreCreateMutex();
  if (networkMutex == NULL) {
    Serial.println("Network Mutex Init fehlgeschlagen!");
    while (true) {
      delay(1000);
    }
  }

  if (!initBtcStorage()) {
    Serial.println("BTC Speicher Init fehlgeschlagen!");
    while (true) {
      delay(1000);
    }
  }

  if (!initBoardDisplay()) {
    Serial.println("ESP32-8048S043C Display Init fehlgeschlagen!");
    while (true) {
      delay(1000);
    }
  }
  setDisplayBrightness(dayBrightness);
  printDisplayDiagnostics();

  initLvglDisplay();
  createUi();
  lv_timer_handler();

  switchScreen(SCREEN_TIME);
  lastScreenSwitch = millis();

  beginWiFi();

  configureTimeOnce();

  BaseType_t fetchTaskCreated = xTaskCreatePinnedToCore(
    fetchDataTask,
    "FetchData",
    fetchTaskStackSize,
    NULL,
    fetchTaskPriority,
    &fetchTaskHandle,
    fetchTaskCore
  );
  if (fetchTaskCreated != pdPASS) {
    Serial.println("FetchData Task Init fehlgeschlagen!");
  } else {
    Serial.printf("FetchData Task gestartet: Core %d, Prio %u\n", (int)fetchTaskCore, (unsigned)fetchTaskPriority);
  }
}

void loop() {
  updateWifiState();

  unsigned long now = millis();
  if (now - lastUiRefresh >= uiRefreshInterval) {
    lastUiRefresh = now;
    refreshUi();
  }

  if (now - lastBrightnessRefresh >= brightnessRefreshInterval) {
    lastBrightnessRefresh = now;
    updateBrightnessBySun();
  }

  if (now - lastHealthLog >= healthLogInterval) {
    lastHealthLog = now;
    printRuntimeHealth();
  }

  if (currentScreen == SCREEN_KLIPPER && !isKlipperScreenAvailable()) {
    lastScreenSwitch = now;
    switchScreen(nextScreenState(currentScreen));
  }

  if (now - lastScreenSwitch > currentScreenInterval()) {
    lastScreenSwitch = now;
    switchScreen(nextScreenState(currentScreen));
  }

  lv_timer_handler();
  if (
    currentScreen == SCREEN_BTC_DAY &&
    now - lastScreenSwitch >= screenSwitchSettleInterval &&
    now - lastBtcChartDraw >= btcChartDrawInterval
  ) {
    lastBtcChartDraw = now;
    drawBtcDayChart();
  }
  delay(5);
}
