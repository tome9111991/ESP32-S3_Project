# ESP32-8048S043C Clean Template

Minimal ESP-IDF project template, pre-configured for the **ESP32-8048S043C** board.

## Hardware

- **Chip:** ESP32-S3-WROOM-1 N16R8
- **Flash:** 16 MB (QIO, 80 MHz)
- **PSRAM:** 8 MB Octal (80 MHz)
- **Display:** 4.3" 800x480 RGB LCD
- **Touch:** GT911 capacitive (I2C)
- **CPU:** 240 MHz dual-core Xtensa LX7

## What's pre-configured

The `sdkconfig.defaults` file ships with sensible, safe optimizations:

| Setting | Value | Why |
|---|---|---|
| Target | esp32s3 | Required for this chip |
| CPU frequency | 240 MHz | Maximum |
| Flash mode | QIO @ 80 MHz | Maximum safe throughput |
| Flash size | 16 MB | Matches hardware |
| PSRAM | 8 MB Octal @ 80 MHz | Matches hardware |
| PSRAM XIP | Instructions + rodata in PSRAM | Frees internal SRAM |
| CPU cache | 64 kB I-cache + 64 kB D-cache, 64-byte lines | Reduces PSRAM access penalty |
| FreeRTOS tick | 1000 Hz | Smoother UI/touch response |
| Main task stack | 8 kB | Headroom for LVGL etc. |
| Compiler | `-O2` (Performance) | Application + bootloader |
| USB Serial/JTAG | Enabled | Console over native USB |
| Brownout level | 7 (most tolerant) | RGB panel + WiFi current spikes |

What is **not** pre-configured (add only when you need it):

- Custom partition table (default works for ~1 MB apps)
- WiFi / TLS / LWIP tuning
- LCD/Touch driver components
- Power management (would hurt display latency)
- Experimental 120 MHz PSRAM (unstable across chip exemplars)

## Requirements

- ESP-IDF v5.x or v6.x installed
- VS Code with the **Espressif ESP-IDF Extension**
- USB-to-UART driver for the board (CH340 or CP210x depending on revision)

## How to use this template

1. **Copy** the entire `clean_template` folder to your projects directory and rename it:
   ```
   clean_template  ->  my_new_project
   ```

2. **Open** the new folder in VS Code.

3. **Rename the project** in the root `CMakeLists.txt`:
   ```cmake
   project(esp32_8048s043c_app)   ->   project(my_new_project)
   ```
   This name becomes the output binary filename.

4. **Configure VS Code's ESP-IDF extension** (status bar at the bottom):
   - Select ESP-IDF installation
   - Select target: `esp32s3`
   - Select COM port
   - Select flash method: `UART`

5. **First build** — `sdkconfig` will be generated automatically from `sdkconfig.defaults`:
   ```
   idf.py build
   ```

6. **Flash + monitor**:
   ```
   idf.py flash monitor
   ```
   Exit monitor with `Ctrl+]`.

## Expected boot log

On first successful boot you should see:

```
I (xxx) esp_psram: Found 8MB PSRAM device
I (xxx) esp_psram: Speed: 80MHz
I (xxx) mmu_psram: Read only data copied and mapped to SPIRAM
I (xxx) mmu_psram: Instructions copied and mapped to SPIRAM
I (xxx) cpu_start: cpu freq: 240000000 Hz
...
I (xxx) app: Hello from ESP32-8048S043C template
```

If `PSRAM` is missing, `cpu freq` is 160 MHz, or flash size shows as 2 MB, the
defaults were not applied — delete `sdkconfig` and rebuild.

## Project layout

```
.
|-- .clangd                    clangd compile flag filter
|-- .devcontainer/             optional Dockerized IDF environment
|-- .gitignore                 ignores build artifacts + machine-specific VS Code settings
|-- .vscode/
|   |-- c_cpp_properties.json  IntelliSense config (portable)
|   |-- launch.json            GDB debug config (portable)
|   `-- settings.json          machine-specific (gitignored)
|-- CMakeLists.txt             root project file
|-- main/
|   |-- CMakeLists.txt         component registration
|   `-- main.c                 app_main entry point
|-- sdkconfig.defaults         board-tuned IDF defaults
`-- README.md                  this file
```

## Notes on regenerating sdkconfig

`sdkconfig` is generated from `sdkconfig.defaults` on first configure. After
that, changes you make in the SDK Config Editor (or `idf.py menuconfig`) are
written to `sdkconfig` only, **not** back to `sdkconfig.defaults`.

To restore the documented defaults:

1. Delete the `sdkconfig` file (and ideally `idf.py fullclean` to drop `build/`)
2. Re-run the SDK Config Editor or `idf.py reconfigure`

`sdkconfig` is `.gitignored` on purpose — only `sdkconfig.defaults` is tracked.

## License

Personal template, use as you like.
