# ESP32-S3 HMI Projects

Public Arduino workspace for ESP32-S3 HMI display projects, board bring-up notes, display tests, experiments, and publishable standalone projects.

The repository currently covers two different ESP32-S3 HMI boards. Some folders contain complete application sketches that may be useful as standalone projects. Other folders are intentionally kept as small test sketches or experiments for validating display drivers, touch input, timing, framebuffers, and board behavior.

Some application sketches are board-specific Arduino/LVGL dashboards, while others are focused hardware tests or implementation experiments.

Code is organized by hardware target. Each board folder contains its own Arduino sketches, display notes, assets, and setup documentation.

## Project Scope and Transparency

This is an independent DIY firmware and documentation repository. It is not
affiliated with, endorsed by, certified by, or sponsored by any hardware,
software, music service, or platform vendor unless explicitly stated.

Some project pages or hardware lists may contain affiliate links or donation
links. If you use those links, the maintainer may receive a small commission or
donation at no extra cost to you. Such links help support development, but they
do not change the technical recommendations, project license, or the project's
independent status.

Third-party product names are used only to describe compatibility or the tested
hardware/software environment. Logos, certification badges, and vendor-owned
assets are not included unless their use is clearly permitted.

## Web Flasher

A browser-based flasher lives on GitHub Pages:

**<https://tome9111991.github.io/ESP32-S3_Project/>**

It runs entirely client-side (no install) and uses WebSerial, so it works in **Chrome or Edge** only. A short wizard guides through Board → Project → Version/Configuration, then the firmware can be installed directly over USB or downloaded as a `.bin`.

| Project | Source of firmware |
| --- | --- |
| ESP32-8048S043C — Klipper Crypto Weather Panel V2 | Pulled from GitHub releases. Picks language (DE/EN) and version. |
| Guition JC4827W543 — Klipper Crypto Weather Panel | Builds a personalised firmware on demand: a config form collects WiFi / location / Klipper URL, a Cloudflare Worker triggers the build workflow and exposes the resulting binary as a short-lived release. Bot protection via Cloudflare Turnstile. |

Pre-configured URLs can be shared, e.g.
`?board=esp32-8048s043c&project=kcwpv2&version=KCWPv2-20251115&lang=DE`.

## Repository Layout

| Path | Purpose |
| --- | --- |
| `ESP32-8048S043C/` | Board support, tests, and application sketches for the ESP32-8048S043C 800 x 480 RGB-panel board. |
| `ESP32-8048S043C/KlipperCryptoWeatherPanel/` | Dashboard sketch for the ESP32-8048S043C. |
| `ESP32-8048S043C/displaytest_esp_lcd_doublefb/` | Direct `esp_lcd` RGB display/touch test using double framebuffer in PSRAM. Recommended baseline for this board. |
| `ESP32-8048S043C/displaytest/` | Arduino_GFX display/touch test for basic validation. |
| `ESP32-8048S043C/displaytest_lovyan/` | LovyanGFX RGB experiment kept for reference. |
| `ESP32-8048S043C/esp-idf-test/SonosPlayer/` | Unofficial ESP-IDF DIY controller/display for local Sonos speakers. |
| `ESP32-8048S043C/BOARD_CODING_NOTES.md` | Hardware notes, tested timings, GT911 touch details, and board-specific implementation guidance. |
| `Guition_JC4827W543/` | Board support, tests, and application sketches for the Guition JC4827W543 480 x 272 HMI display. |
| `Guition_JC4827W543/KlipperCryptoWeatherPanel/` | Dashboard sketch for the Guition JC4827W543. |
| `Guition_JC4827W543/DisplayPixelRaster/` | LVGL raster/orientation test sketch for the Guition display. |
| `Guition_JC4827W543/BOARD_CODING_NOTES.md` | Hardware notes, render-path guidance, and implementation notes for the Guition NV3041A/QSPI panel. |
| `docs/` | Static GitHub Pages site for the [Web Flasher](#web-flasher). |
| `worker/` | Cloudflare Worker that dispatches on-demand Guition builds and reports their status to the flasher. |
| `LICENSE.md` | Non-commercial project license. |

## Arduino Sketches

Open the folder that contains the sketch you want to build in the Arduino IDE, then open that sketch's main `.ino` file.

The numbered `.ino` files in those folders are Arduino tabs and are compiled together with the main sketch. Do not open those tabs as standalone sketches.

## Project Maturity

This repository mixes different maturity levels on purpose:

| Type | Meaning |
| --- | --- |
| Complete projects | Usable sketches with their own README, assets, configuration template, and expected setup flow. |
| Board tests | Small sketches for validating display output, touch input, pinouts, PSRAM, and driver behavior. |
| Experiments | Kept for reference when comparing libraries, render paths, timing, or hardware behavior. |
| Notes | Hardware findings, tested values, caveats, and implementation decisions gathered during board bring-up. |

If you are looking for something to use directly, start with one of the complete project folders. If you are debugging a board, start with the smallest matching test sketch and the board-specific notes.

## Hardware Targets

### ESP32-8048S043C

- ESP32-S3 HMI board with 800 x 480 RGB565 parallel RGB display
- GT911 capacitive touch over I2C
- backlight on GPIO 2
- tested locally with 16 MB flash and 8 MB PSRAM
- application sketches may use `esp_lcd` RGB panel setup and PSRAM-backed display buffers

Start with `ESP32-8048S043C/BOARD_CODING_NOTES.md` when changing display, touch, timing, or framebuffer behavior.

### Guition JC4827W543

- ESP32-S3 HMI board with 480 x 272 NV3041A display
- QSPI/SPI display path through LovyanGFX
- backlight on GPIO 1
- application sketches may use LVGL with a full-screen PSRAM buffer for stable rendering

Start with `Guition_JC4827W543/BOARD_CODING_NOTES.md` before changing the display flush path or LVGL render mode.

## Software Requirements

Install these through the Arduino IDE / Library Manager unless your local setup already provides them:

- Arduino IDE
- ESP32 Arduino core with ESP32-S3 support
- LVGL v9
- ArduinoJson
- LovyanGFX for the Guition JC4827W543 port
- Arduino_GFX only for the ESP32-8048S043C `displaytest` reference sketch

Recommended board profile for both targets is usually `ESP32S3 Dev Module`, with PSRAM enabled. Use the board-specific README for flash size, partition scheme, USB CDC, and upload settings.

## Project Setup

Project-specific setup, configuration, required libraries, and upload settings are documented inside the relevant project or board folder when needed.

## Assets and Enclosures

Some project folders include editable icon sources, generated LVGL image assets, screenshots, font generation helper scripts, or enclosure files.

Keep generated C image assets and source SVG assets together when moving or modifying a board port.

## Suggested Workflow

For a new or unverified board:

1. Choose the target board folder.
2. Read the board-specific hardware notes.
3. Run the smallest relevant display/touch test sketch first.
4. Confirm pinout, display orientation, touch mapping, PSRAM, and serial output.
5. Move to a larger application sketch only after the board basics are stable.

For application sketches:

1. Open the matching main `.ino` sketch.
2. Follow the setup notes in that sketch or project folder.
3. Confirm the required board settings in the Arduino IDE.
4. Compile and upload.
5. Use the Serial Monitor for boot diagnostics and runtime logs when the sketch provides them.

## License

This repository is licensed under the PolyForm Noncommercial License 1.0.0. Commercial use is not permitted without prior written permission from the copyright holder.

See `LICENSE.md` for the full license text and attribution requirements.
