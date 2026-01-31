# FastLED Brightness & Data Structures for HD107S

*Investigation Date: January 31, 2026*
*FastLED Version: Current master (8cd217f137)*

## Key Finding

FastLED provides two modes for APA102/HD107 LEDs:

### Standard Mode: 5-bit Brightness Hardcoded to Max

```cpp
FastLED.addLeds<HD107, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
```
- 5-bit brightness field fixed at 31
- Only 8-bit RGB for dimming
- Wastes the 32-level hardware brightness

### HD Mode: 5-bit Brightness Used for Dynamic Range

```cpp
FastLED.addLeds<HD107ControllerHD, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
```
- Intelligent gamma correction algorithm
- Decomposes color into optimal 8-bit RGB + 5-bit brightness
- Better dark-end gradation

## How HD Mode Improves Dark End

At low brightness:
- **Standard:** brightness=31, RGB=1 = only 1 step above black
- **HD mode:** brightness=1, RGB=31 = 31 steps at similar perceived brightness

The 5-bit field provides coarse control (32 levels), RGB provides fine control within each level.

## Data Structures

**CRGB:** 3 bytes (R, G, B) - no brightness field
- HD mode manages brightness in the encoder, not per-pixel storage
- No CRGB16 exists in FastLED

**Per-LED brightness:** Not exposed through CRGB interface
- HD mode applies brightness globally via `FastLED.setBrightness()`
- For true per-LED control, use NeoPixelBus with `RgbwColor` (see neopixelbus-brightness.md)

## Standalone Gamma Function

The HD gamma algorithm is callable independently:

```cpp
#include "fl/five_bit_hd_gamma.h"

CRGB input(r, g, b);
CRGB output;
uint8_t brightness_5bit;

fl::five_bit_hd_gamma_bitshift(
    input,
    CRGB(255, 255, 255),  // color correction (none)
    255,                   // global brightness
    &output,
    &brightness_5bit       // 0-31
);
```

This enables hybrid approaches: FastLED effects + NeoPixelBus output with per-LED brightness.

See hd-gamma-math.md for algorithm details.

## Related Chipsets

| Chipset | Standard Controller | HD Controller |
|---------|---------------------|---------------|
| APA102 | `APA102` (brightness=31) | `APA102ControllerHD` |
| SK9822 | `SK9822` (brightness=31) | `SK9822ControllerHD` |
| HD107 | `HD107` (brightness=31) | `HD107ControllerHD` |
| HD108 | `HD108` (16-bit color) | N/A |

## Key Files

```
src/fl/five_bit_hd_gamma.h     # Gamma decomposition algorithm
src/fl/chipsets/apa102.h       # HD107ControllerHD definition
src/pixel_controller.h         # Brightness scaling logic
```
