#include <Arduino.h>

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

static constexpr uint32_t BENCH_PHASE_MS = 20000;
static constexpr uint32_t PSRAM_BENCH_MS = 1000;
static constexpr bool BENCH_STRESS_CORE_1 = true;
static constexpr int STRESS_WORK_CHUNK = 4096;
static constexpr TickType_t STRESS_IDLE_TICKS = 1;

static constexpr size_t LCD_FB_PIXELS = LCD_W * LCD_H;
static constexpr size_t LCD_FB_BYTES = LCD_FB_PIXELS * sizeof(uint16_t);
static constexpr size_t STRESS_BUF_BYTES = 512 * 1024;

static HardwareSerial &DebugSerial = Serial0;

static esp_lcd_panel_handle_t panel = nullptr;
static SemaphoreHandle_t color_done_sem = nullptr;
static uint16_t *draw_fb = nullptr;
static uint32_t *stress_buf = nullptr;

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
static uint32_t phase_start_ms = 0;
static uint32_t run_start_ms = 0;
static uint32_t run_frame_count = 0;
static uint32_t run_timeout_total = 0;
static uint32_t run_draw_us_max = 0;
static uint32_t run_present_us_max = 0;
static uint8_t phase_index = 0;
static uint8_t run_index = 0;
static bool benchmark_done = false;

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
};

static const BenchRun BENCH_RUNS[] = {
    {"A", 18000000, 12, 2, true, true},
    {"B", 16000000, 10, 2, true, true},
    {"C", 14000000, 10, 2, true, true},
    {"D", 10000000, 10, 2, true, true},
    {"E", 14000000, 4, 2, true, true},
    {"F", 14000000, 0, 2, true, true},
    {"G", 14000000, 16, 2, true, true},
};

static const BenchPhase BENCH_PHASES[] = {
    {"baseline full redraw", 0},
    {"cpu load", 1},
    {"psram load", 2},
    {"mixed cpu+psram load", 3},
};

static constexpr uint8_t RUN_COUNT = sizeof(BENCH_RUNS) / sizeof(BENCH_RUNS[0]);
static constexpr uint8_t PHASE_COUNT = sizeof(BENCH_PHASES) / sizeof(BENCH_PHASES[0]);

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
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

static bool presentFrame()
{
  uint32_t start = micros();
  esp_err_t err = esp_cache_msync(
      draw_fb,
      LCD_FB_BYTES,
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

  if (xSemaphoreTake(color_done_sem, pdMS_TO_TICKS(250)) != pdTRUE) {
    timeout_count++;
  }

  uint32_t elapsed = micros() - start;
  present_us_sum += elapsed;
  if (elapsed > present_us_max) {
    present_us_max = elapsed;
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
  cfg.flags.bb_invalidate_cache = false;

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
  float mb_s = (elapsed_ms > 0) ? ((float)bytes / 1000000.0f) / ((float)elapsed_ms / 1000.0f) : 0.0f;
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

      if ((mode & 2) && stress_buf) {
        offset = (offset + 37) % words;
        stress_buf[offset] = stress_buf[offset] ^ x;
        x += stress_buf[(offset + 251) % words];
      }
    }

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
    DebugSerial.printf("Run %s: pclk=%u Hz, bounce_lines=%d, num_fbs=%d, double_fb=%s, fb_in_psram=%s\n",
                       run.id,
                       run.pclk_hz,
                       run.bounce_lines,
                       run.num_fbs,
                       run.double_fb ? "true" : "false",
                       run.fb_in_psram ? "true" : "false");
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
  timeout_count = 0;
  stress_mode = BENCH_PHASES[phase_index].stress;

  DebugSerial.println();
  DebugSerial.printf("Run %s phase %u/%u: %s, stress=%u, pclk=%u, bounce_lines=%d\n",
                     run.id,
                     phase_index + 1,
                     PHASE_COUNT,
                     BENCH_PHASES[phase_index].name,
                     stress_mode,
                     run.pclk_hz,
                     run.bounce_lines);
}

static void printPhaseStats(uint32_t elapsed_ms)
{
  const BenchRun &run = BENCH_RUNS[run_index];
  uint32_t frames = phase_frame_count ? phase_frame_count : 1;
  uint32_t avg_draw = draw_us_sum / frames;
  uint32_t avg_present = present_us_sum / frames;
  float fps = (phase_frame_count * 1000.0f) / (float)elapsed_ms;

  run_timeout_total += timeout_count;
  if (draw_us_max > run_draw_us_max) {
    run_draw_us_max = draw_us_max;
  }
  if (present_us_max > run_present_us_max) {
    run_present_us_max = present_us_max;
  }

  DebugSerial.printf(
      "Stats: run=%s, phase=%s, frames=%u, fps=%.1f, draw_avg=%uus, draw_max=%uus, present_avg=%uus, present_max=%uus, timeouts=%u, stress0=%u, stress1=%u, free_psram=%u\n",
      run.id,
      BENCH_PHASES[phase_index].name,
      phase_frame_count,
      fps,
      avg_draw,
      draw_us_max,
      avg_present,
      present_us_max,
      timeout_count,
      stress_counter0,
      stress_counter1,
      ESP.getFreePsram());
}

static void printRunSummary(uint32_t elapsed_ms)
{
  const BenchRun &run = BENCH_RUNS[run_index];
  float fps = (run_frame_count * 1000.0f) / (float)elapsed_ms;
  DebugSerial.printf(
      "RunSummary: run=%s, pclk=%u, bounce_lines=%d, frames=%u, fps=%.1f, timeouts=%u, draw_max=%uus, present_max=%uus, result=%s\n",
      run.id,
      run.pclk_hz,
      run.bounce_lines,
      run_frame_count,
      fps,
      run_timeout_total,
      run_draw_us_max,
      run_present_us_max,
      run_timeout_total == 0 ? "timing-ok" : "suspect");
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

    DebugSerial.println();
    DebugSerial.printf("Starting run %s (%u/%u): pclk=%u Hz, bounce_lines=%d\n",
                       run.id,
                       run_index + 1,
                       RUN_COUNT,
                       run.pclk_hz,
                       run.bounce_lines);

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

void setup()
{
  DebugSerial.begin(115200);
  delay(300);

  pinMode(LCD_BL, OUTPUT);
  setBacklight(true);

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
    printPhaseStats(elapsed);
    if (phase_index + 1 < PHASE_COUNT) {
      startPhase(phase_index + 1);
    } else {
      printRunSummary(millis() - run_start_ms);
      startRun(run_index + 1);
    }
  }
}
