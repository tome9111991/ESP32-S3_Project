#include <Arduino.h>
#include <Wire.h>

#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static HardwareSerial &DebugSerial = Serial0;

// Direct esp_lcd RGB test for ESP32-8048S043C:
// 800x480 RGB565 panel with double framebuffer in the esp_lcd driver.

static constexpr int LCD_W = 800;
static constexpr int LCD_H = 480;
static constexpr int LCD_BL = 2;
static constexpr int LCD_PCLK_HZ = 16000000;
static constexpr int LCD_BOUNCE_LINES = 10;
static constexpr size_t LCD_FB_PIXELS = LCD_W * LCD_H;
static constexpr size_t LCD_FB_BYTES = LCD_FB_PIXELS * sizeof(uint16_t);

static constexpr uint32_t TOUCH_POLL_INTERVAL_MS = 15;
static constexpr uint32_t TOUCH_FRAME_INTERVAL_MS = 50;
static constexpr bool TOUCH_CALIBRATE_ON_BOOT = true;
static constexpr bool TOUCH_USE_SAVED_CALIBRATION = false;
static constexpr uint32_t TOUCH_CALIBRATION_TIMEOUT_MS = 15000;
static constexpr int TOUCH_CALIBRATION_MARGIN = 42;

static constexpr float TOUCH_CAL_X_RX = 1.0f;
static constexpr float TOUCH_CAL_X_RY = 0.0f;
static constexpr float TOUCH_CAL_X_C = 0.0f;
static constexpr float TOUCH_CAL_Y_RX = 0.0f;
static constexpr float TOUCH_CAL_Y_RY = 1.0f;
static constexpr float TOUCH_CAL_Y_C = 0.0f;

static constexpr int TOUCH_SDA = 19;
static constexpr int TOUCH_SCL = 20;
static constexpr int TOUCH_RST = 38;
static constexpr int TOUCH_INT = 18;

static constexpr uint16_t GT911_STATUS_REG = 0x814E;
static constexpr uint16_t GT911_POINT_REG = 0x814F;
static constexpr uint16_t GT911_PRODUCT_ID_REG = 0x8140;

static esp_lcd_panel_handle_t panel = nullptr;
static SemaphoreHandle_t color_done_sem = nullptr;
static uint16_t *draw_fb = nullptr;

static uint8_t touch_addr = 0;
static int last_touch_x = -1;
static int last_touch_y = -1;
static int last_raw_touch_x = -1;
static int last_raw_touch_y = -1;
static uint32_t touch_count = 0;
static uint32_t frame_count = 0;

struct TouchCalibration {
  bool valid;
  float x_rx;
  float x_ry;
  float x_c;
  float y_rx;
  float y_ry;
  float y_c;
};

struct TouchSample {
  int16_t raw_x;
  int16_t raw_y;
  int16_t screen_x;
  int16_t screen_y;
};

static TouchCalibration touch_cal = {};

static void loadSavedTouchCalibration()
{
  touch_cal.valid = true;
  touch_cal.x_rx = TOUCH_CAL_X_RX;
  touch_cal.x_ry = TOUCH_CAL_X_RY;
  touch_cal.x_c = TOUCH_CAL_X_C;
  touch_cal.y_rx = TOUCH_CAL_Y_RX;
  touch_cal.y_ry = TOUCH_CAL_Y_RY;
  touch_cal.y_c = TOUCH_CAL_Y_C;
  DebugSerial.println("Touch: using saved calibration constants.");
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static void setBacklight(bool on)
{
  digitalWrite(LCD_BL, on ? HIGH : LOW);
}

static bool IRAM_ATTR onColorTransferDone(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *)
{
  BaseType_t high_task_woken = pdFALSE;
  if (color_done_sem) {
    xSemaphoreGiveFromISR(color_done_sem, &high_task_woken);
  }
  return high_task_woken == pdTRUE;
}

static void putPixel(uint16_t *fb, int x, int y, uint16_t color)
{
  if ((unsigned)x >= LCD_W || (unsigned)y >= LCD_H) {
    return;
  }
  fb[(y * LCD_W) + x] = color;
}

static void fillRect(uint16_t *fb, int x, int y, int w, int h, uint16_t color)
{
  if (w <= 0 || h <= 0) {
    return;
  }
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > LCD_W) {
    w = LCD_W - x;
  }
  if (y + h > LCD_H) {
    h = LCD_H - y;
  }
  if (w <= 0 || h <= 0) {
    return;
  }

  uint16_t *row = fb + (y * LCD_W) + x;
  for (int j = 0; j < h; ++j) {
    for (int i = 0; i < w; ++i) {
      row[i] = color;
    }
    row += LCD_W;
  }
}

static void drawHLine(uint16_t *fb, int x, int y, int w, uint16_t color)
{
  fillRect(fb, x, y, w, 1, color);
}

static void drawVLine(uint16_t *fb, int x, int y, int h, uint16_t color)
{
  fillRect(fb, x, y, 1, h, color);
}

static void drawRect(uint16_t *fb, int x, int y, int w, int h, uint16_t color)
{
  drawHLine(fb, x, y, w, color);
  drawHLine(fb, x, y + h - 1, w, color);
  drawVLine(fb, x, y, h, color);
  drawVLine(fb, x + w - 1, y, h, color);
}

static void drawCircle(uint16_t *fb, int cx, int cy, int r, uint16_t color)
{
  int x = -r;
  int y = 0;
  int err = 2 - (2 * r);
  do {
    putPixel(fb, cx - x, cy + y, color);
    putPixel(fb, cx - y, cy - x, color);
    putPixel(fb, cx + x, cy - y, color);
    putPixel(fb, cx + y, cy + x, color);
    int old_err = err;
    if (old_err <= y) {
      err += (++y * 2) + 1;
    }
    if (old_err > x || err > y) {
      err += (++x * 2) + 1;
    }
  } while (x < 0);
}

static void drawCornerTarget(uint16_t *fb, int x, int y)
{
  const uint16_t white = rgb565(255, 255, 255);
  drawCircle(fb, x, y, 22, white);
  drawCircle(fb, x, y, 10, white);
  drawHLine(fb, x - 28, y, 57, white);
  drawVLine(fb, x, y - 28, 57, white);
}

static void drawTouchMarker(uint16_t *fb, int x, int y)
{
  const uint16_t red = rgb565(255, 0, 0);
  const uint16_t yellow = rgb565(255, 220, 0);
  drawCircle(fb, x, y, 20, red);
  drawCircle(fb, x, y, 8, yellow);
  drawHLine(fb, x - 34, y, 69, red);
  drawVLine(fb, x, y - 34, 69, red);
}

static void drawCalibrationScreen(int target_x, int target_y, int step)
{
  const uint16_t black = rgb565(0, 0, 0);
  const uint16_t white = rgb565(255, 255, 255);
  const uint16_t blue = rgb565(0, 170, 255);
  const uint16_t green = rgb565(0, 210, 110);
  const uint16_t grey = rgb565(70, 85, 100);

  fillRect(draw_fb, 0, 0, LCD_W, LCD_H, black);
  drawRect(draw_fb, 0, 0, LCD_W, LCD_H, white);

  const int progress_w = 120;
  fillRect(draw_fb, (LCD_W - progress_w) / 2, LCD_H / 2 - 5, progress_w, 10, grey);
  fillRect(draw_fb, (LCD_W - progress_w) / 2, LCD_H / 2 - 5, ((step + 1) * progress_w) / 4, 10, green);

  drawCircle(draw_fb, target_x, target_y, 32, blue);
  drawCircle(draw_fb, target_x, target_y, 18, white);
  drawCircle(draw_fb, target_x, target_y, 6, green);
  drawHLine(draw_fb, target_x - 44, target_y, 89, white);
  drawVLine(draw_fb, target_x, target_y - 44, 89, white);
}

static void drawScene(int touch_x, int touch_y, bool has_touch)
{
  const uint16_t black = rgb565(0, 0, 0);
  const uint16_t white = rgb565(255, 255, 255);
  const uint16_t dark = rgb565(18, 24, 31);
  const uint16_t header = rgb565(26, 33, 42);
  const uint16_t grid = rgb565(35, 50, 65);
  const uint16_t grey = rgb565(90, 110, 130);
  const uint16_t green = rgb565(0, 210, 110);

  fillRect(draw_fb, 0, 0, LCD_W, LCD_H, black);
  fillRect(draw_fb, 0, 0, LCD_W, 46, header);
  drawRect(draw_fb, 0, 0, LCD_W, LCD_H, white);

  const uint16_t bars[] = {
      rgb565(255, 0, 0), rgb565(0, 210, 0), rgb565(0, 70, 255), rgb565(0, 220, 220),
      rgb565(220, 0, 220), rgb565(255, 220, 0), white, black};
  const int bar_w = LCD_W / 8;
  for (int i = 0; i < 8; ++i) {
    fillRect(draw_fb, i * bar_w, 64, bar_w, 76, bars[i]);
    drawRect(draw_fb, i * bar_w, 64, bar_w, 76, grey);
  }

  for (int x = 0; x < LCD_W; x += 40) {
    drawVLine(draw_fb, x, 160, LCD_H - 160, grid);
  }
  for (int y = 160; y < LCD_H; y += 40) {
    drawHLine(draw_fb, 0, y, LCD_W, grid);
  }

  drawCornerTarget(draw_fb, 34, 184);
  drawCornerTarget(draw_fb, LCD_W - 35, 184);
  drawCornerTarget(draw_fb, 34, LCD_H - 35);
  drawCornerTarget(draw_fb, LCD_W - 35, LCD_H - 35);

  fillRect(draw_fb, 170, 178, 460, 84, rgb565(12, 18, 24));
  drawRect(draw_fb, 170, 178, 460, 84, grey);
  fillRect(draw_fb, 188, 204, has_touch ? 180 : 80, 18, has_touch ? green : grey);

  fillRect(draw_fb, 170, 280, 460, 44, black);
  drawRect(draw_fb, 170, 280, 460, 44, grey);
  if (has_touch) {
    int meter_w = 1 + (touch_count % 440);
    fillRect(draw_fb, 180, 294, meter_w, 16, green);
  }

  fillRect(draw_fb, 170, 360, 460, 18, dark);
  drawRect(draw_fb, 170, 360, 460, 18, grey);
  fillRect(draw_fb, 180, 365, 1 + (frame_count % 440), 8, rgb565(0, 170, 255));

  if (has_touch) {
    drawTouchMarker(draw_fb, touch_x, touch_y);
  }
}

static bool presentFrame()
{
  esp_err_t err = esp_cache_msync(
      draw_fb, LCD_FB_BYTES,
      ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  if (err != ESP_OK) {
    DebugSerial.printf("esp_cache_msync failed: 0x%X\n", err);
    return false;
  }

  xSemaphoreTake(color_done_sem, 0);
  err = esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_W, LCD_H, draw_fb);
  if (err != ESP_OK) {
    DebugSerial.printf("esp_lcd_panel_draw_bitmap failed: 0x%X\n", err);
    return false;
  }

  if (xSemaphoreTake(color_done_sem, pdMS_TO_TICKS(200)) != pdTRUE) {
    DebugSerial.println("draw_bitmap timeout waiting for color transfer");
  }
  frame_count++;
  return true;
}

static bool i2cWriteBytes(uint8_t addr, const uint8_t *data, size_t len)
{
  Wire.beginTransmission(addr);
  Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

static bool i2cReadReg(uint8_t addr, uint16_t reg, uint8_t *buf, size_t len)
{
  Wire.beginTransmission(addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  size_t got = Wire.requestFrom(addr, len, true);
  if (got != len) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }

  for (size_t i = 0; i < len; ++i) {
    buf[i] = Wire.read();
  }
  return true;
}

static void gt911ClearStatus()
{
  if (!touch_addr) {
    return;
  }

  const uint8_t clear_status[] = {
      (uint8_t)(GT911_STATUS_REG >> 8),
      (uint8_t)(GT911_STATUS_REG & 0xFF),
      0x00};
  i2cWriteBytes(touch_addr, clear_status, sizeof(clear_status));
}

static void gt911Reset()
{
  pinMode(TOUCH_INT, INPUT_PULLUP);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(20);
  digitalWrite(TOUCH_RST, HIGH);
  delay(120);
}

static uint8_t findGT911()
{
  const uint8_t candidates[] = {0x5D, 0x14};
  uint8_t id[4] = {};

  for (uint8_t addr : candidates) {
    if (i2cReadReg(addr, GT911_PRODUCT_ID_REG, id, sizeof(id))) {
      DebugSerial.printf("GT911 at 0x%02X, product id: %c%c%c%c\n",
                    addr, id[0], id[1], id[2], id[3]);
      return addr;
    }
  }

  return 0;
}

static int clampInt(int value, int low, int high)
{
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

static bool readTouchRaw(int16_t &x, int16_t &y)
{
  if (!touch_addr) {
    return false;
  }

  uint8_t status = 0;
  if (!i2cReadReg(touch_addr, GT911_STATUS_REG, &status, 1)) {
    return false;
  }
  if ((status & 0x80) == 0) {
    return false;
  }

  uint8_t points = status & 0x0F;
  if (points == 0 || points > 5) {
    gt911ClearStatus();
    return false;
  }

  uint8_t data[8] = {};
  bool ok = i2cReadReg(touch_addr, GT911_POINT_REG, data, sizeof(data));
  gt911ClearStatus();
  if (!ok) {
    return false;
  }

  x = data[1] | (data[2] << 8);
  y = data[3] | (data[4] << 8);
  return true;
}

static void applyTouchCalibration(int16_t raw_x, int16_t raw_y, int16_t &x, int16_t &y)
{
  if (touch_cal.valid) {
    float mapped_x = (touch_cal.x_rx * raw_x) + (touch_cal.x_ry * raw_y) + touch_cal.x_c;
    float mapped_y = (touch_cal.y_rx * raw_x) + (touch_cal.y_ry * raw_y) + touch_cal.y_c;
    x = clampInt((int)(mapped_x + 0.5f), 0, LCD_W - 1);
    y = clampInt((int)(mapped_y + 0.5f), 0, LCD_H - 1);
    return;
  }

  x = clampInt(raw_x, 0, LCD_W - 1);
  y = clampInt(raw_y, 0, LCD_H - 1);
}

static bool readTouch(int16_t &x, int16_t &y)
{
  int16_t raw_x = 0;
  int16_t raw_y = 0;
  if (!readTouchRaw(raw_x, raw_y)) {
    return false;
  }

  last_raw_touch_x = raw_x;
  last_raw_touch_y = raw_y;
  applyTouchCalibration(raw_x, raw_y, x, y);
  return true;
}

static bool waitForStableTouchRaw(int16_t &x, int16_t &y, uint32_t timeout_ms)
{
  const uint32_t start = millis();
  int32_t sum_x = 0;
  int32_t sum_y = 0;
  int samples = 0;

  while (millis() - start < timeout_ms) {
    int16_t raw_x = 0;
    int16_t raw_y = 0;
    if (readTouchRaw(raw_x, raw_y)) {
      sum_x += raw_x;
      sum_y += raw_y;
      samples++;
      if (samples >= 8) {
        x = sum_x / samples;
        y = sum_y / samples;
        return true;
      }
      delay(12);
    } else {
      delay(10);
    }
  }

  return false;
}

static void waitForTouchPause(uint32_t pause_ms)
{
  uint32_t quiet_since = millis();
  while (millis() - quiet_since < pause_ms) {
    int16_t raw_x = 0;
    int16_t raw_y = 0;
    if (readTouchRaw(raw_x, raw_y)) {
      quiet_since = millis();
    }
    delay(20);
  }
}

static bool computeTouchCalibration(const TouchSample *samples, TouchCalibration &cal)
{
  const TouchSample &p1 = samples[0];
  const TouchSample &p2 = samples[1];
  const TouchSample &p3 = samples[3];
  const float x1 = p1.raw_x;
  const float y1 = p1.raw_y;
  const float x2 = p2.raw_x;
  const float y2 = p2.raw_y;
  const float x3 = p3.raw_x;
  const float y3 = p3.raw_y;
  const float sx1 = p1.screen_x;
  const float sy1 = p1.screen_y;
  const float sx2 = p2.screen_x;
  const float sy2 = p2.screen_y;
  const float sx3 = p3.screen_x;
  const float sy3 = p3.screen_y;

  float det = (x1 * (y2 - y3)) + (x2 * (y3 - y1)) + (x3 * (y1 - y2));
  if (det > -1.0f && det < 1.0f) {
    return false;
  }

  cal.x_rx = ((sx1 * (y2 - y3)) + (sx2 * (y3 - y1)) + (sx3 * (y1 - y2))) / det;
  cal.x_ry = ((x1 * (sx2 - sx3)) + (x2 * (sx3 - sx1)) + (x3 * (sx1 - sx2))) / det;
  cal.x_c = ((x1 * ((y3 * sx2) - (y2 * sx3))) +
             (x2 * ((y1 * sx3) - (y3 * sx1))) +
             (x3 * ((y2 * sx1) - (y1 * sx2)))) /
            det;

  cal.y_rx = ((sy1 * (y2 - y3)) + (sy2 * (y3 - y1)) + (sy3 * (y1 - y2))) / det;
  cal.y_ry = ((x1 * (sy2 - sy3)) + (x2 * (sy3 - sy1)) + (x3 * (sy1 - sy2))) / det;
  cal.y_c = ((x1 * ((y3 * sy2) - (y2 * sy3))) +
             (x2 * ((y1 * sy3) - (y3 * sy1))) +
             (x3 * ((y2 * sy1) - (y1 * sy2)))) /
            det;

  cal.valid = true;
  return true;
}

static void printTouchCalibrationForCopy()
{
  DebugSerial.println();
  DebugSerial.println("Copy these calibration constants to another project:");
  DebugSerial.println("static constexpr bool TOUCH_USE_SAVED_CALIBRATION = true;");
  DebugSerial.printf("static constexpr float TOUCH_CAL_X_RX = %.8ff;\n", touch_cal.x_rx);
  DebugSerial.printf("static constexpr float TOUCH_CAL_X_RY = %.8ff;\n", touch_cal.x_ry);
  DebugSerial.printf("static constexpr float TOUCH_CAL_X_C = %.8ff;\n", touch_cal.x_c);
  DebugSerial.printf("static constexpr float TOUCH_CAL_Y_RX = %.8ff;\n", touch_cal.y_rx);
  DebugSerial.printf("static constexpr float TOUCH_CAL_Y_RY = %.8ff;\n", touch_cal.y_ry);
  DebugSerial.printf("static constexpr float TOUCH_CAL_Y_C = %.8ff;\n", touch_cal.y_c);
  DebugSerial.println();
  DebugSerial.println("Use this mapping after reading raw GT911 x/y:");
  DebugSerial.println("screen_x = constrain((int)((TOUCH_CAL_X_RX * raw_x) + (TOUCH_CAL_X_RY * raw_y) + TOUCH_CAL_X_C + 0.5f), 0, LCD_W - 1);");
  DebugSerial.println("screen_y = constrain((int)((TOUCH_CAL_Y_RX * raw_x) + (TOUCH_CAL_Y_RY * raw_y) + TOUCH_CAL_Y_C + 0.5f), 0, LCD_H - 1);");
  DebugSerial.println();
}

static bool calibrateTouch()
{
  TouchSample samples[4] = {
      {0, 0, TOUCH_CALIBRATION_MARGIN, TOUCH_CALIBRATION_MARGIN},
      {0, 0, LCD_W - TOUCH_CALIBRATION_MARGIN - 1, TOUCH_CALIBRATION_MARGIN},
      {0, 0, LCD_W - TOUCH_CALIBRATION_MARGIN - 1, LCD_H - TOUCH_CALIBRATION_MARGIN - 1},
      {0, 0, TOUCH_CALIBRATION_MARGIN, LCD_H - TOUCH_CALIBRATION_MARGIN - 1},
  };

  DebugSerial.println("Touch calibration: hold each of the 4 targets until the next target appears.");
  waitForTouchPause(300);

  for (int i = 0; i < 4; ++i) {
    drawCalibrationScreen(samples[i].screen_x, samples[i].screen_y, i);
    presentFrame();
    DebugSerial.printf("Calibration target %d/4 at screen x=%d y=%d\n",
                  i + 1, samples[i].screen_x, samples[i].screen_y);

    if (!waitForStableTouchRaw(samples[i].raw_x, samples[i].raw_y, TOUCH_CALIBRATION_TIMEOUT_MS)) {
      DebugSerial.println("Touch calibration timeout, using raw touch coordinates.");
      touch_cal.valid = false;
      return false;
    }

    DebugSerial.printf("  raw x=%d y=%d\n", samples[i].raw_x, samples[i].raw_y);
    drawCalibrationScreen(samples[i].screen_x, samples[i].screen_y, i);
    drawTouchMarker(draw_fb, samples[i].screen_x, samples[i].screen_y);
    presentFrame();
    waitForTouchPause(300);
  }

  if (!computeTouchCalibration(samples, touch_cal)) {
    DebugSerial.println("Touch calibration failed, using raw touch coordinates.");
    touch_cal.valid = false;
    return false;
  }

  DebugSerial.println("Touch calibration OK.");
  DebugSerial.printf("  x = %.6f * raw_x + %.6f * raw_y + %.2f\n", touch_cal.x_rx, touch_cal.x_ry, touch_cal.x_c);
  DebugSerial.printf("  y = %.6f * raw_x + %.6f * raw_y + %.2f\n", touch_cal.y_rx, touch_cal.y_ry, touch_cal.y_c);
  printTouchCalibrationForCopy();
  return true;
}

static bool initDisplay()
{
  color_done_sem = xSemaphoreCreateBinary();
  if (!color_done_sem) {
    DebugSerial.println("xSemaphoreCreateBinary failed");
    return false;
  }

  draw_fb = (uint16_t *)heap_caps_malloc(LCD_FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!draw_fb) {
    DebugSerial.println("draw_fb allocation failed");
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
  cfg.flags.double_fb = true;
  cfg.flags.no_fb = false;
  cfg.flags.bb_invalidate_cache = false;

  esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &panel);
  if (err != ESP_OK) {
    DebugSerial.printf("esp_lcd_new_rgb_panel failed: 0x%X\n", err);
    return false;
  }

  esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
  callbacks.on_color_trans_done = onColorTransferDone;
  err = esp_lcd_rgb_panel_register_event_callbacks(panel, &callbacks, nullptr);
  if (err != ESP_OK) {
    DebugSerial.printf("register callbacks failed: 0x%X\n", err);
    return false;
  }

  err = esp_lcd_panel_reset(panel);
  if (err != ESP_OK) {
    DebugSerial.printf("esp_lcd_panel_reset failed: 0x%X\n", err);
    return false;
  }
  err = esp_lcd_panel_init(panel);
  if (err != ESP_OK) {
    DebugSerial.printf("esp_lcd_panel_init failed: 0x%X\n", err);
    return false;
  }
  esp_lcd_panel_disp_on_off(panel, true);
  return true;
}

void setup()
{
  DebugSerial.begin(115200);
  delay(300);
  DebugSerial.println();
  DebugSerial.println("ESP32-8048S043C esp_lcd double framebuffer test");

  pinMode(LCD_BL, OUTPUT);
  setBacklight(true);

  DebugSerial.printf("PSRAM free before display init: %u\n", ESP.getFreePsram());
  if (!initDisplay()) {
    DebugSerial.println("Display init failed");
    while (true) {
      setBacklight(true);
      delay(250);
      setBacklight(false);
      delay(250);
    }
  }
  DebugSerial.printf("PSRAM free after display init: %u\n", ESP.getFreePsram());
  DebugSerial.printf("RGB: %dx%d, PCLK %d MHz, num_fbs=2, bounce_lines=%d\n",
                LCD_W, LCD_H, LCD_PCLK_HZ / 1000000, LCD_BOUNCE_LINES);

  drawScene(0, 0, false);
  presentFrame();

  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  gt911Reset();
  touch_addr = findGT911();
  DebugSerial.printf("Touch: %s\n", touch_addr ? "OK" : "not found");
  if (TOUCH_USE_SAVED_CALIBRATION && touch_addr) {
    loadSavedTouchCalibration();
  } else if (TOUCH_CALIBRATE_ON_BOOT && touch_addr) {
    calibrateTouch();
  }

  drawScene(0, 0, false);
  presentFrame();
}

void loop()
{
  static uint32_t last_touch_poll = 0;
  static uint32_t last_frame = 0;
  uint32_t now = millis();

  if (now - last_touch_poll >= TOUCH_POLL_INTERVAL_MS) {
    last_touch_poll = now;
    int16_t x = 0;
    int16_t y = 0;
    if (readTouch(x, y)) {
      int dx = abs((int)x - last_touch_x);
      int dy = abs((int)y - last_touch_y);
      if ((now - last_frame >= TOUCH_FRAME_INTERVAL_MS) && (last_touch_x < 0 || dx > 2 || dy > 2)) {
        last_frame = now;
        last_touch_x = x;
        last_touch_y = y;
        touch_count++;
        DebugSerial.printf("Touch #%lu: raw=%d,%d mapped=%d,%d frame=%lu\n",
                      touch_count, last_raw_touch_x, last_raw_touch_y, x, y, frame_count + 1);
        drawScene(x, y, true);
        presentFrame();
      }
    }
  }

  delay(2);
}
