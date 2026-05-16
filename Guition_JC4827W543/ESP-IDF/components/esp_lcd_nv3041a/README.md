# ESP LCD NV3041A QSPI

Local ESP-IDF `esp_lcd` panel driver for the Guition JC4827W543 NV3041A
display.

This driver is for the board's QSPI/SPI wiring, not for generic 4-wire SPI
NV3041 modules with a DC pin.

## Board Defaults

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

The panel command framing follows the known-working LovyanGFX path:

- register command: `0x02 0x00 CMD 0x00`
- pixel write command: `0x32 0x00 0x2c 0x00`
- SPI mode `1`
- QSPI pixel clock `32 MHz`

## Use In An ESP-IDF Project

Add the shared component folder before `project()` in the project's top-level
`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_guition_app)
```

Then add `esp_lcd_nv3041a` to the app component requirements and include:

```cmake
idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd
                                esp_driver_spi
                                esp_lcd_nv3041a)
```

```c
#include "esp_lcd_nv3041a.h"
```
