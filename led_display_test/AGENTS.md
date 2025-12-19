# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**This is a simple hardware validation utility, NOT the main POV display firmware.**

Purpose: Validate LED strip connectivity and hall effect sensor before loading the complex POV display firmware.

Expected behavior:
- Moving dot cycles through all 33 LEDs (0 → 32)
- Hall sensor triggers color changes: Red → Green → Blue → White
- Serial output confirms each hall trigger

Use this to verify hardware, then return to parent directory (`../`) for the actual POV display firmware.

## Build System: PlatformIO + uv

All commands use `uv run` to invoke PlatformIO:

```bash
# Build firmware
uv run pio run

# Upload to board
uv run pio run -t upload

# Serial monitor (115200 baud)
uv run pio device monitor

# All-in-one: build, upload, monitor
uv run pio run -t upload && uv run pio device monitor
```

**Note:** `uv` and PlatformIO dependencies are installed in parent directory. If `uv` is not found, run `uv sync` in parent directory first.

## Hardware Configuration

### Pin Assignments (Different from Main POV Project!)

| Wire Color | GPIO | Function | Notes |
|------------|------|----------|-------|
| Blue       | D8 (GPIO 8)  | SPI Data (MOSI) | LED data signal |
| Purple     | D9 (GPIO 9)  | SPI Clock (SCLK) | LED clock signal |
| Brown      | D10 (GPIO 10) | Hall Sensor | Active LOW (pullup enabled) |

**WARNING:** Main POV project uses D7/D9/D10 (GPIO 6/8/9). This test uses GPIO 8/9/10. Don't confuse them.

### Hardware Details
- **LEDs**: 33 × SK9822/APA102 (DotStar), BGR color order
- **Board**: Seeed XIAO ESP32S3
- **Hall Sensor**: Digital hall effect (e.g., A3144), active LOW, 200ms debounce

## Code Architecture

**Extremely simple by design** - this is a test utility, not production firmware.

### Single File Structure

`src/main.cpp` contains everything (~130 lines):
- `setup()`: Initialize Serial, NeoPixelBus, and hall sensor ISR
- `loop()`: Blocking cycle through LEDs with `delay(CYCLE_DELAY)`
- `hallISR()`: Interrupt handler for hall sensor (sets `hallTriggered` flag)

### Key Constants (Adjustable)

```cpp
#define NUM_LEDS 33        // Change if testing different LED count
#define CYCLE_DELAY 100    // ms between LED updates (speed control)
const uint32_t DEBOUNCE_MS = 200; // Hall sensor debounce time
```

### Color Palette

Edit `colors[]` array in `main.cpp` to change color sequence:

```cpp
const RgbColor colors[] = {
    RgbColor(255, 0, 0),   // Red
    RgbColor(0, 255, 0),   // Green
    RgbColor(0, 0, 255),   // Blue
    RgbColor(255, 255, 255) // White
};
```

## Dependencies

### NeoPixelBus (Critical)

Uses **coryking fork** with ESP-IDF 5.5 compatibility fix:

```ini
lib_deps =
    https://github.com/coryking/NeoPixelBus.git#esp-idf-5.5-fix
```

**Do NOT change to upstream NeoPixelBus** until ESP-IDF 5.5 fix is merged. Upstream breaks with pioarduino platform.

### PlatformIO Platform

Uses **pioarduino fork** (ESP32 Arduino Core 3.3.4, ESP-IDF 5.5.1):

```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
```

Official PlatformIO ESP32 support ended at Arduino Core 2.x (Nov 2023). Community fork maintains Core 3.x compatibility.

## Differences from Main POV Project

This test is intentionally simplified - it's NOT representative of the main firmware:

| Feature | LED Test (This Project) | Main POV Display (Parent Dir) |
|---------|-------------------------|-------------------------------|
| Pin Config | GPIO 8/9/10 | GPIO 6/8/9 (D7/D9/D10) |
| Rendering | Blocking `delay()` loop | High-speed interrupt-driven |
| Timing | ~100ms cycle time | Sub-millisecond precision |
| Hall Sensor | Color trigger only | Revolution timing & sync |
| Dependencies | NeoPixelBus only | NeoPixelBus + FastLED |
| Lines of Code | ~130 | ~1000+ |

**Do not apply POV display architecture patterns to this test code.** This is meant to be simple.

## Typical Development Workflow

1. **Modify code** in `src/main.cpp` (change colors, timing, LED count, etc.)
2. **Build** with `uv run pio run` (compile only, verify no errors)
3. **Upload** with `uv run pio run -t upload` (requires USB cable, board connected)
4. **Monitor** with `uv run pio device monitor` (115200 baud, watch serial output)
5. **Test hardware**: Pass magnet over hall sensor, observe LED pattern and color changes

## Troubleshooting

### No LEDs Lighting
- LED strip needs external 5V power (common ground with ESP32)
- Verify pin connections: Blue → D8, Purple → D9
- Check serial: Should see "NeoPixelBus initialized" message

### Hall Sensor Not Triggering
- Hall sensors detect specific magnetic pole - try flipping magnet
- Distance: Magnet must be within ~5-10mm
- Check serial: Should see "Hall triggered!" when magnet passes

### Serial Output Garbled
- Verify 115200 baud rate in monitor
- Wait 2 seconds after upload for serial to start
- Try pressing reset button on board

## Next Steps After Hardware Validation

Once this test confirms hardware works:

```bash
# Return to parent directory
cd ..

# Build main POV display firmware
uv run pio run

# Upload main firmware
uv run pio run -t upload
```

See parent directory documentation for POV display usage and architecture.
