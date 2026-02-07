# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code and discussing features in this repository.

## Project Overview

A high-speed Persistence of Vision (POV) display using rotating LED arrays.

The ultimate goal is a spinning LED display with three arms arranged 120 degrees apart. Each arm has either 13 or 14 LEDs, creating an interlaced 40-row radial display when spinning.

## Development Philosophy: Ship Cool Art

This is a hobby art project. The goal is to create beautiful spinning LED displays, not to build enterprise software architecture.

**Core Philosophy:**

> "We're here to make art, not write code. The code serves the art."
> — POV_DISPLAY.md

**Your role as Claude:**

- Technical steward who handles code organization while the artist focuses on effects
- Proactively refactor when patterns emerge (rule of three)
- Watch for timing issues (see docs/led_display/Timing and Dimensionality Calculations.md)
- Point out artistic opportunities in "bugs" (strobing, aliasing, moire patterns)
- Reference existing documentation when explaining decisions
- Keep codebase clean and maintainable without being asked
- Document changes clearly in commits

**Priorities (in order):**

1. **Ship working visual effects** - Get LEDs doing cool things
2. **Respect timing constraints** - Operating range: 700-2800 RPM
3. **Optimize when necessary** - If something's slow, measure it before fixing
4. **Refactor when patterns emerge** - Wait for third instance (rule of three)
5. **Stick with what works** - Don't fix what isn't broken

**Critical Timing Reality:**

- This project works because NeoPixelBus has fast, consistent SPI performance
- Performance IS correctness - jitter causes visual artifacts
- See docs/led_display/TIMING_ANALYSIS.md and docs/led_display/PROJECT_COMPARISON.md for measurements

**Render / Output Tasks**
- We have two tasks forming a double-buffered rendering pipeline.  There is the render task, which manages the angular resolution, timing and whatnot and also, importantly, generates the effects.  There is the output task on the other core which copies from the buffer, does some gamma correction and then spits out to the LEDS via DMA.
- These two tasks are in a tight pairing to form a pipeline.  Render generates what to draw, then the output task picks it up and spits it out while the render task works on the new frame.
- Angular resolution is determined by how long this process takes.  Either render or output will be the "slow bottleneck", causing the other end to wait around.  this is normal.  The angular resolution depends on which is the slowest.

## Documentation Reference

**BEFORE writing or modifying effects, read this:**

- **docs/led_display/EFFECT_SYSTEM_DESIGN.md** - Complete effect system reference: base class API, RenderContext, virtual pixels, polar helpers, geometry, EffectManager, registration, example patterns, performance rules. **This document eliminates the need to explore the codebase to understand how effects work.**

**FastLED Utility Reference** (read when building effects):

- **docs/led_display/FASTLED_REFERENCE.md** - Comprehensive catalog of FastLED's lib8tion math, noise generators, easing functions, beat generators, palettes, color operations, blending, and other building blocks for effect development. Consult this instead of exploring the FastLED source.

**Foundation Documents** (read these to understand the project):

- **docs/led_display/POV_DISPLAY.md** - Art-first philosophy, polar coordinates, design principles
- **docs/led_display/PROJECT_COMPARISON.md** - Why queue-based hall sensor works, flag-based fails
- **docs/led_display/TIMING_ANALYSIS.md** - Deep dive on jitter, why NeoPixelBus is critical
- **docs/led_display/FREERTOS_INTEGRATION.md** - FreeRTOS patterns and pitfalls for this project
- **docs/led_display/Timing and Dimensionality Calculations.md** - Mathematical reference

**Technical Reference** (read when needed):

- **docs/led_display/BUILD.md** - Build system, environments, debugging, IntelliSense
- **docs/led_display/ESP32_REFERENCE.md** - Integer math system, angle units, FPU performance
- **docs/led_display/HARDWARE.md** - Physical hardware, power system, sensors
- **docs/led_display/COORDINATE_SYSTEMS.md** - Coordinate systems, projection math, globe rendering
- **docs/PROJECT_STRUCTURE.md** - Complete project layout

**Hardware Configuration:**
- Pin assignments: See `include/hardware_config.h`
- Physical hardware: See `docs/led_display/HARDWARE.md`

**CRITICAL: When writing effect render loops, NEVER hardcode counts!**

```cpp
// Arm loop - iterate over all 3 arms:
// BAD:  for (int a = 0; a < 3; a++)
// GOOD: for (int a = 0; a < HardwareConfig::NUM_ARMS; a++)

// LED loop - iterate over LEDs in an arm:
// BAD:  for (int p = 0; p < 10; p++)
// GOOD: for (int p = 0; p < HardwareConfig::LEDS_PER_ARM; p++)
```

**Available HardwareConfig constants:**
- `NUM_ARMS` - Number of arms (3)
- `LEDS_PER_ARM` - Maximum LEDs per arm (14, for buffer sizing)
- `ARM_LED_COUNT[a]` - Actual LED count per arm (arm[0]=14, others=13)
- See `include/hardware_config.h` for complete reference

## Code Architecture

### Coding Principles

**Make it work, then make it clean:**

- Inline code first - write it directly in place
- Second time you see similar code - note it but leave it inline
- Third time (rule of three) - extract to a reusable function
- Don't create abstractions speculatively

**Performance awareness:**

- If something's slow, measure it before fixing
- See docs/led_display/Timing and Dimensionality Calculations.md for timing budgets
- See docs/led_display/ESP32_REFERENCE.md for integer math patterns

**Error handling:**

- Keep it simple: when in doubt, reset and restart
- This is embedded art, not safety-critical systems
- Trust inputs unless proven otherwise

### What NOT to Do

**Be careful changing what's working:**

- **FastLED/NeoPixelBus**: FastLED for color math, NeoPixelBus for SPI transfer only
- **Hall sensor ISR**: Uses queue-based pattern, not flags (see docs/led_display/PROJECT_COMPARISON.md)
- **RevolutionTimer**: No mutexes - atomic reads work fine
- **Task priorities**: Don't change without reason
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

**Don't treat this like production software:**

- No elaborate exception hierarchies
- No circuit breakers or retry logic
- No input validation for internal functions
- No backwards compatibility layers

**Logging: Use ESP_LOG, NOT Serial.print:**

The codebase uses ESP-IDF's `ESP_LOG*` macros for all logging. These are thread-safe (via internal mutex) and support log level filtering. **Never use `Serial.print/printf/println` for new code.**

```cpp
#include "esp_log.h"
static const char* TAG = "COMPONENT";  // Tag for filtering

ESP_LOGI(TAG, "Normal message: %d", value);    // Info (startup, state changes)
ESP_LOGW(TAG, "Warning: %s", reason);          // Warning (recoverable issues)
ESP_LOGE(TAG, "Error: %s", error);             // Error (failures)
ESP_LOGD(TAG, "Debug: %d", detail);            // Debug (verbose, filtered out in release)
```

Tag conventions: `"MAIN"`, `"HALL"`, `"ESPNOW"`, `"RENDER"`, `"OUTPUT"`, `"IMU"`, etc.

The debug env uses `-DCORE_DEBUG_LEVEL=5` which enables all log levels. In release builds, `ESP_LOGD` compiles to no-ops.

**Don't silently "fix" everything:**

- Some apparent bugs might be artistic features
- Stroboscopic effects from PWM frequency (SK9822: 4.6 kHz) could be beautiful
- Aliasing patterns from rotation speed might create interesting moire effects
- Beat patterns between update rate and rotation could be exploited artistically
- **Point out unexpected visual behaviors instead of immediately "fixing" them**

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

- Changing fundamental architecture (polling to interrupts, data flow changes)
- Removing features or visual effects
- Changes that could affect artistic output in non-obvious ways
- Adding new hardware requirements or dependencies

## Known Issues

**Arduino USB CDC Multi-Core Bug:** Serial output from Core 0 (OutputTask, ESP-NOW callbacks) is garbled due to a race condition in Arduino-ESP32's HWCDC implementation. Core 1 output works fine. This is an upstream bug with no fix yet. See **docs/led_display/ARDUINO_USB_CDC_BUG.md** for details and workarounds. Moving to pure ESP-IDF would fix this.

## Important Notes

- We are _not_ migrating to NeoPixelBus entirely! We use FastLED for everything but the final data transfer.
- In general, do not run `uv run pio run` to build - let the user build and report any errors.
