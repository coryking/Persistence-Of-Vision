# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

What We're Building: A high-speed Persistence of Vision (POV) display using rotating LED arrays.

This is an embedded systems project testing SPI performance for driving SK9822/APA102 LED strips on ESP32-S3. The test jig validates whether we can achieve the sub-20μs update timing required for a full POV display spinning at 8000 RPM.

**The POV Display Vision**
The ultimate goal is a spinning LED display with three arms arranged 120° apart. Each arm has 10 SK9822 LEDs, creating an interlaced 30-row radial display when spinning. A hall effect sensor detects rotation, subdividing each revolution into 360 angular columns (1° resolution) (as an example).

**The Timing Challenge:**

- At 8000 RPM = 133.33 rev/sec
- Revolution period = 7500 microseconds
- Time per 1° column = 20.8 microseconds
- Need to update 10 LEDs in <20μs per arm

### Why SK9822/APA102?

4-wire clocked SPI protocol (unlike WS2812B's timing-sensitive 3-wire)
Can drive up to 20-30MHz on hardware SPI
Much more forgiving timing for high-speed updates
Each arm's 10 LEDs can theoretically update in ~5μs at maximum SPI speed

### Why ESP32-S3?

- Tiny form factor (using Lolin S3 Mini or Seeed XIAO ESP32S3)
- WiFi for programming/control (device is wirelessly powered)
- Tight power budget - just enough for 30 LEDs
- Hardware SPI with DMA support

## Build System: PlatformIO + uv

This project uses **uv** (modern Python package manager) to manage PlatformIO and tooling dependencies. All PlatformIO commands should be run via `uv run`.

### Setup

```bash
# Install dependencies (first time or after updating pyproject.toml)
uv sync
```

### Common Commands

```bash
# Build the project (default: seeed_xiao_esp32s3 environment)
uv run pio run

# Build specific environment
uv run pio run -e seeed_xiao_esp32s3
uv run pio run -e lolin_s3_mini_debug

# Upload to board (requires USB connection at /dev/cu.usbmodem2101)
uv run pio run -t upload

# Upload specific environment
uv run pio run -e seeed_xiao_esp32s3 -t upload

# Open serial monitor
uv run pio device monitor

# Clean build artifacts
uv run pio run -t clean

# Update dependencies
uv sync
uv run pio pkg update
```

### Why uv?

- **10-100x faster** than pip for package installation
- **Modern dependency management** with `pyproject.toml`
- **Reproducible builds** with automatic lock file management
- **No global PlatformIO installation** - project-isolated tooling

Note: PlatformIO's ESP32 platform requires `pip` to be available in the environment, which is included in `pyproject.toml` dependencies.

## Hardware Configuration

- **Board**: Lolin S3 Mini (ESP32-S3)
- **LED Strip**: SK9822 (APA102-compatible DotStar)
  - Data Pin: GPIO 7 (via hardware SPI)
  - Clock Pin: GPIO 9 (via hardware SPI)
  - Color Order: BGR
- **USB Port**: `/dev/cu.usbmodem2101` (macOS)

## Build Environments

### seeed_xiao_esp32s3 (Production)

- Optimized build with `-O3 -ffast-math -finline-functions -funroll-loops`
- USB CDC on boot enabled

### lolin_s3_mini_debug (Debug)

- Debug level logging (`CORE_DEBUG_LEVEL=5`)
- Full debug symbols with DWARF-4
- GDB debugging enabled (`esp-builtin`)
- Optimization disabled (`-Og`)
- Breakpoint at `setup()`

## Dependencies

- **NeoPixelBus**: `makuna/NeoPixelBus@^2.7.0`
  - Using `DotStarBgrFeature` for SK9822/APA102 LED strips
  - Using `DotStarSpiMethod` for hardware SPI on ESP32-S3
  - Automatically configures hardware SPI pins for optimal performance

## Code Architecture

The project is minimal and focused:

- **src/main.cpp**: Main firmware
  - `setup()`: Initializes Serial (115200 baud) and NeoPixelBus with SK9822/APA102 strip configuration
  - `loop()`: Fills LEDs with white, measures `strip.Show()` timing, prints duration to serial, repeats every second
  - Purpose: Performance benchmarking of SPI write operations

## Key Build Flags

- `ARDUINO_USB_CDC_ON_BOOT=1`: Enables USB serial on boot

## Debugging

Use the `lolin_s3_mini_debug` environment for debugging:

```bash
# Start debug session
uv run pio debug -e lolin_s3_mini_debug
```

The debug session will break at `setup()`. Core debug level is set to maximum (5) for verbose ESP-IDF logging.

## Serial Monitor

The firmware outputs timing measurements for each `strip.Show()` call:

- Baud rate: 115200
- Format: `Sample: <microseconds>`
- Initial message: "Hello there" (after 5s delay)

# ESP32 Quick Reference

**IMPORTANT**: Use pioarduino fork, NOT official PlatformIO

- Platform line: `platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip`
- Docs: https://github.com/pioarduino/platform-espressif32
- Why: Official PlatformIO stuck at Arduino Core 2.x (Espressif stopped support Nov 2023)

## IntelliSense Setup (Modern 2024/2025)

This project uses **clangd** for C/C++ IntelliSense, NOT Microsoft's C/C++ extension.

### Why clangd?

- **ESP32-specific**: Works with Xtensa GCC flags (pioarduino uses Xtensa toolchain)
- **Fast & accurate**: Better performance than Microsoft C++ IntelliSense
- **Standards-compliant**: Uses actual compilation database from PlatformIO

### Required VS Code Extensions

1. **PlatformIO IDE** (`platformio.platformio-ide`)
2. **clangd** (`llvm.vscode-clangd`)

**DO NOT INSTALL**: `ms-vscode.cpptools` or `ms-vscode.cpptools-extension-pack` (marked as unwanted)

### Configuration Files

- **`.clangd`**: Filters out GCC-specific flags that clang doesn't understand
- **`compile_commands.json`**: Generated by PlatformIO (`pio run -t compiledb`), used by clangd
- **`.vscode/settings.json`**: Disables Microsoft C++ IntelliSense, enables clangd
- **`.vscode/c_cpp_properties.json`**: ~~DELETED~~ (not needed with clangd)

### Regenerating IntelliSense

If IntelliSense breaks or shows errors:

```bash
# Regenerate compilation database
uv run pio run -e seeed_xiao_esp32s3 -t compiledb

# Reload VS Code window
# Command Palette -> "Developer: Reload Window"
```

### How It Works

1. PlatformIO generates `compile_commands.json` with exact compiler flags
2. `.clangd` config strips out Xtensa-specific GCC flags
3. clangd reads filtered compilation database
4. IntelliSense works without spurious errors about unknown compiler flags
