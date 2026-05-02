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
#include <esp_system.h>

#define LV_CONF_INCLUDE_SIMPLE
#include <lvgl.h>
#include "ui_assets.h"
#include "ui_font_time_digits.h"

// LVGL v9 compatibility for LovyanGFX
#ifndef LV_ATTRIBUTE_LARGE_CONST
  #define LV_ATTRIBUTE_LARGE_CONST
#endif

// Verhindere, dass LovyanGFX seine eigenen (veralteten) LVGL-Typen definiert
#define M5GFX_USING_REAL_LVGL
#define M5GFX_LVGL_FONT_COMPAT_H
#define M5GFX_LVGL_COLOR_H
#define M5GFX_LVGL_AREA_H
#define M5GFX_LVGL_FONT_H
#define M5GFX_LVGL_DRAW_BUF_H
#define M5GFX_LVGL_FONT_FMT_TXT_H

#include <LovyanGFX.hpp>
#include <lgfx/v1/panel/Panel_NV3041A.hpp>
#include <ArduinoJson.h>

#ifndef RGB565
  #define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#endif

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_NV3041A _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI3_HOST;
      cfg.spi_mode = 1;
      cfg.freq_write = 32000000UL;
      cfg.freq_read = 16000000UL;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 47;
      cfg.pin_io0 = 21;
      cfg.pin_io1 = 48;
      cfg.pin_io2 = 40;
      cfg.pin_io3 = 39;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }

    {
      auto cfg = _panel.config();
      cfg.pin_cs = 45;
      cfg.pin_rst = 4;
      cfg.pin_busy = -1;
      cfg.panel_width = 480;
      cfg.panel_height = 272;
      cfg.memory_width = 480;
      cfg.memory_height = 272;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = true;
      cfg.rgb_order = true;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;
      _panel.config(cfg);
    }

    {
      auto cfg = _light.config();
      cfg.pin_bl = 1;
      cfg.invert = false;
      _light.config(cfg);
      _panel.setLight(&_light);
    }

    setPanel(&_panel);
  }
};

static LGFX display;
static lv_display_t* lvDisplay = nullptr;
static lv_color_t* lvDrawBuf = nullptr;
static lv_color_t* lvDrawBuf2 = nullptr;
static bool lvDrawBufDma = false;

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
// Coinbase Crypto-Paar. Fuer SOL/EUR z.B. cryptoBaseSymbol="SOL", cryptoQuoteSymbol="EUR".
const char* cryptoBaseSymbol = CRYPTO_BASE_SYMBOL;
const char* cryptoQuoteSymbol = CRYPTO_QUOTE_SYMBOL;
const char* cryptoPricePrefix = CRYPTO_PRICE_PREFIX; // leer = automatisch: USD "$ ", EUR "EUR ", sonst Quote-Code
const char* cryptoServiceName = CRYPTO_SERVICE_NAME;
const char* klipperBaseUrl = KLIPPER_BASE_URL;
const unsigned long btcRefreshInterval = 60000; // 60 Sekunden
const unsigned long btcRetryInterval = 10000; // 10 Sekunden bei Fehlern
const unsigned long btcCandleRefreshInterval = 300000; // 5 Minuten
const unsigned long btcCandleRetryInterval = 60000; // 60 Sekunden bei Fehlern
const unsigned long weatherRefreshInterval = 300000; // 5 Minuten
const unsigned long weatherRetryInterval = 60000; // 60 Sekunden bei Fehlern
const unsigned long klipperRefreshInterval = 7000; // 7 Sekunden
const unsigned long klipperRetryInterval = 15000; // 15 Sekunden bei Fehlern
const unsigned long klipperNameRefreshInterval = 300000; // 5 Minuten
const unsigned long klipperNameRetryInterval = 60000; // 60 Sekunden bei Fehlern
const uint32_t BTC_CANDLE_SECONDS = 86400; // Tageskerzen
const int WEATHER_ICON_W = 56;
const int WEATHER_ICON_H = 48;
const int DIVIDER_H = 4;
const int TIME_SECOND_BAR_W = 256;
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
unsigned long lastWifiReconnectAttempt = 0;
const unsigned long wifiReconnectInterval = 10000;
unsigned long lastHealthLog = 0;
const unsigned long healthLogInterval = 60000;
const uint32_t fetchTaskStackSize = 16384;
const UBaseType_t fetchTaskPriority = 0;
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
const int BTC_CHART_W = 412;
const int BTC_CHART_H = 112;
const int BTC_CHART_CANVAS_H = 124;
const int BTC_CHART_PROGRESS_Y = 118;
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
const unsigned long brightnessRefreshInterval = 30000;
const uint8_t dayBrightness = 160;
const uint8_t nightBrightness = 8;
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
void printRuntimeHealth();
bool getLocalTimeFast(struct tm& timeinfo);
void configureTimeOnce();
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
lv_obj_t* createWeatherImage(lv_obj_t* parent);
void updateWeatherImage(int code);
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
bool connectWiFi(unsigned long timeoutMs);
String extractJsonNumber(const String& payload, const String& key);
String extractJsonString(const String& payload, const String& key);
int weatherCodeFromText(String text);
String cryptoProductId();
String cryptoPairTitle();
String cryptoDayTitle();
String cryptoPricePrefixText();
String cryptoSpotUrl();
String cryptoCandlesBaseUrl();
String cryptoOkStatus();
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

  if (!display.init()) {
    Serial.println("GFX Init fehlgeschlagen!");
  }
  display.initDMA();
  display.setColorDepth(16);
  display.invertDisplay(true);
  display.setRotation(2); // Anzeige um 180 Grad drehen
  setDisplayBrightness(dayBrightness);

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
  if (currentScreen == SCREEN_BTC_DAY && now - lastBtcChartDraw >= btcChartDrawInterval) {
    lastBtcChartDraw = now;
    drawBtcDayChart();
  }
  delay(5);
}
