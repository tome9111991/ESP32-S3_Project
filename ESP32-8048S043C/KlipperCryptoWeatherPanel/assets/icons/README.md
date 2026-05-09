# Icon Assets

SVG source assets for the ESP32-8048S043C 800x480 LVGL UI.

- Canvas-sized weather icons use `93x80`, scaled from the Guition `56x48` assets by the 800/480 width ratio.
- Sun/moon status icons use `70x70`, scaled from the Guition `42x42` assets by the same ratio.
- `icon_status_offline.svg` uses `147x147`, scaled from the Guition `88x88` source asset.
- File names use the expected LVGL symbol names, for example `icon_weather_clear.svg` should convert to `icon_weather_clear`.
- These files are source assets only. The sketch uses converted LVGL C image arrays from `src/ui_assets/`.

For LVGL on the ESP32-S3, keep these SVGs as editable originals and regenerate `src/ui_assets/` when icon geometry changes.
