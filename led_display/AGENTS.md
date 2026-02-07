# LED Display — Agent Guide

LED display-specific guidance. For project-wide philosophy and structure, see the root AGENTS.md.

## Hardware Overview

Three arms arranged 120 degrees apart, each with 13 or 14 LEDs, creating an interlaced 40-row radial display when spinning. Operating range: 700-2800 RPM.

- The display can't be connected via USB while spinning — no serial debugging at runtime
- Performance IS correctness — jitter causes visual artifacts
- FastLED for color math, NeoPixelBus for SPI transfer only (don't change this)

## Render / Output Pipeline

Two FreeRTOS tasks form a double-buffered rendering pipeline:
- **Render task** (Core 1): manages angular resolution, timing, generates effects
- **Output task** (Core 0): copies from buffer, applies gamma correction, sends to LEDs via DMA

Angular resolution depends on whichever task is the bottleneck. Either task waiting on the other is normal.

See **docs/led_display/RENDER_OUTPUT_ARCHITECTURE.md** for the full design.

## Key Documentation

Full documentation index: **docs/README.md**

**Read before writing effects:**
- **docs/led_display/EFFECT_SYSTEM_DESIGN.md** — Complete effect system reference (authoritative)
- **docs/led_display/FASTLED_REFERENCE.md** — FastLED utility catalog (consult instead of exploring FastLED source)

**Read when needed:**
- **docs/led_display/COORDINATE_SYSTEMS.md** — Polar math, projections, globe rendering
- **docs/led_display/TIMING_ANALYSIS.md** — Jitter analysis, why NeoPixelBus matters
- **docs/led_display/HARDWARE.md** — Physical hardware, pin assignments (`include/hardware_config.h`)

## Coding Rules

**Never hardcode hardware counts:**

```cpp
// BAD:  for (int a = 0; a < 3; a++)
// GOOD: for (int a = 0; a < HardwareConfig::NUM_ARMS; a++)

// BAD:  for (int p = 0; p < 10; p++)
// GOOD: for (int p = 0; p < HardwareConfig::LEDS_PER_ARM; p++)
```

Available constants: `NUM_ARMS`, `LEDS_PER_ARM`, `ARM_LED_COUNT[a]` — see `include/hardware_config.h`.

**Use ESP_LOG, not Serial.print:**

```cpp
#include "esp_log.h"
static const char* TAG = "COMPONENT";
ESP_LOGI(TAG, "Normal message: %d", value);
ESP_LOGW(TAG, "Warning: %s", reason);
ESP_LOGE(TAG, "Error: %s", error);
ESP_LOGD(TAG, "Debug: %d", detail);
```

Tags: `"MAIN"`, `"HALL"`, `"ESPNOW"`, `"RENDER"`, `"OUTPUT"`, `"IMU"`. Debug env uses `-DCORE_DEBUG_LEVEL=5` for all log levels; release builds compile `ESP_LOGD` to no-ops.

## What NOT to Change

- **FastLED/NeoPixelBus split**: FastLED for color math, NeoPixelBus for SPI transfer only
- **Hall sensor ISR**: Queue-based pattern, not flags (see POV_PROJECT_ARCHITECTURE_LESSONS.md)
- **RevolutionTimer**: No mutexes — atomic reads work fine
- **No delay() in rendering path**: Tight loop is intentional

## Don't Over-Architect

- No interfaces or base classes until there are 3+ implementations
- No configuration files for things that can be constants
- No "what if we need to..." features that weren't requested

## Don't Silently "Fix" Everything

- Some apparent bugs might be artistic features
- Stroboscopic effects, aliasing patterns, beat patterns between update rate and rotation — these could be beautiful
- **Point out unexpected visual behaviors instead of immediately "fixing" them**

## When to Ask vs Just Do It

**Just do it:** Refactoring that preserves behavior (rule of three), obvious bug fixes, documentation, measurably slow code.

**Point it out (don't silently fix):** Visual artifacts that might be artistically interesting, stroboscopic effects, aliasing, moire.

**Ask first:** Architecture changes, removing features, anything affecting artistic output.

## Known Issues

**Arduino USB CDC Multi-Core Bug:** Core 0 serial output is garbled (Arduino-ESP32 HWCDC race condition). Core 1 works fine. See **docs/led_display/ARDUINO_USB_CDC_BUG.md**.

## Important Notes

- We are _not_ migrating to NeoPixelBus entirely — FastLED for everything but the final data transfer.
- Do not run `uv run pio run` to build — let the user build and report any errors.
