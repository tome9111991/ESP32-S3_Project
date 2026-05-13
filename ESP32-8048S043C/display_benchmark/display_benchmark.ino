#include <Arduino.h>
#include <Wire.h>

#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// ESP32-8048S043C RGB display stress benchmark.
//
// The run matrix below is executed automatically. Watch the panel for flicker,
// tearing, shifted lines, snow, or corrupted color blocks while the serial log
// prints timing and timeout statistics for every run.

static constexpr int LCD_W = 800;
static constexpr int LCD_H = 480;
static constexpr int LCD_BL = 2;
static constexpr int LCD_HSYNC_PULSE = 4;
static constexpr int LCD_HSYNC_BACK_PORCH = 16;
static constexpr int LCD_HSYNC_FRONT_PORCH = 8;
static constexpr int LCD_VSYNC_PULSE = 4;
static constexpr int LCD_VSYNC_BACK_PORCH = 4;
static constexpr int LCD_VSYNC_FRONT_PORCH = 4;
static constexpr int TOUCH_SDA = 19;
static constexpr int TOUCH_SCL = 20;
static constexpr int TOUCH_RST = 38;
static constexpr int TOUCH_INT = 18;

static constexpr uint16_t GT911_STATUS_REG = 0x814E;
static constexpr uint16_t GT911_POINT_REG = 0x814F;
static constexpr uint16_t GT911_PRODUCT_ID_REG = 0x8140;

static constexpr float TOUCH_CAL_X_RX = 1.65867031f;
static constexpr float TOUCH_CAL_X_RY = -0.02261823f;
static constexpr float TOUCH_CAL_X_C = 2.12817001f;
static constexpr float TOUCH_CAL_Y_RX = 0.02082564f;
static constexpr float TOUCH_CAL_Y_RY = 1.79517055f;
static constexpr float TOUCH_CAL_Y_C = 10.62223816f;

static constexpr uint32_t BENCH_PHASE_MS = 10000;
static constexpr uint32_t PSRAM_BENCH_MS = 1000;
static constexpr bool BENCH_REQUIRE_VISUAL_CONFIRM = true;
static constexpr uint32_t TOUCH_CONFIRM_ARM_MS = 500;
static constexpr uint32_t TOUCH_CONFIRM_STABLE_MS = 180;
static constexpr uint32_t TOUCH_CONFIRM_POLL_MS = 25;
static constexpr int VISUAL_BUTTON_MARGIN = 36;
static constexpr int VISUAL_BUTTON_GAP = 20;
static constexpr int VISUAL_BUTTON_Y = 145;
static constexpr int VISUAL_BUTTON_H = 190;
static constexpr bool BENCH_STRESS_CORE_1 = true;
static constexpr int STRESS_WORK_CHUNK = 4096;
static constexpr TickType_t STRESS_IDLE_TICKS = 1;

static constexpr uint32_t STRESS_CPU = 0x01;
static constexpr uint32_t STRESS_PSRAM_RMW = 0x02;
static constexpr uint32_t STRESS_PSRAM_READ = 0x04;
static constexpr uint32_t STRESS_PSRAM_WRITE = 0x08;

static constexpr size_t LCD_FB_PIXELS = LCD_W * LCD_H;
static constexpr size_t LCD_FB_BYTES = LCD_FB_PIXELS * sizeof(uint16_t);
static constexpr size_t STRESS_BUF_BYTES = 512 * 1024;

static HardwareSerial &DebugSerial = Serial0;

static esp_lcd_panel_handle_t panel = nullptr;
static SemaphoreHandle_t color_done_sem = nullptr;
static uint16_t *draw_fb = nullptr;
static uint32_t *stress_buf = nullptr;
static uint8_t touch_addr = 0;

static volatile uint32_t stress_mode = 0;
static volatile uint32_t stress_counter0 = 0;
static volatile uint32_t stress_counter1 = 0;
static volatile uint32_t psram_bench_sink = 0;

static uint32_t frame_count = 0;
static uint32_t phase_frame_count = 0;
static uint32_t timeout_count = 0;
static uint32_t draw_us_sum = 0;
static uint32_t draw_us_max = 0;
static uint32_t present_us_sum = 0;
static uint32_t present_us_max = 0;
static uint32_t msync_us_sum = 0;
static uint32_t msync_us_max = 0;
static uint32_t submit_us_sum = 0;
static uint32_t submit_us_max = 0;
static uint32_t wait_us_sum = 0;
static uint32_t wait_us_max = 0;
static uint32_t phase_start_ms = 0;
static uint32_t phase_start_stress_counter0 = 0;
static uint32_t phase_start_stress_counter1 = 0;
static uint32_t run_start_ms = 0;
static uint32_t run_frame_count = 0;
static uint32_t run_timeout_total = 0;
static uint32_t run_draw_us_max = 0;
static uint32_t run_present_us_max = 0;
static uint32_t run_msync_us_max = 0;
static uint32_t run_submit_us_max = 0;
static uint32_t run_wait_us_max = 0;
static uint64_t run_fb_write_min_bytes = 0;
static uint64_t run_psram_stress_bytes = 0;
static uint32_t run_measured_phase_ms = 0;
static uint8_t phase_index = 0;
static uint8_t run_index = 0;
static bool benchmark_done = false;
static bool visual_confirm_all_good = false;

struct BenchPhase {
  const char *name;
  uint32_t stress;
};

struct BenchRun {
  const char *id;
  uint32_t pclk_hz;
  int bounce_lines;
  int num_fbs;
  bool double_fb;
  bool fb_in_psram;
  bool bb_invalidate_cache;
};

static const BenchRun BENCH_RUNS[] = {
    {"A", 18000000, 12, 2, true, true, false},
    {"B", 16000000, 10, 2, true, true, false},
    {"C", 14000000, 10, 2, true, true, false},
    {"D", 14000000, 8, 2, true, true, false},
    {"E", 14000000, 4, 2, true, true, false},
    {"F", 14000000, 0, 2, true, true, false},
    {"G", 14000000, 16, 2, true, true, false},
    {"H", 14000000, 10, 2, false, true, false},
    {"I", 14000000, 10, 2, true, true, true},
};

static const BenchPhase BENCH_PHASES[] = {
    {"baseline full redraw", 0},
    {"cpu load", STRESS_CPU},
    {"psram read load", STRESS_PSRAM_READ},
    {"psram write load", STRESS_PSRAM_WRITE},
    {"psram read_modify_write load", STRESS_PSRAM_RMW},
};

static constexpr uint8_t RUN_COUNT = sizeof(BENCH_RUNS) / sizeof(BENCH_RUNS[0]);
static constexpr uint8_t PHASE_COUNT = sizeof(BENCH_PHASES) / sizeof(BENCH_PHASES[0]);

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static float bytesToMbS(uint64_t bytes, uint32_t elapsed_ms)
{
  if (elapsed_ms == 0) {
    return 0.0f;
  }
  return ((float)bytes / 1000000.0f) / ((float)elapsed_ms / 1000.0f);
}

static bool stressUsesPsram(uint32_t mode)
{
  return (mode & (STRESS_PSRAM_RMW | STRESS_PSRAM_READ | STRESS_PSRAM_WRITE)) != 0;
}

static uint32_t psramBytesPerStressIter(uint32_t mode)
{
  uint32_t bytes = 0;
  if (mode & STRESS_PSRAM_RMW) {
    bytes += 12;
  }
  if (mode & STRESS_PSRAM_READ) {
    bytes += 4;
  }
  if (mode & STRESS_PSRAM_WRITE) {
    bytes += 4;
  }
  return bytes;
}

static float usPercentOfElapsed(uint32_t part_us, uint32_t elapsed_ms)
{
  if (elapsed_ms == 0) {
    return 0.0f;
  }
  return ((float)part_us * 100.0f) / ((float)elapsed_ms * 1000.0f);
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

static uint32_t lcdTotalPixelsPerRefresh()
{
  const uint32_t total_w = LCD_W + LCD_HSYNC_PULSE + LCD_HSYNC_BACK_PORCH + LCD_HSYNC_FRONT_PORCH;
  const uint32_t total_h = LCD_H + LCD_VSYNC_PULSE + LCD_VSYNC_BACK_PORCH + LCD_VSYNC_FRONT_PORCH;
  return total_w * total_h;
}

static float panelRefreshHz(const BenchRun &run)
{
  return (float)run.pclk_hz / (float)lcdTotalPixelsPerRefresh();
}

static float panelPsramReadMbS(const BenchRun &run)
{
  if (!run.fb_in_psram) {
    return 0.0f;
  }
  return (panelRefreshHz(run) * (float)LCD_FB_BYTES) / 1000000.0f;
}

static uint16_t runMarkerColor(uint8_t index)
{
  static const uint16_t colors[] = {
      rgb565(255, 0, 0),
      rgb565(0, 220, 80),
      rgb565(0, 80, 255),
      rgb565(255, 220, 0),
      rgb565(220, 0, 220),
      rgb565(0, 220, 220),
      rgb565(255, 255, 255),
  };
  return colors[index % (sizeof(colors) / sizeof(colors[0]))];
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
  for (int yy = 0; yy < h; ++yy) {
    for (int xx = 0; xx < w; ++xx) {
      row[xx] = color;
    }
    row += LCD_W;
  }
}

static void drawDiagnosticOverlay(uint32_t frame)
{
  const uint16_t black = rgb565(0, 0, 0);
  const uint16_t white = rgb565(255, 255, 255);
  const uint16_t red = rgb565(255, 0, 0);
  const uint16_t green = rgb565(0, 220, 80);
  const uint16_t blue = rgb565(0, 80, 255);
  const uint16_t yellow = rgb565(255, 220, 0);
  const uint16_t run_color = runMarkerColor(run_index);
  const uint16_t phase_color = runMarkerColor(phase_index + 2);

  // Feste Marker zeigen sofort, ob Run und Phase optisch korrekt ankommen.
  fillRect(draw_fb, 0, 0, LCD_W, 8, run_color);
  fillRect(draw_fb, 0, LCD_H - 8, LCD_W, 8, run_color);
  fillRect(draw_fb, 0, 0, 8, LCD_H, run_color);
  fillRect(draw_fb, LCD_W - 8, 0, 8, LCD_H, run_color);

  for (uint8_t i = 0; i <= run_index; ++i) {
    fillRect(draw_fb, 20 + (i * 24), 112, 16, 32, run_color);
    fillRect(draw_fb, 20 + (i * 24), 148, 16, 8, white);
  }
  for (uint8_t i = 0; i <= phase_index; ++i) {
    fillRect(draw_fb, LCD_W - 44 - (i * 28), 112, 20, 44, phase_color);
  }

  fillRect(draw_fb, 22, 174, 96, 96, black);
  for (int y = 0; y < 96; y += 8) {
    for (int x = 0; x < 96; x += 8) {
      if (((x + y) / 8) & 1) {
        fillRect(draw_fb, 22 + x, 174 + y, 8, 8, white);
      }
    }
  }

  for (int x = 150; x < 330; ++x) {
    uint16_t color = ((x + frame) % 3 == 0) ? red : (((x + frame) % 3 == 1) ? green : blue);
    fillRect(draw_fb, x, 176, 1, 88, color);
  }
  for (int y = 286; y < 390; ++y) {
    uint16_t color = ((y + frame) & 1) ? white : black;
    fillRect(draw_fb, 150, y, 180, 1, color);
  }

  fillRect(draw_fb, LCD_W - 118, 174, 28, 28, red);
  fillRect(draw_fb, LCD_W - 82, 174, 28, 28, green);
  fillRect(draw_fb, LCD_W - 46, 174, 28, 28, blue);
  fillRect(draw_fb, LCD_W - 118, 210, 100, 20, yellow);
}

static void drawMovingPattern(uint32_t frame)
{
  const uint16_t black = rgb565(0, 0, 0);
  const uint16_t white = rgb565(255, 255, 255);
  const uint16_t red = rgb565(255, 0, 0);
  const uint16_t green = rgb565(0, 220, 80);
  const uint16_t blue = rgb565(0, 80, 255);
  const uint16_t cyan = rgb565(0, 220, 220);
  const uint16_t magenta = rgb565(220, 0, 220);
  const uint16_t yellow = rgb565(255, 220, 0);
  const uint16_t grey = rgb565(70, 80, 90);

  const uint16_t bars[] = {red, green, blue, cyan, magenta, yellow, white, black};
  const int bar_w = LCD_W / 8;

  for (int y = 0; y < LCD_H; ++y) {
    uint16_t *row = draw_fb + (y * LCD_W);
    uint16_t a = ((y + frame) & 1) ? rgb565(16, 16, 16) : rgb565(38, 38, 38);
    uint16_t b = ((y + frame) & 1) ? rgb565(38, 38, 38) : rgb565(16, 16, 16);
    for (int x = 0; x < LCD_W; ++x) {
      row[x] = ((x + y + frame) & 1) ? a : b;
    }
  }

  for (int i = 0; i < 8; ++i) {
    fillRect(draw_fb, i * bar_w, 0, bar_w, 92, bars[(i + frame / 9) & 7]);
  }

  const int moving_x = (frame * 7) % LCD_W;
  const int moving_y = 110 + ((frame * 5) % (LCD_H - 140));
  fillRect(draw_fb, moving_x - 3, 96, 7, LCD_H - 96, white);
  fillRect(draw_fb, 0, moving_y - 3, LCD_W, 7, yellow);

  for (int i = 0; i < 18; ++i) {
    int x = (int)((frame * (11 + i)) + (i * 53)) % (LCD_W - 80);
    int y = 112 + ((int)((frame * (7 + i)) + (i * 29)) % (LCD_H - 180));
    uint16_t color = bars[(i + frame / 5) & 7];
    fillRect(draw_fb, x, y, 42 + (i * 3), 18 + (i & 7), color);
  }

  fillRect(draw_fb, 0, LCD_H - 32, LCD_W, 32, grey);
  fillRect(draw_fb, 0, LCD_H - 30, (frame * 13) % LCD_W, 28, green);
  fillRect(draw_fb, LCD_W - 26, 0, 26, LCD_H, (frame & 1) ? red : blue);
  drawDiagnosticOverlay(frame);
}

static void drawVisualPromptScreen()
{
  const uint16_t black = rgb565(0, 0, 0);
  const uint16_t white = rgb565(255, 255, 255);
  const uint16_t green = rgb565(0, 180, 70);
  const uint16_t red = rgb565(220, 20, 20);
  const uint16_t yellow = rgb565(230, 190, 0);
  const uint16_t grey = rgb565(50, 55, 65);
  const uint16_t run_color = runMarkerColor(run_index);

  fillRect(draw_fb, 0, 0, LCD_W, LCD_H, black);
  fillRect(draw_fb, 0, 0, LCD_W, 18, run_color);
  fillRect(draw_fb, 0, LCD_H - 18, LCD_W, 18, run_color);

  const int margin = VISUAL_BUTTON_MARGIN;
  const int gap = VISUAL_BUTTON_GAP;
  const int button_w = (LCD_W - (2 * margin) - (2 * gap)) / 3;
  const int button_h = VISUAL_BUTTON_H;
  const int button_y = VISUAL_BUTTON_Y;

  fillRect(draw_fb, margin, button_y, button_w, button_h, green);
  fillRect(draw_fb, margin + button_w + gap, button_y, button_w, button_h, yellow);
  fillRect(draw_fb, margin + (2 * (button_w + gap)), button_y, button_w, button_h, red);

  fillRect(draw_fb, 60, 55, LCD_W - 120, 54, grey);
  fillRect(draw_fb, 82 + (run_index * 26), 68, 18, 28, white);

  // Simple letter markers: G, S, B as block shapes.
  fillRect(draw_fb, margin + 52, button_y + 48, 90, 18, white);
  fillRect(draw_fb, margin + 52, button_y + 48, 18, 96, white);
  fillRect(draw_fb, margin + 52, button_y + 126, 90, 18, white);
  fillRect(draw_fb, margin + 124, button_y + 92, 18, 52, white);
  fillRect(draw_fb, margin + 92, button_y + 92, 50, 18, white);

  const int sx = margin + button_w + gap + 54;
  fillRect(draw_fb, sx, button_y + 48, 90, 18, white);
  fillRect(draw_fb, sx, button_y + 48, 18, 48, white);
  fillRect(draw_fb, sx, button_y + 87, 90, 18, white);
  fillRect(draw_fb, sx + 72, button_y + 87, 18, 57, white);
  fillRect(draw_fb, sx, button_y + 126, 90, 18, white);

  const int bx = margin + (2 * (button_w + gap)) + 54;
  fillRect(draw_fb, bx, button_y + 48, 18, 96, white);
  fillRect(draw_fb, bx, button_y + 48, 72, 18, white);
  fillRect(draw_fb, bx, button_y + 87, 80, 18, white);
  fillRect(draw_fb, bx, button_y + 126, 72, 18, white);
  fillRect(draw_fb, bx + 72, button_y + 66, 18, 21, white);
  fillRect(draw_fb, bx + 72, button_y + 105, 18, 21, white);
}

static bool presentFrame()
{
  uint32_t start = micros();
  uint32_t msync_start = start;
  esp_err_t err = esp_cache_msync(
      draw_fb,
      LCD_FB_BYTES,
      ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  uint32_t msync_elapsed = micros() - msync_start;
  if (err != ESP_OK) {
    DebugSerial.printf("esp_cache_msync failed: 0x%X\n", err);
    return false;
  }

  xSemaphoreTake(color_done_sem, 0);
  uint32_t submit_start = micros();
  err = esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_W, LCD_H, draw_fb);
  uint32_t submit_elapsed = micros() - submit_start;
  if (err != ESP_OK) {
    DebugSerial.printf("esp_lcd_panel_draw_bitmap failed: 0x%X\n", err);
    return false;
  }

  uint32_t wait_start = micros();
  if (xSemaphoreTake(color_done_sem, pdMS_TO_TICKS(250)) != pdTRUE) {
    timeout_count++;
  }
  uint32_t wait_elapsed = micros() - wait_start;

  uint32_t elapsed = micros() - start;
  present_us_sum += elapsed;
  if (elapsed > present_us_max) {
    present_us_max = elapsed;
  }
  msync_us_sum += msync_elapsed;
  if (msync_elapsed > msync_us_max) {
    msync_us_max = msync_elapsed;
  }
  submit_us_sum += submit_elapsed;
  if (submit_elapsed > submit_us_max) {
    submit_us_max = submit_elapsed;
  }
  wait_us_sum += wait_elapsed;
  if (wait_elapsed > wait_us_max) {
    wait_us_max = wait_elapsed;
  }

  frame_count++;
  phase_frame_count++;
  run_frame_count++;
  return true;
}

static bool initBenchmarkBuffers()
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
      DebugSerial.printf("GT911 at 0x%02X, product id: %c%c%c%c\n", addr, id[0], id[1], id[2], id[3]);
      return addr;
    }
  }

  return 0;
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

static bool readTouch(int16_t &x, int16_t &y)
{
  int16_t raw_x = 0;
  int16_t raw_y = 0;
  if (!readTouchRaw(raw_x, raw_y)) {
    return false;
  }

  float mapped_x = (TOUCH_CAL_X_RX * raw_x) + (TOUCH_CAL_X_RY * raw_y) + TOUCH_CAL_X_C;
  float mapped_y = (TOUCH_CAL_Y_RX * raw_x) + (TOUCH_CAL_Y_RY * raw_y) + TOUCH_CAL_Y_C;
  x = clampInt((int)(mapped_x + 0.5f), 0, LCD_W - 1);
  y = clampInt((int)(mapped_y + 0.5f), 0, LCD_H - 1);
  return true;
}

static char visualResultFromTouch(int16_t x, int16_t y)
{
  const int button_w = (LCD_W - (2 * VISUAL_BUTTON_MARGIN) - (2 * VISUAL_BUTTON_GAP)) / 3;
  const int good_x1 = VISUAL_BUTTON_MARGIN;
  const int skip_x1 = VISUAL_BUTTON_MARGIN + button_w + VISUAL_BUTTON_GAP;
  const int bad_x1 = VISUAL_BUTTON_MARGIN + (2 * (button_w + VISUAL_BUTTON_GAP));
  const int y1 = VISUAL_BUTTON_Y;
  const int y2 = VISUAL_BUTTON_Y + VISUAL_BUTTON_H;

  if (y < y1 || y > y2) {
    return 0;
  }
  if (x >= good_x1 && x < good_x1 + button_w) {
    return 'g';
  }
  if (x >= skip_x1 && x < skip_x1 + button_w) {
    return 's';
  }
  if (x >= bad_x1 && x < bad_x1 + button_w) {
    return 'b';
  }
  return 0;
}

static void drainTouchEvents(uint32_t duration_ms)
{
  const uint32_t start = millis();
  while (millis() - start < duration_ms) {
    int16_t x = 0;
    int16_t y = 0;
    readTouch(x, y);
    gt911ClearStatus();
    delay(TOUCH_CONFIRM_POLL_MS);
  }
}

static bool readStableVisualTouch(char &result, int16_t &last_x, int16_t &last_y)
{
  char stable_result = 0;
  uint32_t stable_since = 0;

  while (true) {
    int16_t touch_x = 0;
    int16_t touch_y = 0;
    if (!readTouch(touch_x, touch_y)) {
      if (stable_result != 0) {
        if (millis() - stable_since >= TOUCH_CONFIRM_STABLE_MS) {
          result = stable_result;
          return true;
        }
        delay(TOUCH_CONFIRM_POLL_MS);
        continue;
      }
      return false;
    }

    char current = visualResultFromTouch(touch_x, touch_y);
    if (current == 0) {
      if (stable_result != 0 && millis() - stable_since >= TOUCH_CONFIRM_STABLE_MS) {
        result = stable_result;
        return true;
      }
      delay(TOUCH_CONFIRM_POLL_MS);
      continue;
    }
    if (current != stable_result) {
      stable_result = current;
      stable_since = millis();
    }

    last_x = touch_x;
    last_y = touch_y;
    if (millis() - stable_since >= TOUCH_CONFIRM_STABLE_MS) {
      result = stable_result;
      return true;
    }

    delay(TOUCH_CONFIRM_POLL_MS);
  }
}

static void dropPanelHandle()
{
  if (panel) {
    esp_lcd_panel_del(panel);
    panel = nullptr;
  }
}

static bool initDisplay(const BenchRun &run)
{
  if (panel) {
    // Recreate the RGB panel so pclk and bounce buffer settings really change.
    esp_lcd_panel_disp_on_off(panel, false);
    esp_err_t del_err = esp_lcd_panel_del(panel);
    if (del_err != ESP_OK) {
      DebugSerial.printf("esp_lcd_panel_del failed: 0x%X\n", del_err);
      return false;
    }
    panel = nullptr;
    delay(150);
  }

  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = run.pclk_hz;
  cfg.timings.h_res = LCD_W;
  cfg.timings.v_res = LCD_H;
  cfg.timings.hsync_pulse_width = LCD_HSYNC_PULSE;
  cfg.timings.hsync_back_porch = LCD_HSYNC_BACK_PORCH;
  cfg.timings.hsync_front_porch = LCD_HSYNC_FRONT_PORCH;
  cfg.timings.vsync_pulse_width = LCD_VSYNC_PULSE;
  cfg.timings.vsync_back_porch = LCD_VSYNC_BACK_PORCH;
  cfg.timings.vsync_front_porch = LCD_VSYNC_FRONT_PORCH;
  cfg.timings.flags.hsync_idle_low = 1;
  cfg.timings.flags.vsync_idle_low = 1;
  cfg.timings.flags.de_idle_high = 0;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.timings.flags.pclk_idle_high = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
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
  cfg.num_fbs = run.num_fbs;
  cfg.bounce_buffer_size_px = LCD_W * run.bounce_lines;
  cfg.flags.fb_in_psram = run.fb_in_psram ? 1 : 0;
  cfg.flags.double_fb = run.double_fb ? 1 : 0;
  cfg.flags.no_fb = false;
  cfg.flags.bb_invalidate_cache = run.bb_invalidate_cache ? 1 : 0;

  esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &panel);
  if (err != ESP_OK) {
    DebugSerial.printf("esp_lcd_new_rgb_panel failed: 0x%X\n", err);
    dropPanelHandle();
    return false;
  }

  esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
  callbacks.on_color_trans_done = onColorTransferDone;
  err = esp_lcd_rgb_panel_register_event_callbacks(panel, &callbacks, nullptr);
  if (err != ESP_OK) {
    DebugSerial.printf("register callbacks failed: 0x%X\n", err);
    dropPanelHandle();
    return false;
  }

  err = esp_lcd_panel_reset(panel);
  if (err != ESP_OK) {
    DebugSerial.printf("esp_lcd_panel_reset failed: 0x%X\n", err);
    dropPanelHandle();
    return false;
  }
  err = esp_lcd_panel_init(panel);
  if (err != ESP_OK) {
    DebugSerial.printf("esp_lcd_panel_init failed: 0x%X\n", err);
    dropPanelHandle();
    return false;
  }

  esp_lcd_panel_disp_on_off(panel, true);
  return true;
}

static void runPsramBenchPass(const char *name, uint8_t mode)
{
  const size_t words = STRESS_BUF_BYTES / sizeof(uint32_t);
  uint32_t checksum = 0xA5A5A5A5u ^ mode;
  uint32_t loops = 0;
  uint32_t elapsed_ms = 0;
  uint32_t start_ms = millis();

  do {
    for (size_t i = 0; i < words; ++i) {
      uint32_t v = stress_buf[i];

      if (mode == 0) {
        // Schreibtest fuellt den ganzen PSRAM-Puffer mit wechselnden Mustern.
        stress_buf[i] = (uint32_t)i ^ checksum ^ loops;
      } else if (mode == 1) {
        // Lesetest sammelt einen Checksum-Wert, damit der Compiler nichts wegoptimiert.
        checksum ^= v + (uint32_t)i + loops;
      } else {
        // Read-modify-write entspricht eher dem Stress in Phase 3/4.
        v ^= checksum + (uint32_t)i + loops;
        stress_buf[i] = v;
        checksum += v;
      }
    }

    loops++;
    elapsed_ms = millis() - start_ms;
  } while (elapsed_ms < PSRAM_BENCH_MS);

  psram_bench_sink ^= checksum;

  uint64_t bytes = (uint64_t)loops * STRESS_BUF_BYTES;
  if (mode == 2) {
    bytes *= 2;
  }
  float mb_s = bytesToMbS(bytes, elapsed_ms);
  DebugSerial.printf("PSRAMBench: test=%s, buffer=%u, loops=%u, elapsed=%u ms, throughput=%.1f MB/s, checksum=0x%08X\n",
                     name,
                     (unsigned)STRESS_BUF_BYTES,
                     loops,
                     elapsed_ms,
                     mb_s,
                     checksum);
}

static void runPsramBenchmark()
{
  DebugSerial.println();
  DebugSerial.printf("PSRAM benchmark without active display DMA, pass_ms=%u\n", PSRAM_BENCH_MS);
  if (!stress_buf) {
    DebugSerial.println("PSRAMBench: skipped, no stress buffer");
    return;
  }

  runPsramBenchPass("write", 0);
  runPsramBenchPass("read", 1);
  runPsramBenchPass("read_modify_write", 2);
  DebugSerial.printf("PSRAMBench: sink=0x%08X, free_psram=%u\n", psram_bench_sink, ESP.getFreePsram());
}

static void stressTask(void *param)
{
  const uint32_t task_id = (uint32_t)(uintptr_t)param;
  uint32_t x = 0x12345678u ^ (task_id * 0x9E3779B9u);
  size_t offset = task_id * 97;
  const size_t words = STRESS_BUF_BYTES / sizeof(uint32_t);

  while (true) {
    uint32_t mode = stress_mode;
    if (mode == 0) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    for (int i = 0; i < STRESS_WORK_CHUNK; ++i) {
      x ^= x << 13;
      x ^= x >> 17;
      x ^= x << 5;

      if ((mode & STRESS_PSRAM_READ) && stress_buf) {
        offset = (offset + 37) % words;
        x += stress_buf[offset];
      }
      if ((mode & STRESS_PSRAM_WRITE) && stress_buf) {
        offset = (offset + 37) % words;
        stress_buf[offset] = x;
      }
      if ((mode & STRESS_PSRAM_RMW) && stress_buf) {
        offset = (offset + 37) % words;
        stress_buf[offset] = stress_buf[offset] ^ x;
        x += stress_buf[(offset + 251) % words];
      }
    }

    psram_bench_sink ^= x;
    if (task_id == 0) {
      stress_counter0++;
    } else {
      stress_counter1++;
    }
    // Kurz blockieren, damit die Idle-Tasks den Watchdog bedienen koennen.
    vTaskDelay(STRESS_IDLE_TICKS);
  }
}

static void printConfig()
{
  DebugSerial.println();
  DebugSerial.println("ESP32-8048S043C display benchmark");
  DebugSerial.printf("Automatic runs=%u, phases=%u, phase_ms=%u\n", RUN_COUNT, PHASE_COUNT, BENCH_PHASE_MS);
  for (uint8_t i = 0; i < RUN_COUNT; ++i) {
    const BenchRun &run = BENCH_RUNS[i];
    DebugSerial.printf("Run %s: pclk=%u Hz, refresh_est=%.2f Hz, panel_psram_read_est=%.1f MB/s, bounce_lines=%d, num_fbs=%d, double_fb=%s, fb_in_psram=%s, bb_invalidate_cache=%s\n",
                       run.id,
                       run.pclk_hz,
                       panelRefreshHz(run),
                       panelPsramReadMbS(run),
                       run.bounce_lines,
                       run.num_fbs,
                       run.double_fb ? "true" : "false",
                       run.fb_in_psram ? "true" : "false",
                       run.bb_invalidate_cache ? "true" : "false");
  }
  DebugSerial.printf("Free heap=%u, free PSRAM=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());
}

static void startPhase(uint8_t index)
{
  const BenchRun &run = BENCH_RUNS[run_index];
  phase_index = index % PHASE_COUNT;
  phase_start_ms = millis();
  phase_frame_count = 0;
  draw_us_sum = 0;
  draw_us_max = 0;
  present_us_sum = 0;
  present_us_max = 0;
  msync_us_sum = 0;
  msync_us_max = 0;
  submit_us_sum = 0;
  submit_us_max = 0;
  wait_us_sum = 0;
  wait_us_max = 0;
  timeout_count = 0;
  stress_mode = BENCH_PHASES[phase_index].stress;
  phase_start_stress_counter0 = stress_counter0;
  phase_start_stress_counter1 = stress_counter1;

  DebugSerial.println();
  DebugSerial.printf("Run %s phase %u/%u: %s, stress=%u, pclk=%u, refresh_est=%.2f Hz, panel_psram_read_est=%.1f MB/s, bounce_lines=%d, double_fb=%s, bb_invalidate_cache=%s\n",
                     run.id,
                     phase_index + 1,
                     PHASE_COUNT,
                     BENCH_PHASES[phase_index].name,
                     stress_mode,
                     run.pclk_hz,
                     panelRefreshHz(run),
                     panelPsramReadMbS(run),
                     run.bounce_lines,
                     run.double_fb ? "true" : "false",
                     run.bb_invalidate_cache ? "true" : "false");
}

static void printPhaseStats(uint32_t elapsed_ms)
{
  const BenchRun &run = BENCH_RUNS[run_index];
  uint32_t frames = phase_frame_count ? phase_frame_count : 1;
  uint32_t avg_draw = draw_us_sum / frames;
  uint32_t avg_present = present_us_sum / frames;
  uint32_t avg_msync = msync_us_sum / frames;
  uint32_t avg_submit = submit_us_sum / frames;
  uint32_t avg_wait = wait_us_sum / frames;
  float fps = (phase_frame_count * 1000.0f) / (float)elapsed_ms;
  uint32_t stress_delta0 = stress_counter0 - phase_start_stress_counter0;
  uint32_t stress_delta1 = stress_counter1 - phase_start_stress_counter1;
  uint32_t stress_delta_total = stress_delta0 + stress_delta1;
  float stress_chunks_s = ((float)stress_delta_total * 1000.0f) / (float)elapsed_ms;
  uint64_t fb_write_min_bytes = (uint64_t)phase_frame_count * LCD_FB_BYTES;
  uint32_t phase_stress_mode = BENCH_PHASES[phase_index].stress;
  uint64_t psram_stress_bytes = (uint64_t)stress_delta_total * STRESS_WORK_CHUNK * psramBytesPerStressIter(phase_stress_mode);
  if (!stressUsesPsram(phase_stress_mode) || !stress_buf) {
    psram_stress_bytes = 0;
  }
  float fb_write_min_mb_s = bytesToMbS(fb_write_min_bytes, elapsed_ms);
  float psram_stress_mb_s = bytesToMbS(psram_stress_bytes, elapsed_ms);
  float panel_read_mb_s = panelPsramReadMbS(run);
  float psram_total_est_mb_s = fb_write_min_mb_s + psram_stress_mb_s + panel_read_mb_s;
  float draw_pct = usPercentOfElapsed(draw_us_sum, elapsed_ms);
  float present_pct = usPercentOfElapsed(present_us_sum, elapsed_ms);
  float wait_pct = usPercentOfElapsed(wait_us_sum, elapsed_ms);

  run_timeout_total += timeout_count;
  run_fb_write_min_bytes += fb_write_min_bytes;
  run_psram_stress_bytes += psram_stress_bytes;
  run_measured_phase_ms += elapsed_ms;
  if (draw_us_max > run_draw_us_max) {
    run_draw_us_max = draw_us_max;
  }
  if (present_us_max > run_present_us_max) {
    run_present_us_max = present_us_max;
  }
  if (msync_us_max > run_msync_us_max) {
    run_msync_us_max = msync_us_max;
  }
  if (submit_us_max > run_submit_us_max) {
    run_submit_us_max = submit_us_max;
  }
  if (wait_us_max > run_wait_us_max) {
    run_wait_us_max = wait_us_max;
  }

  DebugSerial.printf(
      "Stats: run=%s, phase=%s, frames=%u, fps=%.1f, draw_avg=%uus, draw_max=%uus, present_avg=%uus, present_max=%uus, msync_avg=%uus, msync_max=%uus, submit_avg=%uus, submit_max=%uus, wait_avg=%uus, wait_max=%uus, draw_pct=%.1f, present_pct=%.1f, wait_pct=%.1f, timeouts=%u, stress0=%u, stress1=%u, stress0_delta=%u, stress1_delta=%u, stress_chunks_s=%.1f, fb_write_min=%.1f MB/s, psram_stress=%.1f MB/s, panel_psram_read_est=%.1f MB/s, psram_total_est=%.1f MB/s, free_psram=%u\n",
      run.id,
      BENCH_PHASES[phase_index].name,
      phase_frame_count,
      fps,
      avg_draw,
      draw_us_max,
      avg_present,
      present_us_max,
      avg_msync,
      msync_us_max,
      avg_submit,
      submit_us_max,
      avg_wait,
      wait_us_max,
      draw_pct,
      present_pct,
      wait_pct,
      timeout_count,
      stress_counter0,
      stress_counter1,
      stress_delta0,
      stress_delta1,
      stress_chunks_s,
      fb_write_min_mb_s,
      psram_stress_mb_s,
      panel_read_mb_s,
      psram_total_est_mb_s,
      ESP.getFreePsram());
}

static void printRunSummary(uint32_t elapsed_ms)
{
  const BenchRun &run = BENCH_RUNS[run_index];
  float fps = (run_frame_count * 1000.0f) / (float)elapsed_ms;
  float fb_write_min_mb_s = bytesToMbS(run_fb_write_min_bytes, run_measured_phase_ms);
  float psram_stress_mb_s = bytesToMbS(run_psram_stress_bytes, run_measured_phase_ms);
  float panel_read_mb_s = panelPsramReadMbS(run);
  float psram_total_est_mb_s = fb_write_min_mb_s + psram_stress_mb_s + panel_read_mb_s;
  DebugSerial.printf(
      "RunSummary: run=%s, pclk=%u, refresh_est=%.2f Hz, bounce_lines=%d, double_fb=%s, bb_invalidate_cache=%s, frames=%u, fps=%.1f, timeouts=%u, draw_max=%uus, present_max=%uus, msync_max=%uus, submit_max=%uus, wait_max=%uus, fb_write_min_avg=%.1f MB/s, psram_stress_avg=%.1f MB/s, panel_psram_read_est=%.1f MB/s, psram_total_est_avg=%.1f MB/s, result=%s\n",
      run.id,
      run.pclk_hz,
      panelRefreshHz(run),
      run.bounce_lines,
      run.double_fb ? "true" : "false",
      run.bb_invalidate_cache ? "true" : "false",
      run_frame_count,
      fps,
      run_timeout_total,
      run_draw_us_max,
      run_present_us_max,
      run_msync_us_max,
      run_submit_us_max,
      run_wait_us_max,
      fb_write_min_mb_s,
      psram_stress_mb_s,
      panel_read_mb_s,
      psram_total_est_mb_s,
      run_timeout_total == 0 ? "timing-ok" : "suspect");
}

static const char *visualResultName(char result)
{
  if (result == 'g') {
    return "good";
  }
  if (result == 'b') {
    return "bad";
  }
  if (result == 's') {
    return "skip";
  }
  return "unknown";
}

static char waitForVisualResult()
{
  const BenchRun &run = BENCH_RUNS[run_index];
  const BenchPhase &phase = BENCH_PHASES[phase_index];
  if (!BENCH_REQUIRE_VISUAL_CONFIRM) {
    DebugSerial.printf("VisualResult: run=%s, phase=%s, visual=not-requested\n", run.id, phase.name);
    return 'g';
  }
  if (visual_confirm_all_good) {
    DebugSerial.printf("VisualResult: run=%s, phase=%s, visual=good, note=auto-all-remaining-good\n", run.id, phase.name);
    return 'g';
  }

  stress_mode = 0;
  drawVisualPromptScreen();
  presentFrame();
  drainTouchEvents(TOUCH_CONFIRM_ARM_MS);

  DebugSerial.println();
  DebugSerial.printf("Visual check for run %s phase %u/%u (%s): touch left=good, middle=skip, right=bad; serial g/b/s/a also works\n",
                     run.id,
                     phase_index + 1,
                     PHASE_COUNT,
                     phase.name);

  while (true) {
    int16_t touch_x = 0;
    int16_t touch_y = 0;
    char touch_result = 0;
    if (readStableVisualTouch(touch_result, touch_x, touch_y)) {
      DebugSerial.printf("VisualResult: run=%s, phase=%s, visual=%s, input=touch, x=%d, y=%d\n",
                         run.id,
                         phase.name,
                         visualResultName(touch_result),
                         touch_x,
                         touch_y);
      drainTouchEvents(250);
      return touch_result;
    }

    while (DebugSerial.available() > 0) {
      char c = (char)DebugSerial.read();
      if (c >= 'A' && c <= 'Z') {
        c = (char)(c + ('a' - 'A'));
      }
      if (c == 'g' || c == 'b' || c == 's') {
        DebugSerial.printf("VisualResult: run=%s, phase=%s, visual=%s\n", run.id, phase.name, visualResultName(c));
        return c;
      }
      if (c == 'a') {
        visual_confirm_all_good = true;
        DebugSerial.printf("VisualResult: run=%s, phase=%s, visual=good, note=all-remaining-good\n", run.id, phase.name);
        return 'g';
      }
    }
    delay(20);
  }
}

static void finishBenchmark()
{
  stress_mode = 0;
  benchmark_done = true;
  DebugSerial.println();
  DebugSerial.println("Benchmark complete. Compare RunSummary lines and your visual notes.");
}

static bool startRun(uint8_t index)
{
  while (index < RUN_COUNT) {
    run_index = index;
    const BenchRun &run = BENCH_RUNS[run_index];
    run_frame_count = 0;
    run_timeout_total = 0;
    run_draw_us_max = 0;
    run_present_us_max = 0;
    run_msync_us_max = 0;
    run_submit_us_max = 0;
    run_wait_us_max = 0;
    run_fb_write_min_bytes = 0;
    run_psram_stress_bytes = 0;
    run_measured_phase_ms = 0;

    DebugSerial.println();
    DebugSerial.printf("Starting run %s (%u/%u): pclk=%u Hz, refresh_est=%.2f Hz, panel_psram_read_est=%.1f MB/s, bounce_lines=%d, double_fb=%s, bb_invalidate_cache=%s\n",
                       run.id,
                       run_index + 1,
                       RUN_COUNT,
                       run.pclk_hz,
                       panelRefreshHz(run),
                       panelPsramReadMbS(run),
                       run.bounce_lines,
                       run.double_fb ? "true" : "false",
                       run.bb_invalidate_cache ? "true" : "false");

    if (initDisplay(run)) {
      run_start_ms = millis();
      startPhase(0);
      return true;
    }

    DebugSerial.printf("Run %s init failed; skipping.\n", run.id);
    ++index;
  }

  finishBenchmark();
  return false;
}

static void finishPhase(uint32_t elapsed_ms)
{
  printPhaseStats(elapsed_ms);
  waitForVisualResult();

  if (phase_index + 1 < PHASE_COUNT) {
    startPhase(phase_index + 1);
  } else {
    printRunSummary(millis() - run_start_ms);
    startRun(run_index + 1);
  }
}

void setup()
{
  DebugSerial.begin(115200);
  delay(300);

  pinMode(LCD_BL, OUTPUT);
  setBacklight(true);
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  gt911Reset();
  touch_addr = findGT911();
  DebugSerial.printf("Touch: %s\n", touch_addr ? "OK" : "not found, serial feedback only");

  printConfig();
  if (!initBenchmarkBuffers()) {
    DebugSerial.println("Benchmark buffer init failed");
    while (true) {
      setBacklight(true);
      delay(250);
      setBacklight(false);
      delay(250);
    }
  }

  stress_buf = (uint32_t *)heap_caps_malloc(STRESS_BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!stress_buf) {
    DebugSerial.println("Warning: PSRAM stress buffer allocation failed; PSRAM phase will be weaker.");
  } else {
    for (size_t i = 0; i < STRESS_BUF_BYTES / sizeof(uint32_t); ++i) {
      stress_buf[i] = 0xA5A50000u ^ i;
    }
  }

  runPsramBenchmark();

  xTaskCreatePinnedToCore(stressTask, "stress0", 4096, (void *)(uintptr_t)0, 1, nullptr, 0);
  if (BENCH_STRESS_CORE_1) {
    xTaskCreatePinnedToCore(stressTask, "stress1", 4096, (void *)(uintptr_t)1, 1, nullptr, 1);
  }

  DebugSerial.printf("After init: free heap=%u, free PSRAM=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());
  startRun(0);
}

void loop()
{
  if (benchmark_done) {
    delay(1000);
    return;
  }

  uint32_t draw_start = micros();
  drawMovingPattern(frame_count);
  uint32_t draw_elapsed = micros() - draw_start;

  draw_us_sum += draw_elapsed;
  if (draw_elapsed > draw_us_max) {
    draw_us_max = draw_elapsed;
  }

  presentFrame();

  uint32_t elapsed = millis() - phase_start_ms;
  if (elapsed >= BENCH_PHASE_MS) {
    finishPhase(elapsed);
  }
}
