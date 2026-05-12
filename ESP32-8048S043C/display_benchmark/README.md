# ESP32-8048S043C Display Benchmark

Automatic stress sketch for finding a stable RGB display configuration on the
ESP32-8048S043C. It keeps the known pinout and `esp_lcd` RGB path, then runs a
small configuration matrix while drawing fast full-screen patterns.

## What It Can And Cannot Measure

The ESP32 cannot read the actual LCD pixels back from this RGB panel, so visual
corruption cannot be detected automatically. This sketch makes corruption easy
to see and logs timing data over serial:

- flicker
- tearing
- shifted lines
- colored snow
- corrupted rectangles
- transfer callback timeouts
- frame timing under CPU and PSRAM load

## Automatic Test Matrix

Change `BENCH_RUNS` at the top of `display_benchmark.ino` only when you want a
different matrix. One upload runs all entries automatically; every run recreates
the RGB panel so `pclk` and `bounce_lines` really change.

Default order:

| Run | `pclk_hz` | `bounce_lines` | `num_fbs` | `double_fb` |
| --- | ---: | ---: | ---: | --- |
| A | `18000000` | `12` | `2` | `true` |
| B | `16000000` | `10` | `2` | `true` |
| C | `14000000` | `10` | `2` | `true` |
| D | `10000000` | `10` | `2` | `true` |
| E | `14000000` | `4` | `2` | `true` |
| F | `14000000` | `0` | `2` | `true` |
| G | `14000000` | `16` | `2` | `true` |

This matrix keeps a 14 MHz / 10-line reference as Run C, then spreads wider
across pixel clock and bounce-buffer extremes. Run A pushes the pixel clock
higher, Run D checks a much lower clock, Run F tests no bounce buffer, and Run G
tests a larger bounce buffer. If a run shows the wrong picture instead of the
same diagnostic pattern, treat it as bad even when the serial timing still says
`timing-ok`.

## Serial Output

Use 115200 baud on the CH340/UART serial port. Each phase prints lines like:

```text
PSRAMBench: test=write, buffer=..., loops=..., elapsed=..., throughput=... MB/s, checksum=...
Stats: run=A, phase=mixed cpu+psram load, frames=..., fps=..., draw_avg=..., present_avg=..., timeouts=...
RunSummary: run=A, pclk=..., bounce_lines=..., frames=..., fps=..., timeouts=..., result=timing-ok
```

The `PSRAMBench` lines run once before the display benchmark starts. They measure
CPU access to the PSRAM buffer without active RGB display DMA:

- `write`: sequential PSRAM writes
- `read`: sequential PSRAM reads
- `read_modify_write`: read, modify, and write back

Use these numbers as a baseline. Phase 3 and 4 then show how much frame drawing
and presenting slow down when PSRAM is stressed while the RGB panel is active.

Treat a configuration as suspect when:

- the panel shows visible corruption in any phase
- `timeouts` is non-zero
- `present_max` repeatedly spikes far above the average
- the real application still shows artifacts after the benchmark looked stable

`RunSummary` only judges timing. If the LCD visibly glitches, mark that run as
bad even when the summary says `timing-ok`.

## Observed Results

Latest wide test notes:

| Run | Visual result | Decision |
| --- | --- | --- |
| A | Many horizontal stripes, especially from phase 3. | Bad |
| B | Similar to A, slightly less distortion but still stripes. | Bad |
| C | Phase 1 and 2 looked good; phase 3/4 lagged only under PSRAM load. | Best candidate |
| D | Display showed full-screen colors instead of the diagnostic pattern. | Bad |
| E | Image wobble and horizontal stripes. | Bad |
| F | Mostly okay, but occasional full-screen flicker; more flicker/shake from phase 3. | Borderline |
| G | Phase 1 and 2 looked good; phase 3/4 lagged under PSRAM load. | Reserve candidate |

Phase 3 and 4 are expected to lag because the benchmark intentionally stresses
PSRAM while the framebuffer also lives in PSRAM. Low FPS in these phases is not
automatically a failure. Wrong colors, shifted lines, full-screen color fills,
flicker, or broken diagnostic markers are failures.

## Suggested Winner

For GUI use, prefer the fastest setting that survives all benchmark phases and
the real application:

```cpp
num_fbs = 2
double_fb = true
fb_in_psram = true
```

Current best setting from the observed tests:

```cpp
pclk_hz = 14000000
bounce_lines = 10
num_fbs = 2
double_fb = true
fb_in_psram = true
```

If the real application still shows artifacts with this setting, retest
`bounce_lines = 16` as the first fallback.
