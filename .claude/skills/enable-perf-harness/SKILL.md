---
name: enable-perf-harness
description: Quick reference for performance profiling the LED display firmware
---

# Performance Profiling Cheatsheet

See `docs/led_display/BUILD.md` for full documentation.

## Quick Start

```bash
cd led_display
uv run pio run -e seeed_xiao_esp32s3_profiling -t upload
uv run pio device monitor > timing.csv
```

## What's Enabled

- `TEST_MODE` - Fake hall sensor (no spinning needed)
- `ENABLE_TIMING_INSTRUMENTATION` - CSV output with `total_us` per frame

## Adding Effect-Specific Timing

Use `ENABLE_DETAILED_TIMING` for granular timing in effects. See `NoiseField.cpp` for example pattern.
