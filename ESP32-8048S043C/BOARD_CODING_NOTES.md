# ESP32-8048S043C Coding Notes

Status: 2026-05-08, cleaned up after local display and touch tests

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
- PCLK: `16000000`, test `14000000` if unstable
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

- verify that PSRAM is enabled
- test `LCD_BOUNCE_LINES` from `10` down to `0`
- lower PCLK from `16000000` to `14000000`

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

- ESP3D, Sunton 4.3 ESP32-8048S043C: https://esp3d.io/esp3d-tft/version_1x/hardware/esp32-s3/sunton-43-8048/
- HomeDing Panel ESP32-8048S043C: https://homeding.github.io/boards/esp32s3/panel-8048S043.htm
- ESPHome Devices, Sunton ESP32-8048S043C: https://devices.esphome.io/devices/sunton-esp32-8048s043c/
- Espressif RGB LCD / esp_lcd: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html
- Espressif ESP32-S3-WROOM-1/WROOM-1U Datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
