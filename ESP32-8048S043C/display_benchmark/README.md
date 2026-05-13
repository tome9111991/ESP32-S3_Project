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

With the default 9 runs, 5 phases, and `BENCH_PHASE_MS = 10000`, a full pass
takes about 7.5 minutes plus your visual confirmation pauses after each phase.

Default order:

| Run | `pclk_hz` | `bounce_lines` | `num_fbs` | `double_fb` | `bb_invalidate_cache` |
| --- | ---: | ---: | ---: | --- | --- |
| A | `18000000` | `12` | `2` | `true` | `false` |
| B | `16000000` | `10` | `2` | `true` | `false` |
| C | `14000000` | `10` | `2` | `true` | `false` |
| D | `14000000` | `8` | `2` | `true` | `false` |
| E | `14000000` | `4` | `2` | `true` | `false` |
| F | `14000000` | `0` | `2` | `true` | `false` |
| G | `14000000` | `16` | `2` | `true` | `false` |
| H | `14000000` | `10` | `2` | `false` | `false` |
| I | `14000000` | `10` | `2` | `true` | `true` |

This matrix keeps a 14 MHz / 10-line reference as Run C, then spreads wider
across pixel clock and bounce-buffer variants. Run A pushes the pixel clock
higher, Run D checks an 8-line bounce buffer between the 4/10/16-line cases,
Run F tests no bounce buffer, and Run G tests a larger bounce buffer. Run H
checks whether disabling `double_fb` behaves like the real app's current panel
setup, and Run I checks whether
`bb_invalidate_cache` helps or hurts. If a run shows the wrong picture instead
of the same diagnostic pattern, treat it as bad even when the serial timing
still says `timing-ok`.

## Serial Output

Use 115200 baud on the CH340/UART serial port. Each phase prints lines like:

```text
PSRAMBench: test=write, buffer=..., loops=..., elapsed=..., throughput=... MB/s, checksum=...
Stats: run=A, phase=psram read_modify_write load, frames=..., fps=..., draw_avg=..., present_avg=..., msync_avg=..., submit_avg=..., wait_avg=..., draw_pct=..., present_pct=..., wait_pct=..., stress_chunks_s=..., fb_write_min=... MB/s, psram_stress=... MB/s, panel_psram_read_est=... MB/s, psram_total_est=... MB/s
RunSummary: run=A, pclk=..., refresh_est=..., bounce_lines=..., double_fb=..., bb_invalidate_cache=..., frames=..., fps=..., timeouts=..., draw_max=..., present_max=..., msync_max=..., submit_max=..., wait_max=..., fb_write_min_avg=... MB/s, psram_stress_avg=... MB/s, panel_psram_read_est=... MB/s, psram_total_est_avg=... MB/s, result=timing-ok
VisualResult: run=A, phase=psram read_modify_write load, visual=good
```

The `PSRAMBench` lines run once before the display benchmark starts. They measure
CPU access to the PSRAM buffer without active RGB display DMA:

- `write`: sequential PSRAM writes
- `read`: sequential PSRAM reads
- `read_modify_write`: read, modify, and write back

Use these numbers as a baseline. The display benchmark phases then split the
load into CPU-only, PSRAM read-only, PSRAM write-only, and PSRAM
read-modify-write so read/write contention can be compared while the RGB panel
is active.

The per-phase `Stats` lines also estimate PSRAM bus pressure while the display
benchmark is running:

- `draw_avg`: CPU time spent redrawing the test pattern into PSRAM
- `msync_avg`: cache sync time before handing the frame to the RGB driver
- `submit_avg`: time spent inside `esp_lcd_panel_draw_bitmap`
- `wait_avg`: time waiting for the RGB transfer callback
- `draw_pct`, `present_pct`, `wait_pct`: share of phase wall time spent there
- `stress_chunks_s`: stress-worker progress rate; high in CPU-only phases and
  much lower in PSRAM phases means the workers are blocked by memory access
- `fb_write_min`: minimum CPU write traffic from full-frame redraws
- `psram_stress`: measured stress-worker PSRAM traffic from counter deltas
- `panel_psram_read_est`: estimated RGB panel DMA read traffic from `pclk` and
  porch timing when `fb_in_psram = true`
- `psram_total_est`: sum of the three values above

`panel_psram_read_est` is an estimate because the ESP32 does not expose a direct
hardware counter for the RGB peripheral's PSRAM reads. `fb_write_min` is also a
lower bound because the diagnostic overlay overwrites some pixels more than
once.

Treat a configuration as suspect when:

- the panel shows visible corruption in any phase
- `timeouts` is non-zero
- `present_max` repeatedly spikes far above the average
- the real application still shows artifacts after the benchmark looked stable

`RunSummary` only judges timing. If the LCD visibly glitches, mark that run as
bad even when the summary says `timing-ok`.

After every phase, the sketch pauses on a colored confirmation screen. Tap the
left/middle/right colored panel or use one Serial Monitor key:

- left / `g`: visually good
- middle / `s`: skip / unsure
- right / `b`: visually bad
- `a`: mark this and all remaining phases good without further pauses

Touch uses the GT911 on SDA19/SCL20 with the saved 800x480 calibration from
`displaytest_esp_lcd_doublefb`. If touch is not detected, serial feedback still
works. The sketch clears old GT911 events before accepting feedback and requires
a short stable touch inside one of the colored panels, so stale touch events
should not auto-select an answer.

## Observed Results

Latest wide test notes:

| Run | Visual result | Decision |
| --- | --- | --- |
| A | Baseline and PSRAM write/RMW marked bad. | Bad |
| B | Mostly skip, only PSRAM read marked good. | Bad / inconclusive |
| C | Baseline, CPU, and PSRAM read marked good; PSRAM write/RMW marked skip. | Usable, but not clean |
| D | `14 MHz / bounce_lines=8`; mostly good, PSRAM write marked skip. | Reserve candidate |
| E | All phases marked bad. | Bad |
| F | Fastest timing, but all phases marked bad. | Bad despite FPS |
| G | Baseline good, later phases mostly bad. | Bad |
| H | All phases marked good with `double_fb=false`, `bb_invalidate_cache=false`. | Best candidate |
| I | All phases marked good with `double_fb=true`, `bb_invalidate_cache=true`. | Good fallback |

The PSRAM read, write, and read-modify-write phases are expected to lag because
the benchmark intentionally stresses PSRAM while the framebuffer also lives in
PSRAM. Low FPS in these phases is not automatically a failure. Wrong colors,
shifted lines, full-screen color fills, flicker, or broken diagnostic markers
are failures.

Latest timing notes:

- CPU-only load is not the main problem; the CPU phase keeps roughly the same
  behavior as the baseline.
- PSRAM write load is the worst stress case for both timing and visual
  stability. This matches GUI workloads that write large LVGL/canvas/framebuffer
  areas into PSRAM.
- `bounce_lines = 0` improves FPS, but Run F was visually bad in every phase.
- `bounce_lines = 8` and `10` are close on timing; visual feedback currently
  favors the 10-line Run H.
- `double_fb` and `bb_invalidate_cache` do not meaningfully change the measured
  FPS, but they can still change visual stability.

## Suggested Winner

For GUI use, prefer the fastest setting that survives all benchmark phases
visually, not only the one with the highest FPS:

```cpp
pclk_hz = 14000000
bounce_lines = 10
num_fbs = 2
fb_in_psram = true
double_fb = false
bb_invalidate_cache = false
```

Current best setting from the latest observed tests is Run H:

```cpp
pclk_hz = 14000000
bounce_lines = 10
num_fbs = 2
fb_in_psram = true
double_fb = false
bb_invalidate_cache = false
```

Run I is the first fallback if the real application behaves better with cache
invalidation enabled:

```cpp
pclk_hz = 14000000
bounce_lines = 10
num_fbs = 2
fb_in_psram = true
double_fb = true
bb_invalidate_cache = true
```
