#include <Arduino.h>
#include <esp_heap_caps.h>

#define LV_CONF_INCLUDE_SIMPLE
#include <lvgl.h>

#ifndef LV_ATTRIBUTE_LARGE_CONST
  #define LV_ATTRIBUTE_LARGE_CONST
#endif

#define M5GFX_USING_REAL_LVGL
#define M5GFX_LVGL_FONT_COMPAT_H
#define M5GFX_LVGL_COLOR_H
#define M5GFX_LVGL_AREA_H
#define M5GFX_LVGL_FONT_H
#define M5GFX_LVGL_DRAW_BUF_H
#define M5GFX_LVGL_FONT_FMT_TXT_H

#include <LovyanGFX.hpp>
#include <lgfx/v1/panel/Panel_NV3041A.hpp>

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

static const int SCREEN_W = 480;
static const int SCREEN_H = 272;
static const int MINOR_STEP = 10;
static const int MAJOR_STEP = 50;

static uint32_t lvTickMillis() {
  return millis();
}

void lvFlush(lv_display_t* disp, const lv_area_t* area, uint8_t* pxMap) {
  int32_t x1 = area->x1;
  int32_t y1 = area->y1;
  int32_t x2 = area->x2;
  int32_t y2 = area->y2;

  if (x2 < 0 || y2 < 0 || x1 >= display.width() || y1 >= display.height()) {
    lv_display_flush_ready(disp);
    return;
  }

  const int32_t srcWidth = lv_area_get_width(area);
  if (x1 < 0) x1 = 0;
  if (y1 < 0) y1 = 0;
  if (x2 >= display.width()) x2 = display.width() - 1;
  if (y2 >= display.height()) y2 = display.height() - 1;

  const uint32_t w = x2 - x1 + 1;
  const uint32_t h = y2 - y1 + 1;
  const lgfx::swap565_t* pixels = reinterpret_cast<const lgfx::swap565_t*>(pxMap)
    + ((y1 - area->y1) * srcWidth)
    + (x1 - area->x1);
  const bool contiguous = (srcWidth == (int32_t)w);
  const bool startedWrite = (display.getStartCount() == 0);

  display.waitDMA();

  if (startedWrite) {
    display.startWrite();
  }

  if (contiguous) {
    display.pushImage(x1, y1, w, h, pixels);
  } else {
    for (uint32_t row = 0; row < h; row++) {
      display.pushImage(x1, y1 + row, w, 1, pixels + (row * srcWidth));
    }
  }

  if (startedWrite) {
    display.endWrite();
  }

  display.waitDMA();
  lv_display_flush_ready(disp);
}

void initLvglDisplay() {
  lv_init();
  lv_tick_set_cb(lvTickMillis);

  const uint32_t bufferBytes = SCREEN_W * SCREEN_H * sizeof(lv_color_t);
  lvDrawBuf = (lv_color_t*)heap_caps_malloc(bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (lvDrawBuf == nullptr) {
    Serial.println("LVGL Fullscreen-Buffer in PSRAM fehlgeschlagen!");
    while (true) {
      delay(1000);
    }
  }

  Serial.printf("LVGL Buffer: %u Bytes, FULL, PSRAM\n", (unsigned)bufferBytes);

  lvDisplay = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(lvDisplay, lvFlush);
  lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565_SWAPPED);
  lv_display_set_buffers(
    lvDisplay,
    lvDrawBuf,
    nullptr,
    bufferBytes,
    LV_DISPLAY_RENDER_MODE_FULL
  );
}

lv_obj_t* createRect(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t color) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_bg_color(obj, color, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  return obj;
}

int clampInt(int value, int minValue, int maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

lv_obj_t* createLabel(lv_obj_t* parent, const char* text, lv_color_t color, const lv_font_t* font) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_style_bg_color(box, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_border_color(box, color, 0);
  lv_obj_set_style_pad_left(box, 5, 0);
  lv_obj_set_style_pad_right(box, 5, 0);
  lv_obj_set_style_pad_top(box, 2, 0);
  lv_obj_set_style_pad_bottom(box, 2, 0);

  lv_obj_t* label = lv_label_create(box);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_update_layout(label);

  lv_obj_set_size(box, lv_obj_get_width(label) + 10, lv_obj_get_height(label) + 4);
  lv_obj_center(label);
  return box;
}

void createXLabel(lv_obj_t* parent, const char* text, int x, lv_color_t color) {
  lv_obj_t* box = createLabel(parent, text, color, &lv_font_montserrat_14);
  lv_obj_update_layout(box);

  const int w = lv_obj_get_width(box);
  const int labelX = clampInt(x - (w / 2), 2, SCREEN_W - w - 2);
  const int labelY = 6;
  lv_obj_set_pos(box, labelX, labelY);

  const int markerX = clampInt(x - 1, 0, SCREEN_W - 3);
  createRect(parent, markerX, 0, 3, labelY + lv_obj_get_height(box) + 8, color);
  lv_obj_move_foreground(box);
}

void createYLabel(lv_obj_t* parent, const char* text, int y, lv_color_t color) {
  lv_obj_t* box = createLabel(parent, text, color, &lv_font_montserrat_14);
  lv_obj_update_layout(box);

  const int h = lv_obj_get_height(box);
  const int labelX = 6;
  const int labelY = clampInt(y - (h / 2), 2, SCREEN_H - h - 2);
  lv_obj_set_pos(box, labelX, labelY);

  const int markerY = clampInt(y - 1, 0, SCREEN_H - 3);
  createRect(parent, 0, markerY, labelX + lv_obj_get_width(box) + 8, 3, color);
  lv_obj_move_foreground(box);
}

void createPointLabel(lv_obj_t* parent, const char* text, int x, int y, int labelX, int labelY, lv_color_t color) {
  createRect(parent, x - 4, y - 1, 9, 3, color);
  createRect(parent, x - 1, y - 4, 3, 9, color);
  createRect(parent, min(x, labelX), y - 1, abs(labelX - x), 3, color);
  createRect(parent, labelX - 1, min(y, labelY), 3, abs(labelY - y), color);

  lv_obj_t* box = createLabel(parent, text, color, &lv_font_montserrat_18);
  lv_obj_set_pos(box, labelX, labelY);
}

void createGridUi() {
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  lv_obj_set_size(screen, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x07090d), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  const lv_color_t minorColor = lv_color_hex(0x1e242c);
  const lv_color_t majorColor = lv_color_hex(0x5c6876);
  const lv_color_t xAxisColor = lv_color_hex(0xff4040);
  const lv_color_t yAxisColor = lv_color_hex(0x2ee59d);
  const lv_color_t centerColor = lv_color_hex(0xffdc50);
  const lv_color_t edgeColor = lv_color_hex(0xffdc50);
  const lv_color_t textColor = lv_color_hex(0xffffff);

  for (int x = 0; x < SCREEN_W; x += MINOR_STEP) {
    createRect(screen, x, 0, x % MAJOR_STEP == 0 ? 2 : 1, SCREEN_H, x % MAJOR_STEP == 0 ? majorColor : minorColor);
  }

  for (int y = 0; y < SCREEN_H; y += MINOR_STEP) {
    createRect(screen, 0, y, SCREEN_W, y % MAJOR_STEP == 0 ? 2 : 1, y % MAJOR_STEP == 0 ? majorColor : minorColor);
  }

  createRect(screen, 0, 0, SCREEN_W, 2, edgeColor);
  createRect(screen, 0, SCREEN_H - 2, SCREEN_W, 2, edgeColor);
  createRect(screen, 0, 0, 2, SCREEN_H, edgeColor);
  createRect(screen, SCREEN_W - 2, 0, 2, SCREEN_H, edgeColor);

  createRect(screen, 0, 0, 25, 4, xAxisColor);
  createRect(screen, 0, 0, 4, 25, yAxisColor);
  createRect(screen, SCREEN_W - 28, 4, 28, 4, xAxisColor);
  createRect(screen, 4, SCREEN_H - 28, 4, 28, yAxisColor);

  lv_obj_t* originLabel = createLabel(screen, "0", edgeColor, &lv_font_montserrat_14);
  lv_obj_set_pos(originLabel, 5, 5);
  createXLabel(screen, "50", 50, xAxisColor);
  createXLabel(screen, "100", 100, xAxisColor);
  createXLabel(screen, "150", 150, xAxisColor);
  createXLabel(screen, "200", 200, xAxisColor);
  createXLabel(screen, "250", 250, xAxisColor);
  createXLabel(screen, "300", 300, xAxisColor);
  createXLabel(screen, "350", 350, xAxisColor);
  createXLabel(screen, "400", 400, xAxisColor);
  createXLabel(screen, "479", 479, xAxisColor);

  createYLabel(screen, "50", 50, yAxisColor);
  createYLabel(screen, "100", 100, yAxisColor);
  createYLabel(screen, "150", 150, yAxisColor);
  createYLabel(screen, "200", 200, yAxisColor);
  createYLabel(screen, "271", 271, yAxisColor);

  createPointLabel(screen, "240,136", 240, 136, 258, 150, centerColor);

  lv_screen_load(screen);
  lv_obj_invalidate(screen);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  if (!display.init()) {
    Serial.println("GFX Init fehlgeschlagen!");
  }

  display.initDMA();
  display.setColorDepth(16);
  display.invertDisplay(true);
  display.setRotation(2);
  display.setBrightness(160);

  initLvglDisplay();
  createGridUi();
  lv_timer_handler();

  Serial.println("DisplayPixelRaster gestartet: LVGL FULL Buffer in PSRAM, 480x272, Rotation 180");
}

void loop() {
  lv_timer_handler();
  delay(5);
}
