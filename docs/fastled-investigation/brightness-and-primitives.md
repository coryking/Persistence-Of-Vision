# FastLED Brightness & Data Structures for HD107S

*Investigation Date: January 31, 2026*
*FastLED Version: Current master (8cd217f137)*

## The Key Discovery

FastLED provides **two distinct modes** for APA102/HD107 LEDs:

### Standard Mode: 5-bit Brightness WASTED
```cpp
FastLED.addLeds<HD107, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
```
- The 5-bit brightness field is **hardcoded to max (31)**
- You're only using the 8-bit PWM for dimming
- Wastes the 32-level hardware brightness control

### HD Mode: 5-bit Brightness USED FOR DYNAMIC RANGE
```cpp
FastLED.addLeds<HD107ControllerHD, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
```
- Uses intelligent gamma correction algorithm
- Decomposes 8-bit colors + 8-bit global brightness into optimal 8-bit colors + 5-bit brightness
- Provides **~8x finer control at the dark end**

## Your Solution

**Replace `HD107` with `HD107ControllerHD`:**

```cpp
// Before (poor dark-end range):
FastLED.addLeds<HD107, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);

// After (better dark-end range):
FastLED.addLeds<HD107ControllerHD, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
```

## How It Works

### The 5-bit Gamma Algorithm

The HD mode uses a closed-form mathematical decomposition:

1. Takes your 8-bit RGB values + 8-bit global brightness (from `FastLED.setBrightness()`)
2. Calculates optimal split between 8-bit PWM and 5-bit brightness
3. Maximizes precision at low brightness levels

At low brightness:
- Standard mode: brightness=31, RGB=1 → only 1 step above black
- HD mode: brightness=1, RGB=31 → 31 steps above black at similar perceived brightness

### Data Structures

**CRGB struct is 3 bytes only (R, G, B)**
- No 4th field for brightness
- Brightness is managed by the pixel controller, not stored per-LED

**No CRGB16 exists** in FastLED for 16-bit per-channel color.

**HD108 (16-bit LEDs)** uses different encoding but still CRGB input with internal upscaling.

### Per-LED Brightness?

The encoder infrastructure **supports** per-LED brightness internally, but it's **not exposed** through the simple CRGB interface. The HD mode applies brightness globally via `FastLED.setBrightness()`, then the encoder optimally distributes it across all LEDs.

If you need true per-LED brightness control, NeoPixelBus offers this via `RgbwColor` (see neopixelbus-brightness.md).

## Related Chipsets

| Chipset | Controller | Brightness Handling |
|---------|------------|---------------------|
| APA102 | `APA102` | Hardcoded to 31 |
| APA102 | `APA102ControllerHD` | Gamma-optimized |
| SK9822 | `SK9822` | Hardcoded to 31 |
| SK9822 | `SK9822ControllerHD` | Gamma-optimized |
| HD107 | `HD107` | Hardcoded to 31 |
| HD107 | `HD107ControllerHD` | Gamma-optimized |
| HD108 | `HD108` | 16-bit color, separate handling |

## Key Files

```
src/chipsets.h              # HD107, APA102, HD108 definitions (lines 1040-1183 for HD108)
src/pixel_controller.h      # Brightness scaling logic
src/pixel_iterator.h        # getHdScale / loadRGBScaleAndBrightness
src/five_bit_hd_gamma.h     # The 5-bit gamma decomposition algorithm
```

## Example Usage

```cpp
#include "FastLED.h"

#define NUM_LEDS 100
#define DATA_PIN 7
#define CLOCK_PIN 8

CRGB leds[NUM_LEDS];

void setup() {
    // Use HD mode for better dynamic range
    FastLED.addLeds<HD107ControllerHD, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);

    // Global brightness still works - HD mode optimizes how it's applied
    FastLED.setBrightness(128);
}

void loop() {
    // Dark values will have much better gradation in HD mode
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB(i, 0, 0);  // Gradient from 0-99
    }
    FastLED.show();
}
```
