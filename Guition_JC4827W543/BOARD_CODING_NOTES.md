# Guition JC4827W543 Coding Notes

Status: 2026-05-10, cleaned up after local display and render tests

These notes apply to the `Guition_JC4827W543` board with an
`ESP32-S3-WROOM-1 N4R8` module and a 4.3-inch `480x272` display.

## Summary

- Board: Guition JC4827W543, 4.3-inch ESP32-S3 HMI.
- MCU/module: ESP32-S3-WROOM-1 N4R8.
- Display: 480 x 272 RGB565 / 16 bit.
- Display driver: NV3041A.
- Display interface: QSPI/SPI through LovyanGFX, not RGB-DE/VSYNC/HSYNC/PCLK.
- Backlight: GPIO1 through LovyanGFX `Light_PWM`.
- Current display orientation: `display.setRotation(2)` / 180 degrees.
- Currently stable render path: LVGL fullscreen buffer in PSRAM,
  `LV_DISPLAY_RENDER_MODE_FULL`, `LV_COLOR_FORMAT_RGB565_SWAPPED`.

Recommended display base for new UI work:

- `LovyanGFX` with `Panel_NV3041A`
- LVGL v9
- one fullscreen buffer in PSRAM
- conservative SPI/QSPI clock of `32 MHz`

Reference sketch:

```text
DisplayPixelRaster/DisplayPixelRaster.ino
```

The raster sketch uses the same stable display path as the larger application
sketch. It is the best starting point when checking display artifacts,
rotation, coordinates, or buffer problems.

## Arduino IDE Starting Values

- Board: `ESP32S3 Dev Module`
- CPU Frequency: 240 MHz
- Flash Size: 4 MB
- Partition Scheme: `Huge APP (3MB No OTA/1MB SPIFFS)`
- PSRAM: enable `OPI PSRAM` / `Octal PSRAM`
- USB CDC On Boot: Enabled
- Upload Speed: 921600, try lower values if unreliable

The sketches currently do not use SPIFFS. The Huge APP partition is useful
because the sketches are flashed manually over USB and do not need OTA slots.

PSRAM should be enabled. One fullscreen LVGL buffer needs:

```text
480 * 272 * 2 = 261120 bytes
```

That is not much for this board, but the stable render path depends on being
able to allocate a suitable contiguous buffer.

## Software Base

The Guition sketches typically use:

- ESP32 Arduino core with ESP32-S3 support
- LVGL v9
- LovyanGFX
- ArduinoJson when the application sketch processes JSON

Important for LovyanGFX + real LVGL:

```cpp
#define M5GFX_USING_REAL_LVGL
#define M5GFX_LVGL_FONT_COMPAT_H
#define M5GFX_LVGL_COLOR_H
#define M5GFX_LVGL_AREA_H
#define M5GFX_LVGL_FONT_H
#define M5GFX_LVGL_DRAW_BUF_H
#define M5GFX_LVGL_FONT_FMT_TXT_H
```

These defines prevent LovyanGFX from providing its own outdated LVGL types when
the project uses LVGL v9 directly.

## Display Pins

Currently stable pinout for the NV3041A over QSPI/SPI:

| Function | GPIO |
| --- | ---: |
| SCLK | 47 |
| IO0 | 21 |
| IO1 | 48 |
| IO2 | 40 |
| IO3 | 39 |
| CS | 45 |
| RST | 4 |
| BL | 1 |

This pinout is not compatible with RGB-panel boards such as the
`ESP32-8048S043C`. Do not reuse RGB-DE/VSYNC/HSYNC/PCLK pin lists here.

## LovyanGFX Display Configuration

Tested starting values:

| Setting | Value |
| --- | --- |
| Panel | `lgfx::Panel_NV3041A` |
| Bus | `lgfx::Bus_SPI` |
| SPI Host | `SPI3_HOST` |
| SPI Mode | `1` |
| Write Clock | `32000000UL` |
| Read Clock | `16000000UL` |
| 3-Wire SPI | `true` |
| DMA Channel | `SPI_DMA_CH_AUTO` |
| Panel Width | `480` |
| Panel Height | `272` |
| Memory Width | `480` |
| Memory Height | `272` |
| Invert | `true` |
| RGB Order | `true` |
| 16-bit Data Length | `false` |
| Bus Shared | `true` |

Setup order in the existing sketches:

```cpp
display.init();
display.initDMA();
display.setColorDepth(16);
display.invertDisplay(true);
display.setRotation(2);
display.setBrightness(160);
```

The application sketch controls brightness with `display.setBrightness()`.
The raster test starts with a fixed value of `160`.

## Recommended Render Path

This board/display setup is sensitive to partial updates. The currently stable
path is:

- use LVGL
- allocate a fullscreen buffer in PSRAM:
  `heap_caps_malloc(bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`
- use `LV_DISPLAY_RENDER_MODE_FULL`
- use `LV_COLOR_FORMAT_RGB565_SWAPPED`
- keep the flush path from the raster test or application sketch
- keep the SPI/QSPI clock conservative at `32 MHz`
- use `display.waitDMA()` after pushing data through LovyanGFX

The `DisplayPixelRaster` test confirmed that direct LovyanGFX drawing with many
lines/text elements can produce visible artifacts or pixel errors on this
NV3041A/QSPI setup. With an LVGL fullscreen buffer in PSRAM, the raster was
clean.

## LVGL Flush Path

The flush path clips the LVGL area to the display bounds, handles non-contiguous
row regions, and passes the data to LovyanGFX as `lgfx::swap565_t`.

Important details:

- `LV_COLOR_FORMAT_RGB565_SWAPPED`
- `reinterpret_cast<const lgfx::swap565_t*>(pxMap)`
- `display.waitDMA()` before and after the transfer
- `display.startWrite()` / `display.endWrite()` only when no write is active
- use row-by-row `pushImage()` when the LVGL buffer is not contiguous

Do not simplify this combination without a concrete test reason. Small changes
to color format, byte swap, or flush window handling can immediately cause
wrong colors or pixel artifacts.

## Optimization Caution

Do not casually switch to LVGL partial rendering with DMA partial buffers. On
this setup, visible pixel artifacts appeared later even when the screen looked
clean at first.

Likely cause combination:

- NV3041A QSPI driver
- RGB565 byte swap
- LVGL partial invalidation
- flush/window handling

More robust optimizations are usually UI-level optimizations:

- refresh only active screens
- update label text only when it actually changed
- rate-limit canvas and chart updates
- avoid aggressive display/SPI changes without a concrete test reason

The application sketch contains fallbacks to smaller partial buffers if the
fullscreen buffer cannot be allocated. These fallbacks are useful, but should
not be treated as the preferred render path while the PSRAM fullscreen buffer
works.

## Touch

This repository does not currently document a stable touch path for the Guition
JC4827W543. Do not copy GT911 pins or calibration values from the
`ESP32-8048S043C`; this Guition board uses a different display interface and
must be verified separately.

If touch is added for this board later:

- identify the I2C pins and touch controller first
- create a separate minimal test
- log raw values
- test rotation and mapping independently from the display flush path
- integrate it into larger LVGL applications only afterwards

## Reference Sketches

| Path | Purpose |
| --- | --- |
| `DisplayPixelRaster/DisplayPixelRaster.ino` | Orientation, coordinates, raster, LVGL fullscreen buffer in PSRAM |
| `KlipperCryptoWeatherPanel/KlipperCryptoWeatherPanel.ino` | Larger application sketch using the same LovyanGFX/NV3041A base |

## Practical Rules

- For new UI work, use the LVGL fullscreen buffer path first.
- When pixel artifacts appear, first check: PSRAM enabled, rotation `2`, color
  format `RGB565_SWAPPED`, SPI clock `32 MHz`, unchanged flush path.
- Do not port RGB-panel code from the ESP32-8048S043C; this board is NV3041A
  over QSPI/SPI.
- Direct LovyanGFX drawing is fine for small tests, but LVGL fullscreen
  rendering was locally more stable for more complex line/text layouts.
- Do not adopt partial-rendering, DMA, or SPI optimizations without testing the
  `DisplayPixelRaster` sketch and the real application on the panel afterwards.
- If colors look wrong, check byte swap and `rgb_order` first instead of
  randomly changing UI colors.
