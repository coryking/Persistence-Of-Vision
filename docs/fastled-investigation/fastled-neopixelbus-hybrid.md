# FastLED + NeoPixelBus Hybrid Approach

*Investigation Date: January 31, 2026*

## Overview

Use FastLED for effects and gamma correction, NeoPixelBus for SPI output with per-LED 5-bit brightness.

## The Approach

1. Compute effects using FastLED (CRGB arrays, noise, palettes, etc.)
2. Apply HD gamma decomposition via `five_bit_hd_gamma_bitshift()`
3. Output via NeoPixelBus `DotStarLbgrFeature` with `RgbwColor`

## API Compatibility

**FastLED output:** `five_bit_hd_gamma_bitshift()` produces:
- 8-bit RGB (scaled)
- 5-bit brightness (0-31)

**NeoPixelBus input:** `DotStarLbgrFeature` expects:
- `RgbwColor(r, g, b, brightness)` where brightness is 0-31 (clamped if higher)

These match directly. No conversion needed.

## Wire Protocol Verification

`DotStarL4ByteFeature::applyPixelColor()` encodes:
```cpp
*p++ = 0xE0 | (color.W < 31 ? color.W : 31);  // Frame: 111bbbbb
*p++ = color.B;
*p++ = color.G;
*p++ = color.R;
```

This correctly produces the DotStar/APA102 frame format with per-LED brightness.

## Performance Characteristics

**Per-pixel `five_bit_hd_gamma_bitshift()` overhead:**
- ~50-80 CPU cycles per pixel
- All integer math (no floating point)
- Uses lookup tables (stack/flash)
- No heap allocations

**For 144 LEDs at 240MHz ESP32:**
- ~36 microseconds per frame for gamma decomposition
- SPI transfer dominates (~1-2ms at 40MHz)
- CPU overhead is negligible

## Implementation Sketch

```cpp
// Declarations
NeoPixelBus<DotStarLbgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);
CRGB leds[NUM_LEDS];  // FastLED effect buffer

// Per-frame processing
for (int i = 0; i < NUM_LEDS; i++) {
    CRGB output;
    uint8_t brightness;

    fl::five_bit_hd_gamma_bitshift(
        leds[i],              // Input from effect
        CRGB(255,255,255),    // No color correction
        global_brightness,     // 0-255
        &output,
        &brightness           // 0-31
    );

    strip.SetPixelColor(i, RgbwColor(output.r, output.g, output.b, brightness));
}

strip.Show();
```

## Why This Approach

**FastLED strengths:**
- Rich effect library (noise, palettes, blur, etc.)
- HD gamma decomposition algorithm
- Active development

**NeoPixelBus strengths:**
- Superior ESP32 SPI implementation (single DMA transaction)
- Clean per-LED brightness via RgbwColor
- Reliable on ESP32-S3

**Combined:** Best of both - FastLED effects with NeoPixelBus output efficiency.

## Test Project Validation Goals

1. Verify the per-pixel loop doesn't impact frame timing
2. Confirm 5-bit brightness values appear correctly on wire (logic analyzer)
3. Visual test: dark gradients with HD gamma vs without
4. POV test: check for artifacts when spinning
