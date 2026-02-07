# Hall Effect Test — Agent Guide

Hardware validation utility — verifies LED strip and hall sensor before loading POV display firmware.

**This is a ~130-line test utility. Don't apply POV display architecture patterns here.**

## What It Does

- Moving dot cycles through all 33 LEDs (0 → 32)
- Hall sensor triggers color changes: Red → Green → Blue → White
- Serial output confirms each hall trigger

## Build

```bash
cd test_projects/hall_effect_test
uv run pio run                    # Build
uv run pio run -t upload          # Upload
uv run pio device monitor         # Monitor (115200 baud)
```

## Hardware

Uses same pin assignments as main LED display — see `led_display/include/hardware_config.h`.

- LEDs: 33 x SK9822/APA102 (DotStar), BGR color order
- Board: Seeed XIAO ESP32S3
- Hall Sensor: A3144, active LOW, 200ms debounce

**Pin label gotcha**: XIAO ESP32S3 silkscreen labels (D8, D9, D10) don't match GPIO numbers. D8=GPIO7, D10=GPIO9, D7=GPIO44. We use hardware SPI pins (D10 for MOSI, D8 for SCK).

## Critical Dependency

Uses **coryking fork** of NeoPixelBus with ESP-IDF 5.5 fix. Do NOT change to upstream until the fix is merged.

```ini
lib_deps = https://github.com/coryking/NeoPixelBus.git#esp-idf-5.5-fix
```
