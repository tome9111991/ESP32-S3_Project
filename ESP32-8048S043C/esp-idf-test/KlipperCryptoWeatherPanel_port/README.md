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
- Tap the weather icon on the time screen to open the weather detail page
  with current extras and a 5-day forecast
- Tap left/right to switch screens; auto-rotation cycles enabled screens with
  per-screen durations
- Long-press anywhere opens a popup with Settings / Reboot / Firmware /
  Factory-Reset (factory reset clears the whole NVS partition and reboots back
  to the compile-time defaults)
- Firmware page shows build metadata and can check/install OTA updates from
  GitHub Releases when matching release assets are available
- Settings menu has six real sub-screens:
  - **WLAN** — SSID list + on-screen keyboard, credentials in NVS
  - **Screens** — enable/disable each dashboard screen, per-screen rotation
    duration (5..120 s)
  - **Crypto** — base / quote / chart-timeframe (15M / 1H / 6H / 1D)
  - **Display** — brightness slider (32..255) with live preview, night-mode
    toggle, 180-degree rotation toggle (reboot on change)
  - **Touch calibration** — 5-point flow that stores affine map in NVS and
    honors the current 180-degree rotation
  - **Location** — latitude/longitude editor with keyboard, persisted in NVS
- Backlight runs on LEDC PWM (250 Hz, 8 bit); day/night brightness follows the
  computed sun position (full brightness from sunrise+90 min, night value
  after sunset; night dimming can be disabled in Display settings)
- Fetches weather from Open-Meteo, reverse-geocoded location names from
  BigDataCloud, crypto from Coinbase (spot, 24h stats, candles),
  and printer data from Moonraker (`/server/info`, `/printer/info`,
  `/printer/objects/query`, `/server/files/metadata`,
  `/server/database/item`) using native ESP-IDF HTTP + cJSON; MMU tool / gate /
  colors / status are rendered on the Klipper screen when present

## Hardware

Bill of Materials:

Some links may be affiliate links. If you buy through them, I may earn a small
commission at no extra cost to you.

| Qty | Part | Source |
| ---: | --- | --- |
| 1 | ESP32-8048S043C ESP32-S3 HMI display | [Amazon](https://amzn.to/4uZAayc), [AliExpress](https://s.click.aliexpress.com/e/_c4ahltJL), select `Color: Capacitive touch` |
| 4 | M3x6 cylinder head screws | [Amazon](https://amzn.to/430va00), [AliExpress](https://s.click.aliexpress.com/e/_c3xgcUUx), select `50pcs M3x6mm` |
| 4 | M3 heat-set inserts, 5x4 mm | [Amazon](https://amzn.to/4wgW0P7), [AliExpress](https://s.click.aliexpress.com/e/_c3v6WnvD), `30pcs` |

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

On the first build the component manager downloads LVGL, esp_lvgl_port,
esp_lcd_touch_gt911 and cJSON from the Espressif Component Registry — this
can take about a minute.

```bash
idf.py build
idf.py -p COMx flash monitor
```

`Ctrl+]` exits the monitor.

The local helper `build.ps1` defaults to
`idf.py -B C:\espbuild\kwp_8048 -p COM4 build` and forwards extra arguments
to `idf.py`.

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
    ├── version.h                  # firmware metadata + OTA release defaults
    ├── i18n.h                     # UI string catalog (X-Macro + T(id))
    ├── i18n.c                     # compile-time language selection
    ├── main.c                     # panel + touch + LVGL host loop
    ├── net_fetcher.c              # WiFi/NTP/Open-Meteo/Coinbase/Moonraker
    ├── ota_service.c/.h           # GitHub Releases OTA check/install
    ├── display_brightness.c       # LEDC PWM + sun-based day/night curve
    ├── touch_calibration.c        # CAL map, NVS, 5-point UI
    ├── ui_screens.c               # the four dashboard screens
    ├── ui_weather_detail.c        # detail page for current + 5-day weather
    ├── ui_chart.c                 # candle chart renderer
    ├── ui_popup.c                 # long-press popup (Settings/Reboot/FW/Reset)
    ├── ui_firmware_info.c         # firmware metadata + OTA UI
    ├── ui_settings_menu.c         # settings list (port of 12_SettingsMenu)
    ├── ui_wifi_setup.c            # WLAN sub-screen (scan + keyboard)
    ├── ui_keyboard.c              # on-screen keyboard widget
    ├── ui_screen_settings.c       # enable/disable + per-screen durations
    ├── ui_crypto_settings.c       # base / quote / timeframe selector
    ├── ui_display_settings.c      # brightness, night mode, rotate-180
    ├── ui_location_settings.c     # runtime latitude/longitude editor
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
| `esp_driver_ledc` | ESP-IDF built-in | Backlight PWM |
| `esp_timer` | ESP-IDF built-in | Timestamps |
| `esp_wifi`, `esp_event`, `esp_netif` | ESP-IDF built-in | WiFi station mode |
| `nvs_flash` | ESP-IDF built-in | Runtime settings |
| `esp_http_client` | ESP-IDF built-in | REST API requests |
| `esp_https_ota`, `app_update` | ESP-IDF built-in | OTA install + rollback |
| `esp-tls`, `mbedtls` | ESP-IDF built-in | TLS and certificate bundle |
| `lwip` | ESP-IDF built-in | DHCP/NTP networking |
| `espressif/cjson` | Component Registry | JSON parsing |
| `espressif/esp_lvgl_port` | Component Registry | LVGL integration for ESP boards |
| `espressif/esp_lcd_touch_gt911` | Component Registry | GT911 I2C driver |
| `lvgl/lvgl` | Component Registry | UI library |

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
| Display | `display/day_bright`, `display/night_en`, `display/rotate180` | Display sub-screen |
| Screens | `screens/time`, `screens/price`, `screens/chart`, `screens/klipper`, `screens/d_*` | Screens sub-screen |
| Crypto | base, quote, timeframe | Crypto sub-screen |
| Location | `location/lat`, `location/lon` | Location sub-screen |
| Touch | calibration affine map | Touch sub-screen |

Fields not (yet) exposed via UI — timezone, Coinbase vs. another service,
price prefix, Klipper base URL — stay in `config_private.h`.

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

### Firmware metadata and OTA

`main/version.h` provides fallback values for:

| Macro | Default | Purpose |
|---|---|---|
| `APP_FW_NAME` | `KCWPv2` | Release tag prefix and firmware screen name |
| `APP_FW_VERSION` | `20260513` | Current version; compared lexicographically |
| `APP_FW_BOARD` | `ESP32-8048S043C` | OTA asset selector |
| `APP_OTA_REPO_OWNER` / `APP_OTA_REPO_NAME` | local defaults | GitHub Releases source |

These can be overridden from CMake, for example:

```bash
idf.py build -DAPP_FW_VERSION="20260514" -DAPP_FW_BOARD="ESP32-8048S043C"
```

The OTA checker scans GitHub Releases for tags named `APP_FW_NAME-<version>`
and expects an asset named:

```text
APP_FW_NAME-APP_FW_BOARD-APP_FW_LANG.bin
```

`APP_FW_LANG` is `DE` or `EN` from the compile-time language selection. OTA
installation pauses the normal API fetch loop, uses the OTA slots from
`partitions.csv`, and relies on bootloader rollback. The release API call uses
the certificate bundle; the large firmware stream intentionally skips server
certificate verification to reduce heap pressure during the download.

## Technical decisions

A few non-obvious things that were learned the hard way and are worth
documenting:

### RGB framebuffers, DIRECT render mode and rotation

The RGB panel itself is initialized with `num_fbs = 2` in PSRAM. In normal
orientation, `direct_mode = true` and `avoid_tearing = true` are used together:
`esp_lvgl_port` treats the two panel FBs as front/back buffers and swaps them
on VSYNC. If that fast path is tested again, keep those two flags coupled.

With the current default `DISPLAY_ROTATE_180_DEFAULT = 1`, the port deliberately
uses a 20-line LVGL partial buffer instead of the direct full-frame path. That
flushes through `esp_lcd_panel_draw_bitmap()`, which honors the 180-degree
rotation reliably on this RGB panel.

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

### 180-degree rotation persists in NVS

The Display settings screen persists `display/rotate180` and reboots after a
change so the buffer layout, touch map, and LVGL display state start cleanly.
The touch coordinate callback returns panel pixels; LVGL applies display
rotation internally, while the local touch polling timer mirrors manually for
screen-switch detection and the long-press feedback arc.

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
| OTA check finds no update | Release tag/asset does not match | Use `APP_FW_NAME-<version>` tag and `APP_FW_NAME-APP_FW_BOARD-APP_FW_LANG.bin` asset |

## Open items

Feature parity with the Arduino sketch is reached. See `TODO.md` for the
remaining items, which are mostly nice-to-have:

- Web-based config, if runtime configuration should move beyond the panel UI.
- API status / diagnostics screen.
- README screenshots.
- Release workflow documentation for producing OTA assets.

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
- Open-Meteo forecast API: https://open-meteo.com/en/docs
- BigDataCloud Reverse Geocoding API:
  https://www.bigdatacloud.com/free-api/free-reverse-geocode-to-city-api
- Coinbase Exchange API: https://docs.cdp.coinbase.com/exchange/docs/welcome
- Moonraker API: https://moonraker.readthedocs.io/
