# ESP32-8048S043C Klipper Crypto Weather Panel Port

ESP-IDF project for the **ESP32-8048S043C** (4.3" 800x480 RGB panel with
GT911 touch). This is the native IDF port of the Arduino
`KlipperCryptoWeatherPanel` dashboard, built on the already verified
RGB-panel + GT911 + LVGL scaffold.

## What the app currently does

- Initializes the RGB panel with double framebuffer in PSRAM and a bounce
  buffer in internal SRAM
- Starts the display mirrored by 180 degrees by default, matching the Arduino
  sketch
- Reads GT911 touch over I2C and calibrates it to screen pixels
- Starts LVGL via `esp_lvgl_port`
- Shows the four dashboard screens: time/weather, live crypto price, crypto
  candle chart, and Klipper/Moonraker status
- Connects WiFi from compile-time `main/config_private.h`
- Fetches weather from Bright Sky, crypto data from Coinbase, and printer data
  from Moonraker using native ESP-IDF HTTP + cJSON
- Taps left/right switch screens; the dashboard also auto-rotates screens

## Hardware

| Item | Value |
|---|---|
| Board | ESP32-8048S043**C** |
| MCU | ESP32-S3-WROOM-1 N16R8 |
| Flash | 16 MB, QIO @ 80 MHz |
| PSRAM | 8 MB Octal @ 80 MHz |
| Display | 4.3" 800×480, parallel RGB565 interface |
| Touch | GT911 capacitive over I2C (address 0x5D) |

Pinout: see `../../BOARD_CODING_NOTES.md` and the constants near the top of
`main/main.c`.

## Build / Flash

On the first build the component manager downloads LVGL, esp_lvgl_port and
esp_lcd_touch_gt911 from the Espressif Component Registry — this can take
about a minute.

```bash
idf.py build
idf.py -p COMx flash monitor
```

`Ctrl+]` exits the monitor.

## Project layout

```
KlipperCryptoWeatherPanel_port/
├── CMakeLists.txt
├── README.md                      # this file
├── sdkconfig.defaults             # board-specific IDF defaults
└── main/
    ├── CMakeLists.txt             # component sources/requires
    ├── idf_component.yml          # managed dependencies (LVGL, touch)
    ├── app_state.h                # shared state/config/API boundaries
    ├── main.c                     # panel + touch + LVGL host loop
    ├── ui_screens.c               # dashboard screens
    ├── ui_chart.c                 # candle chart renderer
    ├── net_fetcher.c              # WiFi/NTP/API fetch task
    └── config_private.example.h   # copy to config_private.h locally
```

`sdkconfig` is .gitignored and gets generated from `sdkconfig.defaults` on
the first `idf.py reconfigure`. The project uses `partitions.csv` so the
larger LVGL/font binary fits in the ESP32-8048S043C's 16 MB flash. When
changing the defaults: delete `sdkconfig` and regenerate.

## Dependencies

| Component | Source | Purpose |
|---|---|---|
| `esp_lcd` | ESP-IDF built-in | RGB panel driver |
| `esp_driver_i2c` | ESP-IDF built-in | I2C master for touch |
| `esp_driver_gpio` | ESP-IDF built-in | Backlight, touch reset |
| `esp_timer` | ESP-IDF built-in | Timestamps |
| `espressif/esp_lvgl_port` | Component Registry | LVGL integration for ESP boards |
| `espressif/esp_lcd_touch_gt911` | Component Registry | GT911 I2C driver |
| `lvgl/lvgl` | Component Registry (transitive) | UI library |

## Configuration

Copy `main/config_private.example.h` to `main/config_private.h` and edit the
private values there. `main/config_private.h` is ignored by Git.

The native port currently uses compile-time configuration only. The Arduino
runtime settings overlays, LittleFS persistence, brightness settings, runtime
rotation controls, touch calibration UI, and MMU gate detail display are not
ported yet.

## Technical decisions

A few non-obvious things that were learned the hard way and are worth
documenting:

### Double framebuffer + DIRECT render mode

`num_fbs = 2`, `avoid_tearing = true`, `direct_mode = true`. These three are
coupled: `esp_lvgl_port` uses the two panel FBs as front/back buffers and
swaps them on VSYNC. Without `direct_mode`, LVGL falls back to partial mode,
but its flush logic does not match full-size FBs → blank or stuck image.

### Bounce buffer is mandatory, not optional

`bounce_buffer_size_px = LCD_H_RES * 10` plus `bb_mode = true` in the
`esp_lvgl_port` RGB config. The two 800×480×2 B framebuffers live in PSRAM
(too large for internal SRAM), so without a bounce buffer the LCD-DMA reads
~15 MB/s continuously from PSRAM. As soon as LVGL writes into the back
buffer to repaint a UI element, CPU writes and DMA reads collide on the
PSRAM bus → DMA underruns → visible shake/tearing on every UI update.

With the bounce buffer the DMA reads from internal SRAM (10 lines, ~16 KB),
and an ISR refills that buffer in bursts from the active PSRAM FB. PSRAM
access becomes bursty with idle gaps, and LVGL writes fit into the gaps —
no contention, no shake. This is required for *any* dynamic UI on this
board, not just demos.

### SPIRAM_FETCH_INSTRUCTIONS / SPIRAM_RODATA off

Both are explicitly disabled in `sdkconfig.defaults`. If they are on, the
CPU fetches code and rodata from PSRAM in small random reads, which
competes badly with the bounce-buffer refill ISR's larger burst reads on
the same bus. Code stays in flash XIP — slightly tighter on internal cache
but much smoother for the LCD pipeline.

### IRAM-safe RGB ISR disabled

`CONFIG_LCD_RGB_ISR_IRAM_SAFE` is **not** set, because `esp_lvgl_port`'s
VSYNC callback is not compiled into IRAM. Enabling it would make
`esp_lcd_rgb_panel_register_event_callbacks()` fail with `ESP_ERR_INVALID_ARG`.
Trade-off: later flash/NVS writes may produce occasional display artifacts —
the right fix then is to pause LVGL around the write, not to tighten the
driver beyond what `esp_lvgl_port` expects.

### Pixels via `esp_lcd_panel_draw_bitmap()`, not direct FB writes

Direct CPU writes to the PSRAM framebuffer have cache coherency issues:
writes sit in the D-cache and only reach PSRAM later, while the LCD DMA reads
directly from PSRAM. Result: visible glitches at frame edges. `draw_bitmap`
handles the cache sync internally. (LVGL / `esp_lvgl_port` handles this for
us during runtime.)

### Touch calibration as a driver callback

The GT911 on this board reports raw values in the range ~0..458 / ~0..249
instead of the expected 0..800 / 0..480. The affine map from
`BOARD_CODING_NOTES.md` is registered as a `process_coordinates` callback on
the `esp_lcd_touch` handle. This way **every** consumer (LVGL input and our
own polling) receives already-mapped screen pixels — no per-call conversion
needed.

### 180-degree display default via panel mirror

The Arduino sketch defaults to `DISPLAY_ROTATE_180_DEFAULT = true`. The IDF
port keeps that behavior by setting `LV_DISPLAY_ROTATION_180` after adding the
RGB display and mirrors touch coordinates in the GT911 coordinate callback.
When the rotation default is enabled, LVGL uses a small 20-line partial buffer
and flushes through `esp_lcd_panel_draw_bitmap()`, because that path honors the
LCD panel mirror on RGB panels. Software rotation stays disabled, so no extra
rotation buffer is allocated.

## Pitfalls

| Symptom | Cause | Fix |
|---|---|---|
| Build error `esp_timer.h not found` | Component missing from REQUIRES | Add `esp_timer` to `main/CMakeLists.txt` |
| Build error `bits_per_pixel has no member` | IDF v5 API used | In v6: `in_color_format` / `out_color_format` |
| Panel white but backlight on | Wrong render-mode combination | Set `direct_mode = true` |
| Panel shows nothing even though init succeeds | `CONFIG_LCD_RGB_ISR_IRAM_SAFE=y` with esp_lvgl_port | Disable IRAM_SAFE |
| White glitches at the edge | Cache coherency on direct FB writes | Use `esp_lcd_panel_draw_bitmap()` |
| Touch hits wrong UI areas | GT911 returns raw values | Apply calibration in `process_coordinates` callback |
| Colors look inverted | RGB pin order off | Swap the R and B groups in `data_gpio_nums` |
| Screen shakes / button tears on UI updates | LCD-DMA fights LVGL writes on the PSRAM bus | Enable `bb_mode` + `bounce_buffer_size_px = LCD_H_RES * 10` |
| Constant subtle wobble even without UI changes | CPU instruction fetches hit PSRAM during DMA bursts | Disable `CONFIG_SPIRAM_FETCH_INSTRUCTIONS` and `CONFIG_SPIRAM_RODATA` |
| Image unstable even with bounce buffer | PCLK too high for panel timing | Lower PCLK to 14 MHz |

## Next steps

1. **NVS config/provisioning UI** — replace compile-time WiFi with on-device
   setup and persisted settings.
2. **Display settings** — brightness and 180-degree rotation controls.
3. **Klipper parity** — metadata-based ETA and MMU gate/status detail.
4. **Touch/settings overlays** — popup menu, reboot/reset, calibration flow.

## Sources

- BOARD_CODING_NOTES (locally verified pins, timings, calibration):
  `../../BOARD_CODING_NOTES.md`
- ESP-IDF RGB LCD:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html
- esp_lvgl_port:
  https://components.espressif.com/components/espressif/esp_lvgl_port
- esp_lcd_touch_gt911:
  https://components.espressif.com/components/espressif/esp_lcd_touch_gt911
- LVGL 9 docs: https://docs.lvgl.io/9/
