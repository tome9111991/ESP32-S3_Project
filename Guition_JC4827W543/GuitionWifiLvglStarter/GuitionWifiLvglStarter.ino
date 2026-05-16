#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <esp_heap_caps.h>

#define LV_CONF_INCLUDE_SIMPLE
#include <lvgl.h>

// LVGL v9 compatibility for LovyanGFX.
#ifndef LV_ATTRIBUTE_LARGE_CONST
  #define LV_ATTRIBUTE_LARGE_CONST
#endif

// Prevent LovyanGFX from defining old LVGL compatibility types.
#define M5GFX_USING_REAL_LVGL
#define M5GFX_LVGL_FONT_COMPAT_H
#define M5GFX_LVGL_COLOR_H
#define M5GFX_LVGL_AREA_H
#define M5GFX_LVGL_FONT_H
#define M5GFX_LVGL_DRAW_BUF_H
#define M5GFX_LVGL_FONT_FMT_TXT_H

#include <LovyanGFX.hpp>
#include <lgfx/v1/panel/Panel_NV3041A.hpp>

// Local secrets stay out of Git. Falls die Datei fehlt, laufen Platzhalterwerte.
#if __has_include("config_private.h")
  #include "config_private.h"
#else
  #include "config_private.example.h"
  #warning "config_private.h fehlt; Starter nutzt Platzhalter aus config_private.example.h."
#endif

#ifndef WIFI_SSID
  #error "WIFI_SSID fehlt. Kopiere config_private.example.h nach config_private.h und trage deine WLAN-Daten ein."
#endif
#ifndef WIFI_PASSWORD
  #error "WIFI_PASSWORD fehlt. Kopiere config_private.example.h nach config_private.h und trage deine WLAN-Daten ein."
#endif
#ifndef TIMEZONE_POSIX
  #define TIMEZONE_POSIX "CET-1CEST,M3.5.0,M10.5.0/3"
#endif
#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 2
#endif
#ifndef DISPLAY_BRIGHTNESS
  #define DISPLAY_BRIGHTNESS 160
#endif

#if (DISPLAY_ROTATION < 0) || (DISPLAY_ROTATION > 3)
  #error "DISPLAY_ROTATION muss 0, 1, 2 oder 3 sein."
#endif

static const int SCREEN_W = 480;
static const int SCREEN_H = 272;
static const char* ssid = WIFI_SSID;
static const char* password = WIFI_PASSWORD;
static const char* timezonePosix = TIMEZONE_POSIX;

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
      cfg.panel_width = SCREEN_W;
      cfg.panel_height = SCREEN_H;
      cfg.memory_width = SCREEN_W;
      cfg.memory_height = SCREEN_H;
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
static SemaphoreHandle_t networkMutex = NULL;

static volatile bool wifiConnected = false;
static bool timeConfigured = false;
static unsigned long lastWifiReconnectAttempt = 0;
static unsigned long lastUiRefresh = 0;

static const unsigned long wifiReconnectInterval = 10000;
static const unsigned long uiRefreshInterval = 1000;

static lv_obj_t* titleLabel = nullptr;
static lv_obj_t* wifiLabel = nullptr;
static lv_obj_t* ipLabel = nullptr;
static lv_obj_t* timeLabel = nullptr;
static lv_obj_t* heapLabel = nullptr;
static lv_obj_t* statusLabel = nullptr;

static uint32_t lvTickMillis();
void lvFlush(lv_display_t* disp, const lv_area_t* area, uint8_t* pxMap);
void initLvglDisplay();
void createUi();
void refreshUi();
void setStatusText(const char* text);
void setWifiText(const char* text);
void beginWiFi();
void updateWifiState();
bool configureTimeOnce();

void setup() {
  Serial.begin(115200);
  delay(200);

  networkMutex = xSemaphoreCreateMutex();
  if (networkMutex == NULL) {
    Serial.println("Network Mutex Init fehlgeschlagen!");
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
  display.setRotation(DISPLAY_ROTATION);
  display.setBrightness(DISPLAY_BRIGHTNESS);

  initLvglDisplay();
  createUi();
  lv_timer_handler();

  beginWiFi();
}

void loop() {
  updateWifiState();

  const unsigned long now = millis();
  if (now - lastUiRefresh >= uiRefreshInterval) {
    lastUiRefresh = now;
    refreshUi();
  }

  lv_timer_handler();
  delay(5);
}
