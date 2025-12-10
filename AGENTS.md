# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Development Philosophy: Ship Cool Art

This is a hobby art project. The goal is to create beautiful spinning LED displays, not to build enterprise software architecture.

**Core Philosophy:**

> "We're here to make art, not write code. The code serves the art."
> — POV_DISPLAY.md

**Your role as Claude:**

- Technical steward who handles code organization while the artist focuses on effects
- Proactively refactor when patterns emerge (rule of three)
- Watch for timing issues (see docs/Timing and Dimensionality Calculations.md)
- Point out artistic opportunities in "bugs" (strobing, aliasing, moiré patterns)
- Reference existing documentation when explaining decisions
- Keep codebase clean and maintainable without being asked
- Document changes clearly in commits

**Priorities (in order):**

1. **Ship working visual effects** - Get LEDs doing cool things
2. **Respect timing constraints** - Operating range: 700-2800 RPM (see docs/Timing and Dimensionality Calculations.md)
3. **Optimize when necessary** - If something's slow, measure it before fixing
4. **Refactor when patterns emerge** - Wait for third instance (rule of three)
5. **Stick with what works** - Current architecture works; don't fix what isn't broken

**Critical Timing Reality:**

- This project works because NeoPixelBus has fast, consistent SPI performance
- Other POV projects using FastLED failed with same architecture due to slow SPI
- Performance IS correctness - jitter causes visual artifacts
- See docs/TIMING_ANALYSIS.md and docs/PROJECT_COMPARISON.md for measurements

**Foundation Documents** (read these to understand the project):

- **docs/POV_DISPLAY.md** - Art-first philosophy, polar coordinates, design principles
- **docs/PROJECT_COMPARISON.md** - Why queue-based hall sensor works, flag-based fails
- **docs/TIMING_ANALYSIS.md** - Deep dive on jitter, why NeoPixelBus is critical
- **docs/FREERTOS_INTEGRATION.md** - FreeRTOS patterns and pitfalls for this project
- **docs/Timing and Dimensionality Calculations.md** - Mathematical reference

## Project Overview

What We're Building: A high-speed Persistence of Vision (POV) display using rotating LED arrays.

This is an embedded systems project testing SPI performance for driving SK9822/APA102 LED strips on ESP32-S3. The test jig validates whether we can achieve the sub-20μs update timing required for a full POV display spinning at 8000 RPM.

**The POV Display Vision**
The ultimate goal is a spinning LED display with three arms arranged 120° apart. Each arm has 10 SK9822 LEDs, creating an interlaced 30-row radial display when spinning. A hall effect sensor detects rotation, subdividing each revolution into 360 angular columns (1° resolution) (as an example).

**The Timing Challenge:**

- Operating range: 700-2800 RPM
- See docs/Timing and Dimensionality Calculations.md for exact timing budgets at different RPMs

### Why Exact Timing Matters

POV displays work by persistence of vision - LEDs flash at precise angular positions as the disc rotates. **For this project, performance IS correctness.**

**Jitter (timing inconsistency) causes visual artifacts:**

- Image wobble (pixels appear at inconsistent angles)
- Radial misalignment (multi-arm synchronization breaks)
- Blurring (when LEDs update at wrong positions)
- Beat patterns (when update timing drifts relative to rotation)

**A slow but consistent update is better than a fast but jittery one.**

**What causes jitter:**

- **Interrupt latency**: Wi-Fi, USB, other peripherals stealing cycles
- **Blocking operations**: Serial.print(), delay(), filesystem access in rendering path
- **Memory allocations**: malloc/free in time-critical paths
- **Task priority inversion**: Low-priority task holding resource needed by high-priority task
- **Inconsistent ISR priority**: Competing interrupts
- **Cache misses, branch mispredictions**: In tight loops

**Current architecture (what's working):**

- **Queue-based hall sensor** (not flag-based - see docs/PROJECT_COMPARISON.md for why)
- **High-priority FreeRTOS task** for hall processing
- **ISR timestamp capture** with IRAM_ATTR and esp_timer_get_time()
- **NeoPixelBus library** (fast SPI - see docs/TIMING_ANALYSIS.md)
- **No mutexes on RevolutionTimer** (atomic reads work fine, stale data doesn't matter)
- **Pure integer math** in render path (no floats - see Integer Math System below)

**Design implications:**

- If timing looks inconsistent, check for jitter sources (blocking calls, malloc in loops, etc.)
- Keep rendering path non-blocking (tight loop when active, delay() only during warmup)
- Pre-allocate buffers if you can, but don't obsess
- See timing budgets in docs if you need to understand constraints

**Reference:** See docs/TIMING_ANALYSIS.md for deep dive on jitter sources and mitigation.

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
  - Data Pin: D5 / GPIO 5 (orange wire, via hardware SPI)
  - Clock Pin: D6 / GPIO 6 (green wire, via hardware SPI)
  - Color Order: BGR
- **Hall Effect Sensor**: D1 / GPIO 1 (yellow wire)
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

### Coding Principles

**Make it work, then make it clean:**

- Inline code first - write it directly in place
- Second time you see similar code - note it but leave it inline
- Third time (rule of three) - extract to a reusable function
- Don't create abstractions speculatively

**Performance awareness:**

- If something's slow, measure it before fixing
- See docs/Timing and Dimensionality Calculations.md for timing budgets

**Error handling:**

- Keep it simple: when in doubt, reset and restart
- This is embedded art, not safety-critical systems
- No need for elaborate error hierarchies or recovery strategies
- Trust inputs unless proven otherwise (no defensive validation everywhere)

**Memory management:**

- Pre-allocate buffers in time-critical paths
- Focus on correctness over memory efficiency
- Performance might matter, but measure first

### What NOT to Do

**Be careful changing what's working:**

- **FastLED**: Current code uses NeoPixelBus for fast SPI (see docs/TIMING_ANALYSIS.md for comparison)
- **Hall sensor ISR**: Uses queue-based pattern, not flags (see docs/PROJECT_COMPARISON.md for why)
- **RevolutionTimer**: No mutexes - atomic reads work fine
- **Task priorities**: Current setup works; don't change without reason
- **Rendering path**: No delay() in active rendering - tight loop is intentional

**Don't over-architect:**

- No interfaces or base classes until there are 3+ implementations
- No plugin systems or extensibility frameworks
- No configuration files for things that can be constants
- No "what if we need to..." features that weren't requested
- No elaborate test plans (this is art, not production software)

**Don't ask permission for obvious cleanups:**

- Rule of three refactoring? Just do it, document in commit
- Renaming for clarity? Go ahead
- Extracting duplicated code? Don't ask, explain why after
- Reorganizing files? Make the change, document rationale

**Don't treat this like production software:**

- No elaborate exception hierarchies
- No circuit breakers or retry logic
- No input validation for internal functions (trust inputs unless proven otherwise)
- No backwards compatibility layers
- Simple error handling: when in doubt, reset and restart

**Don't silently "fix" everything:**

- Some apparent bugs might be artistic features
- Stroboscopic effects from PWM frequency (SK9822: 4.6 kHz) could be beautiful
- Aliasing patterns from rotation speed might create interesting moiré effects
- Beat patterns between update rate and rotation could be exploited artistically
- **Point out unexpected visual behaviors instead of immediately "fixing" them**

**Avoid common jitter sources:**

- malloc/free in rendering loop (pre-allocate if needed)
- Serial.print() during active rendering
- Filesystem access in time-critical paths
- Blocking operations in rendering loop

### When to Ask vs Just Do It

**Just do it (don't ask):**

- Refactoring that preserves behavior (rule of three, renaming, reorganizing)
- Fixing obvious bugs (crashes, incorrect calculations, memory corruption)
- Improving code comments or documentation
- Optimizing something that's measurably slow

**Point it out (don't silently fix):**

- Unexpected visual artifacts that might be artistically interesting
- Performance characteristics that create interesting patterns
- "Problems" that could be features (stroboscopic effects, aliasing, etc.)
- Trade-offs between approaches that affect visual output

**Ask first:**

- Changing fundamental architecture (polling → interrupts, data flow changes)
- Removing features or visual effects
- Changes that could affect artistic output in non-obvious ways
- Adding new hardware requirements or dependencies

### Current Architecture

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

## Integer Math System (IMPORTANT)

**The render path uses NO floating-point math.** All angles and timing use integer types for speed and precision.

### Angle Units

Angles use `angle_t` (uint16_t) where **3600 units = 360 degrees** (0.1° precision):

```cpp
angle_t angleUnits = arm.angleUnits;        // 0-3599
uint8_t pattern = angleUnits / 180;         // Exact integer division for 18° patterns
```

Key constants in `types.h`:
- `ANGLE_FULL_CIRCLE = 3600`
- `ANGLE_PER_PATTERN = 180` (18 degrees)
- `INNER_ARM_PHASE = 1200` (120 degrees)
- `OUTER_ARM_PHASE = 2400` (240 degrees)

### Speed: Use microsPerRev, NOT RPM

**Do NOT convert to RPM** - it requires float division. Use `microsPerRev` directly:

```cpp
// BAD - requires float division:
float rpm = 60000000.0f / microsPerRev;

// GOOD - use raw measurement:
uint8_t speed = speedFactor8(ctx.microsPerRev);  // Returns 0-255 (faster = higher)
```

Speed ranges:
- 700 RPM (slow) = ~85,714 µs/rev
- 2800 RPM (fast) = ~21,428 µs/rev

### FastLED Integer Helpers

Use FastLED's optimized functions instead of float math:

| Instead of | Use | Notes |
|------------|-----|-------|
| `float * float` | `scale8(val, scale)` | 8-bit multiply |
| `std::min(255, a+b)` | `qadd8(a, b)` | Saturating add |
| `lerp(a, b, t)` | `lerp8by8(a, b, frac)` | frac is 0-255 |
| `sin(angle)` | `sin16(phase)` | phase 0-65535 |
| `fmod(angle, 360)` | `angle % 3600` | Integer modulo |

### Key Files

- `include/types.h` - `angle_t` and constants
- `include/polar_helpers.h` - Integer angle helpers (`isAngleInArcUnits`, `arcIntensityUnits`, `speedFactor8`)
- `include/RenderContext.h` - `arms[].angleUnits` (not `angle`)

## ESP32-S3 Floating-Point Performance Summary

The ESP32-S3 includes a single-precision hardware floating point unit (FPU) that provides decent performance for `float` operations, with single-precision multiplications taking approximately 4 CPU clock cycles. However, even with hardware acceleration, floating-point calculations are still consistently 2× slower than integer operations on the S3. **This project uses pure integer math in the render path to maximize performance.**

Double-precision (`double`) operations are software-emulated and extremely slow - **never use double in timing-critical code**.

### Further Reading

- **Espressif Official Documentation**: [Speed Optimization - ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/speed.html) - Official performance guidelines
- **FPU Overview**: [Floating-Point Units on Espressif SoCs](https://developer.espressif.com/blog/2025/10/cores_with_fpu/) - Explains which Espressif chips have FPUs and why they matter
- **Benchmarking Study**: [Integer vs Float Performance on the ESP32-S3: Why TinyML Loves Quantization](https://medium.com/@sfarrukhm/integer-vs-float-performance-on-the-esp32-s3-why-tinyml-loves-quantization-227eca11bd35) - Real-world benchmarks showing 2× performance difference
- **ESP32-S2 Comparison**: [No, the ESP32-S2 is not faster at floating point operations](https://blog.llandsmeer.com/tech/2021/04/08/esp32-s2-fpu.html) - Detailed analysis of FPU performance and optimization techniques
- **Forum Discussion**: [FPU Documentation for S3](https://esp32.com/viewtopic.php?t=40615) - Community discussion about FPU cycle counts

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

- we are _not_ migrating to neopixelbus! we use fastled for everything but the final data transfer.
- in general, please do not do uv run pio run to build.... let me build instead, i'll report any errors... don't you worry about that!