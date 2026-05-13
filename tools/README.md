# tools

Project utilities. Run from the repo root.

## svg2lvgl.py — SVG to LVGL 9 image converter

Rasters an SVG **in memory** (no temporary PNG file) and emits a C file with
an `lv_image_dsc_t` ready to drop into an LVGL 9 project. Default output is
`LV_COLOR_FORMAT_RGB565A8` (RGB565 plane + separate 8-bit alpha plane), so
transparent SVG backgrounds stay transparent — no hand-editing of the
generated C file needed.

Fixes two recurring issues from the stock LVGL online converter:
- C identifiers with dashes (e.g. `status-moon`) get sanitized to underscores.
- Transparent SVG backgrounds no longer get baked in as a solid color.

### Install

```powershell
python -m pip install -r tools/requirements.txt
```

Uses `resvg-py` (pure Rust, no native libs required on Windows).
`cairosvg` is supported as a fallback if already installed.

### Usage

```powershell
# Single file
python tools\svg2lvgl.py path\to\icon.svg --prefix icon_

# Whole directory + aggregate header with LV_IMAGE_DECLARE lines
python tools\svg2lvgl.py `
  Guition_JC4827W543\KlipperCryptoWeatherPanel\assets\icons `
  -o   Guition_JC4827W543\KlipperCryptoWeatherPanel\ui_assets `
  --prefix icon_ `
  --header Guition_JC4827W543\KlipperCryptoWeatherPanel\ui_assets.h

# Force size / pick ARGB8888 for full-quality gradients
python tools\svg2lvgl.py icon.svg --size 64x64 --format argb8888
```

### Options

| Flag | Default | Description |
| --- | --- | --- |
| `input` | — | SVG file or directory containing `*.svg` |
| `-o`, `--out` | next to input | Output directory for generated `.c` files |
| `--format` | `rgb565a8` | `rgb565a8` or `argb8888` |
| `--size WxH` | from SVG | Force output pixel size (e.g. `48x48`) |
| `--prefix` | (none) | Prefix prepended to every C identifier |
| `--header` | (none) | Also write an aggregate header with `LV_IMAGE_DECLARE(...)` lines |

### Identifier rules

`weather-clear.svg` with `--prefix icon_` becomes:
- Symbol: `icon_weather_clear`
- File: `icon_weather_clear.c`
- Macro: `LV_ATTRIBUTE_IMAGE_ICON_WEATHER_CLEAR`

Non-alphanumeric characters are collapsed to `_`. Leading digits get a `_`
prefix so the identifier is valid C.

### Using the generated images

```c
#include "ui_assets.h"   // contains LV_IMAGE_DECLARE(icon_weather_clear);

lv_obj_t * img = lv_image_create(parent);
lv_image_set_src(img, &icon_weather_clear);
```
