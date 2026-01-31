---
name: enable-perf-harness
description: Quick reference for performance profiling the LED display firmware
---

# Performance Profiling Cheatsheet

See `docs/led_display/BUILD.md` for full documentation.

## Quick Start

```bash
cd led_display

# Build with profiling environment
uv run pio run -e seeed_xiao_esp32s3_profiling

# Upload and capture output
uv run pio run -e seeed_xiao_esp32s3_profiling -t upload
uv run pio device monitor > timing.log
```

## What's Enabled

- `TEST_MODE` - Fake hall sensor (no spinning needed)
- `ENABLE_TIMING_INSTRUMENTATION` - CSV output with `total_us` per frame

## Enabling Detailed Timing

To enable granular per-effect timing, uncomment in `platformio.ini`:

```ini
[env:seeed_xiao_esp32s3_profiling]
build_flags =
    ...
    -DENABLE_DETAILED_TIMING          # Uncomment this line
```

## Adding Timing to a New Effect

Use periodic averages (every 1000 frames) to minimize log spam. See `Radar.cpp` or `NoiseField.cpp` for examples.

```cpp
// At file scope
#ifdef ENABLE_DETAILED_TIMING
static int64_t timingAccum = 0;
static int renderCount = 0;
#endif

void IRAM_ATTR MyEffect::render(RenderContext& ctx) {
#ifdef ENABLE_DETAILED_TIMING
    int64_t start = esp_timer_get_time();
#endif

    // ... render code ...

#ifdef ENABLE_DETAILED_TIMING
    timingAccum += esp_timer_get_time() - start;
    if (++renderCount % 1000 == 0) {
        Serial.printf("MYEFFECT_TIMING: avg_us=%lld\n", timingAccum / renderCount);
        timingAccum = 0;
        renderCount = 0;
    }
#endif
}
```

## Analyzing Output

```bash
# Filter specific timing
grep "TIMING" timing.log

# Quick average with awk
grep "MYEFFECT_TIMING" timing.log | awk -F'=' '{sum+=$2; n++} END{print sum/n}'
```

## Timing Budget

At 1300 RPM with 120 slots/rev:
- **Budget per slot**: ~380us
- **Target render time**: <150us (leaves margin for Show())
