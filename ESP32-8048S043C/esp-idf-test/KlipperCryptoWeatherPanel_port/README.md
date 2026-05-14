# ESP32-8048S043C Klipper Crypto Weather Panel Port

ESP-IDF project for the **ESP32-8048S043C** (4.3" 800x480 RGB panel with
GT911 touch). This is the native IDF port of the Arduino
`KlipperCryptoWeatherPanel` dashboard, built on the already verified
RGB-panel + GT911 + LVGL scaffold.

## What the app currently does

- Initializes the RGB panel with double framebuffer in PSRAM and a bounce
  buffer in internal SRAM
- Reads GT911 touch over I2C and calibrates it to screen pixels via the driver
  callback; 5-point recalibration UI available from the settings menu
- Starts LVGL via `esp_lvgl_port`
- Shows the four dashboard screens: time/weather, live crypto price, crypto
  candle chart, and Klipper/Moonraker status
- Tap left/right to switch screens; auto-rotation cycles enabled screens with
  per-screen durations
- Long-press anywhere opens a popup with Settings / Reboot / Factory-Reset
  (factory reset clears the whole NVS partition and reboots back to the
  compile-time defaults)
- Settings menu has five real sub-screens:
  - **WLAN** — SSID list + on-screen keyboard, credentials in NVS
  - **Screens** — enable/disable each dashboard screen, per-screen rotation
    duration (5..120 s)
  - **Crypto** — base / quote / chart-timeframe (15M / 1H / 6H / 1D)
  - **Display** — brightness slider (32..255) with live preview, 180-degree
    rotation toggle (reboot on change)
  - **Touch calibration** — 5-point flow that stores affine map in NVS and
    honors the current 180-degree rotation
- Backlight runs on LEDC PWM (250 Hz, 8 bit); day/night brightness follows the
  computed sun position (full brightness from sunrise+90 min, night value after
  sunset)
- Fetches weather from Bright Sky, crypto from Coinbase (spot, 24h stats,
  candles), and printer data from Moonraker (`/server/info`,
  `/printer/info`, `/printer/objects/query`, `/server/files/metadata`) using
  native ESP-IDF HTTP + cJSON; MMU tool / gate / colors / status are rendered
  on the Klipper screen when present

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
├── TODO.md                        # current open items
├── partitions.csv                 # 4 MB factory + 2x 4 MB OTA + ~4 MB SPIFFS
├── sdkconfig.defaults             # board-specific IDF defaults
└── main/
    ├── CMakeLists.txt             # component sources/requires
    ├── Kconfig.projbuild          # app-level menuconfig options (UI language)
    ├── idf_component.yml          # managed dependencies (LVGL, touch)
    ├── app_state.h                # shared state, constants, public APIs
    ├── config_private.example.h   # copy to config_private.h locally
    ├── i18n.h                     # UI string catalog (X-Macro + T(id))
    ├── i18n.c                     # compile-time language selection
    ├── main.c                     # panel + touch + LVGL host loop
    ├── net_fetcher.c              # WiFi/NTP/Bright Sky/Coinbase/Moonraker
    ├── display_brightness.c       # LEDC PWM + sun-based day/night curve
    ├── touch_calibration.c        # CAL map, NVS, 5-point UI
    ├── ui_screens.c               # the four dashboard screens
    ├── ui_chart.c                 # candle chart renderer
    ├── ui_popup.c                 # long-press popup (Settings/Reboot/Reset)
    ├── ui_settings_menu.c         # settings list (port of 12_SettingsMenu)
    ├── ui_wifi_setup.c            # WLAN sub-screen (scan + keyboard)
    ├── ui_keyboard.c              # on-screen keyboard widget
    ├── ui_screen_settings.c       # enable/disable + per-screen durations
    ├── ui_crypto_settings.c       # base / quote / timeframe selector
    ├── ui_display_settings.c      # brightness + rotate-180
    ├── ui_font_price_digits.*     # large crypto price font
    ├── ui_font_time_digits.*      # large clock font
    ├── ui_assets.h                # icon table
    └── ui_assets/                 # LVGL image C-arrays (weather, status, ...)
```

`sdkconfig` is .gitignored and gets generated from `sdkconfig.defaults` on
the first `idf.py reconfigure`. `partitions.csv` provides 4 MB factory +
2× 4 MB OTA slots + a SPIFFS storage partition in the remaining flash. When
changing defaults: delete `sdkconfig` and regenerate.

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

The compile-time values act as defaults; once the device has been configured
through the on-screen settings menu, the corresponding runtime values come from
NVS. A factory reset wipes the NVS partition, so the build-time defaults take
over again — which is intentional, so the device is always usable after a
reset without needing a console.

### Runtime settings (NVS-persisted)

| Domain | Keys | Source |
|---|---|---|
| WiFi | `wifi/ssid`, `wifi/pass` | WLAN sub-screen |
| Display | `display/day_bright`, `display/rotate180` | Display sub-screen |
| Screens | enable + duration per screen | Screens sub-screen |
| Crypto | base, quote, timeframe | Crypto sub-screen |
| Touch | calibration affine map | Touch sub-screen |

Fields not (yet) exposed via UI — location/timezone, Coinbase vs. another
service, Klipper base URL — stay in `config_private.h`.

### UI language (compile-time)

The UI language is selected at build time, not at runtime. `idf.py menuconfig`
→ **App Localization → UI language** offers `Deutsch` (default) or `English`.
Only the selected language ends up in the binary — no runtime overhead.

All user-facing strings live in `main/i18n.h` as an X-Macro list
(`id, deutsch, english`). New strings get added there once and are referenced
in the UI code via `T(ID)`:

```c
make_label(..., T(SETTINGS_TITLE));   // "Einstellungen" / "Settings"
```

Switching the language with `menuconfig` changes `sdkconfig.h`, which most
TUs include transitively → expect a near-full recompile after the switch.
Editing only `i18n.c` (e.g. tweaking a translation) rebuilds just that file.

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

### 180-degree display rotation via panel mirror

`DISPLAY_ROTATE_180_DEFAULT = 1` matches the Arduino sketch. The actual rotation
flag is loaded from NVS (`display/rotate180`) at boot and applied via
`LV_DISPLAY_ROTATION_180` after the RGB display is added. The touch coordinate
callback returns panel pixels; LVGL applies the rotation internally via
`lv_display_rotate_point`, and the local touch polling timer mirrors manually
for screen-switch detection and the feedback arc. With rotation enabled, LVGL
uses a small 20-line partial buffer and flushes through
`esp_lcd_panel_draw_bitmap()`, since that path honors the panel mirror on RGB
panels. Software rotation stays disabled — no extra rotation buffer. Toggling
the option in the Display sub-screen persists to NVS and triggers a clean
reboot so the buffer layout and touch map come up in the new orientation.

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

## Open items

Feature parity with the Arduino sketch is reached. See `TODO.md` for the
remaining items, which are mostly nice-to-have:

- OTA / web-based config (OTA partitions are already laid out).
- API status / diagnostics screen.
- README screenshots.

Two refactor ideas (`net_fetcher.c` split into per-service files, `g_app` write
setters) are tracked but intentionally on hold — the current shape is
mechanical and works; refactoring is only worth it once a service grows real
new complexity.

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
