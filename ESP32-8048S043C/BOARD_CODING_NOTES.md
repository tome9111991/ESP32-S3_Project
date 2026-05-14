# ESP32-8048S043C Coding Notes

Status: 2026-05-14, cleaned up after local display and touch tests and full
`display_benchmark` sweep

These notes apply to the `ESP32-8048S043C` board with an `ESP32-S3-WROOM-1`
module and a 4.3-inch `800x480` display.

## Summary

- Board: ESP32-8048S043C, 4.3-inch HMI/TFT.
- MCU/module: ESP32-S3-WROOM-1, marking `MCN16R8`.
- Locally confirmed: 16 MB flash, 8 MB PSRAM, 40 MHz crystal.
- Display: 800 x 480 RGB565 over a parallel RGB interface.
- Touch: GT911 over I2C, locally found at address `0x5D`.
- Backlight: GPIO2, `HIGH` turns it on.
- SD card: SPI, CS on GPIO10.

Recommended display base for smooth touch GUIs:

- Direct `esp_lcd` RGB setup with double buffering.
- Test sketch: `displaytest_esp_lcd_doublefb/displaytest_esp_lcd_doublefb.ino`
- Locally much more stable while dragging a finger than Arduino_GFX with a
  single framebuffer.

Arduino_GFX is still useful for simple or static tests:

- Test sketch: `displaytest/displaytest.ino`
- Display and touch work.
- Not ideal for fast touch trails, animations, or frequent updates because the
  local Arduino_GFX RGB class uses only one PSRAM framebuffer.

Not recommended as the base for this setup:

- Direct LovyanGFX RGB driver with Arduino-ESP32 `3.3.8`.
- Locally hit an `LCD_CAM interrupt` problem; the display did not run cleanly.

Important: This board is not a Guition/JC4827W543 and not an NV3041A QSPI
panel. Do not reuse QSPI/SPI display pinouts for this board.

## Arduino IDE Starting Values

- Board: `ESP32S3 Dev Module`
- CPU Frequency: 240 MHz
- Flash Size: 16 MB
- Flash Mode: QIO 80 MHz, try DIO if boot/upload is unreliable
- PSRAM: enable `OPI PSRAM` / `Octal PSRAM`
- Upload Speed: 921600
- USB CDC On Boot: Disabled when Serial/upload uses CH340/UART
- JTAG Adapter: Disabled when unused
- Events Run On: Core 1
- Arduino Runs On: Core 1
- Partition Scheme: for tests, `16M Flash (3MB APP/9.9MB FATFS)`

PSRAM must be enabled for `displaytest_esp_lcd_doublefb`. Two RGB565
framebuffers plus the draw buffer need roughly 2.3 MB PSRAM:
`800 * 480 * 2 = 768000 bytes` per full framebuffer.

## Display Pins

Control signals:

| Function | GPIO |
| --- | ---: |
| LCD_DE | 40 |
| LCD_VSYNC | 41 |
| LCD_HSYNC | 39 |
| LCD_PCLK | 42 |
| TFT_BL | 2 |

RGB data lines:

| Color | Bits | GPIOs |
| --- | --- | --- |
| Red | R0 R1 R2 R3 R4 | 45, 48, 47, 21, 14 |
| Green | G0 G1 G2 G3 G4 G5 | 5, 6, 7, 15, 16, 4 |
| Blue | B0 B1 B2 B3 B4 | 8, 3, 46, 9, 1 |

Tested timing starting values:

- Resolution: `800x480`
- RGB: 16 bit / RGB565
- PCLK: `14000000` (16 MHz and 18 MHz showed artifacts even at baseline in the
  `display_benchmark` sweep on 2026-05-14)
- Bounce buffer: `bounce_lines = 10` (sweet spot 8..10; below 8 or above 14
  failed visually under load)
- HSYNC: polarity `0`, front `8`, pulse `4`, back `16`
- VSYNC: polarity `0`, front `4`, pulse `4`, back `4`
- PCLK active negative: `1`
- PCLK idle high: `1`
- DE idle high: `0`

## Recommended Display Path

For new GUI work, start from this sketch:

```text
displaytest_esp_lcd_doublefb/displaytest_esp_lcd_doublefb.ino
```

The sketch uses:

- `esp_lcd_new_rgb_panel()`
- `num_fbs = 2`
- `double_fb = true`
- `fb_in_psram = true`
- `esp_lcd_panel_draw_bitmap()`
- `on_color_trans_done` callback before reusing the draw buffer

Local result: a more stable image while dragging a finger. Response is slightly
slower than drawing directly with Arduino_GFX, but without visible distortion.
If more responsiveness is needed, first lower `TOUCH_FRAME_INTERVAL_MS` in the
sketch from `50` to `30` or `25`.

If memory or initialization problems occur:

- verify that OPI PSRAM is enabled (`Tools -> PSRAM -> OPI PSRAM`)
- keep `LCD_BOUNCE_LINES` in `8..10`; the benchmark showed `0` and `4` always
  visually broken, and `16` failed under CPU stress
- if 14 MHz still glitches, try lower (12 MHz) before changing buffers — 16 MHz
  is already past the stable point on this board

## Display Benchmark Findings (2026-05-14)

Full sweep of `display_benchmark/display_benchmark.ino` (9 runs x 5 stress
phases each), evaluated with the new yes/no prompt ("would you ship these
settings, considering both image quality and FPS?").

### PSRAM speed

PSRAMBench (pure CPU access without RGB DMA running):

| Test | Throughput |
| --- | ---: |
| write | 48.6 MB/s |
| read | 52.5 MB/s |
| read-modify-write | 84.9 MB/s |

These numbers are typical for OPI PSRAM at 80 MHz DDR with 32-byte cachelines.
Quad PSRAM would sit around 25..35 MB/s, so OPI is correctly active. Arduino
IDE only exposes `OPI PSRAM` / `QSPI PSRAM` in the dropdown, no clock speed —
80 MHz is the fixed default in the arduino-esp32 core. Pushing to 120 MHz
requires ESP-IDF with `CONFIG_SPIRAM_SPEED_120M` and a matching Flash mode, so
it is not reachable from the stock Arduino IDE on this board.

### Visual matrix (yes/no per phase)

All runs are at `num_fbs = 2`, `fb_in_psram = true` unless noted.

| Run | pclk | bounce | double_fb | bb_inval | baseline | cpu | psram_r | psram_w | psram_rmw |
| --- | ---: | ---: | --- | --- | --- | --- | --- | --- | --- |
| A | 18 MHz | 12 | true | false | no | no | no | no | no |
| B | 16 MHz | 10 | true | false | no | no | no | no | no |
| C | 14 MHz | 10 | true | false | **yes** | **yes** | no | no | no |
| D | 14 MHz | 8 | true | false | **yes** | **yes** | no | no | no |
| E | 14 MHz | 4 | true | false | no | no | no | no | no |
| F | 14 MHz | 0 | true | false | no | no | no | no | no |
| G | 14 MHz | 16 | true | false | **yes** | no | no | no | no |
| **H** | 14 MHz | 10 | **false** | false | **yes** | **yes** | no | no | no |
| I | 14 MHz | 10 | true | **true** | **yes** | **yes** | no | no | no |

### Rules derived from the matrix

- pclk >= 16 MHz on this board produces artifacts even at baseline (Runs A, B).
  Do not push past 14 MHz.
- bounce_lines < 8 (Runs E with 4, F with 0) is visually broken regardless of
  pclk, even though Run F had the highest FPS (12.7) — fastest is not safest.
- bounce_lines = 16 (Run G) survives baseline but fails under CPU load.
- The sweet spot is bounce_lines = 8..10 with pclk = 14 MHz.
- `double_fb` and `bb_invalidate_cache` have no visible effect on yes/no
  outcomes in the baseline+cpu phases (C, H, I all pass).

### Why all PSRAM-stress phases were marked "no"

Not because the picture was corrupted, but because FPS collapsed to 0.2..5
FPS. Mechanism: three masters share the PSRAM bus when the framebuffer lives
in PSRAM:

1. RGB panel DMA reads ~26 MB/s constantly to refresh the LCD.
2. CPU writes the next frame into PSRAM (~10 MB/s at full redraw).
3. The stress task hammers PSRAM with extra reads/writes.

The arbiter has to give DMA priority (otherwise tearing), so the CPU starves.
`draw_avg` jumps from ~52 ms to ~2700 ms in the PSRAM-write phase. This is bus
contention, not PSRAM speed.

### Implication for the real GUI

If the application does heavy parallel PSRAM work during redraws (full-frame
animations, image decode into PSRAM, large LVGL canvases), expect the same FPS
drop in production — no display setting fixes it. Levers that actually help:

- Place LVGL draw buffers in internal SRAM instead of PSRAM (smaller buffers,
  but no contention with the panel DMA).
- Use partial-refresh / dirty-rect updates instead of full-frame redraws.
- Avoid hot loops that scan large PSRAM regions on the same core as the
  drawing task.

### Chosen winner

**Run H** — already matches the current app config:

```cpp
pclk_hz = 14000000
bounce_lines = 10
num_fbs = 2
fb_in_psram = true
double_fb = false
bb_invalidate_cache = false
```

Fallback if cache artifacts appear in the real app: **Run I** (same but
`double_fb = true`, `bb_invalidate_cache = true`).

## Arduino_GFX Use

Arduino_GFX works locally for basic display/touch tests:

```text
displaytest/displaytest.ino
```

Suitable for:

- static test images
- simple status displays
- infrequent label/value updates

Not ideal for:

- touch trails while dragging
- continuous animations
- frequent full-screen or partial-screen updates

Reason: the local `GFX_Library_for_Arduino` RGB path internally sets
`num_fbs = 1` and `double_fb = false`. If CPU/cache writes into the same PSRAM
framebuffer that LCD_CAM is currently reading, visible distortion can occur.

## GT911 Touch

| Function | GPIO / Value |
| --- | --- |
| I2C SDA | GPIO19 |
| I2C SCL | GPIO20 |
| GT911 Reset | GPIO38 |
| GT911 INT | GPIO18, do not assume it is usable |
| I2C address | locally `0x5D`, fallback `0x14` is reasonable |

Touch polling works. For new sketches, start without an interrupt:

```cpp
Wire.begin(19, 20, 400000);
```

After reading the GT911 status register `0x814E`, write `0x00` back to it.
GPIO18/INT is often only reliably usable on this board class with a hardware
modification.

Locally measured touch calibration from 2026-05-08:

- The display remains `800x480`.
- The GT911 raw values are only roughly in this range:
  `raw_x` about `27..458`, `raw_y` about `24..249`.
- Never use raw values directly as display pixels.
- After calibration in
  `displaytest_esp_lcd_doublefb/displaytest_esp_lcd_doublefb.ino`, these values
  matched well:

```cpp
static constexpr bool TOUCH_USE_SAVED_CALIBRATION = true;
static constexpr float TOUCH_CAL_X_RX = 1.65867031f;
static constexpr float TOUCH_CAL_X_RY = -0.02261823f;
static constexpr float TOUCH_CAL_X_C = 2.12817001f;
static constexpr float TOUCH_CAL_Y_RX = 0.02082564f;
static constexpr float TOUCH_CAL_Y_RY = 1.79517055f;
static constexpr float TOUCH_CAL_Y_C = 10.62223816f;
```

Mapping after reading raw GT911 values:

```cpp
screen_x = constrain((int)((TOUCH_CAL_X_RX * raw_x) + (TOUCH_CAL_X_RY * raw_y) + TOUCH_CAL_X_C + 0.5f), 0, LCD_W - 1);
screen_y = constrain((int)((TOUCH_CAL_Y_RX * raw_x) + (TOUCH_CAL_Y_RY * raw_y) + TOUCH_CAL_Y_C + 0.5f), 0, LCD_H - 1);
```

These calibration values apply to this board with `800x480` and the currently
tested display orientation. Recalibrate or adjust the mapping when the display
is rotated or mirrored.

## SD Card

| Function | GPIO |
| --- | ---: |
| SD_CS | 10 |
| SD_MOSI | 11 |
| SD_SCK | 12 |
| SD_MISO | 13 |

Do not reuse these pins for other SPI devices unless chip-select handling is
clean.

## Other Pins

| GPIO | Use |
| ---: | --- |
| 0 | BOOT button, mind strapping |
| 17 | NC |
| 18 | CTP_INT only with / depending on hardware modification |
| 33, 34 | NA according to pin lists |
| 35, 36, 37 | NC / NA |
| 43 | U0TXD / CH340 Serial |
| 44 | U0RXD / CH340 Serial |

## Measured Board Data

Measured locally with esptool:

```text
Chip type: ESP32-S3 (QFN56), revision v0.2
Features: Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
Crystal frequency: 40MHz
Detected flash size: 16MB
Flash type set in eFuse: quad (4 data lines)
Flash voltage set by eFuse: 3.3V
Secure Boot: Disabled
Flash Encryption: Disabled
```

## Practical Rules

- For smooth touch GUIs, use `esp_lcd` double buffering as the base.
- For simple tests, Arduino_GFX is okay.
- Do not draw fast animations directly into an active single framebuffer.
- Update labels/values only when they actually changed.
- When artifacts appear, first check: PSRAM enabled, PCLK 16/14 MHz, inverted
  PCLK, porch values, RGB pin order.
- Do not use LovyanGFX RGB with Arduino-ESP32 `3.3.8` as the default path here.

## Sources

- HomeDing Panel ESP32-8048S043C: https://homeding.github.io/boards/esp32s3/panel-8048S043.htm
- Espressif RGB LCD / esp_lcd: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html
- Espressif ESP32-S3-WROOM-1/WROOM-1U Datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
