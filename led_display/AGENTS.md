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
- Other POV projects using FastLED failed with same architecture due to slow SPI
- Performance IS correctness - jitter causes visual artifacts
- See docs/led_display/TIMING_ANALYSIS.md and docs/led_display/PROJECT_COMPARISON.md for measurements

## Documentation Reference

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
- **docs/PROJECT_STRUCTURE.md** - Complete project layout

## Project Overview

A high-speed Persistence of Vision (POV) display using rotating LED arrays.

The ultimate goal is a spinning LED display with three arms arranged 120 degrees apart. Each arm has 11 SK9822 LEDs, creating an interlaced 33-row radial display when spinning.

**Hardware Configuration:**
- Pin assignments: See `include/hardware_config.h`
- Physical hardware: See `docs/led_display/HARDWARE.md`

**CRITICAL: When writing effect render loops, NEVER hardcode LED counts!**

```cpp
// BAD:  for (int p = 0; p < 10; p++)
// GOOD: for (int p = 0; p < HardwareConfig::LEDS_PER_ARM; p++)
```

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

## Important Notes

- We are _not_ migrating to NeoPixelBus entirely! We use FastLED for everything but the final data transfer.
- In general, do not run `uv run pio run` to build - let the user build and report any errors.
