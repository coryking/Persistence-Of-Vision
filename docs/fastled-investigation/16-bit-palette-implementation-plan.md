# 16-Bit Palette Implementation Plan (Option A: Minimal)

*Implementation Date: February 8, 2026*

## Overview

Add 16-bit palette interpolation function without creating a CRGB16 type. Implementation uses separate `u16` output parameters, following existing FastLED patterns (e.g., `gamma16()`, `five_bit_bitshift()`).

**Status:** Local implementation for POV project. Can be extracted to FastLED PR later.

---

## Design Goals

1. **Minimal:** Single function, no new types, ~50 lines of code
2. **Compatible:** Follows FastLED's existing pointer-output pattern
3. **Quality:** True 16-bit interpolation using `lerp16by16()`
4. **Local-first:** Implement in POV project, extract to FastLED later if desired

---

## API Design

### Function Signature

```cpp
namespace fl {

/// Get 16-bit color from palette with true sub-8-bit interpolation
/// @param pal The palette to extract color from
/// @param index 16-bit palette position (0-65535)
/// @param out_r Pointer to output red channel (16-bit)
/// @param out_g Pointer to output green channel (16-bit)
/// @param out_b Pointer to output blue channel (16-bit)
/// @param brightness Global brightness scaling (0-255, default 255)
/// @param blendType LINEARBLEND or NOBLEND (default LINEARBLEND)
void ColorFromPalette16(const CRGBPalette16& pal, u16 index,
                        u16* out_r, u16* out_g, u16* out_b,
                        u8 brightness = 255,
                        TBlendType blendType = LINEARBLEND);

}  // namespace fl
```

### Usage Example

```cpp
// Generate 16-bit palette color
u16 r16, g16, b16;
u16 palette_index = noise_value;  // 0-65535

fl::ColorFromPalette16(myPalette, palette_index, &r16, &g16, &b16);

// Apply to HD107S LEDs
u8 brightness_5bit;
five_bit_bitshift(r16, g16, b16, 31, &leds[i], &brightness_5bit);
```

---

## Implementation

### Algorithm

1. **Extract palette indices** (same as `ColorFromPaletteExtended`)
   - 16-bit index → two 4-bit palette indices (0-15)
   - Calculate blend fraction (0-65535)

2. **Get palette stops** (8-bit CRGB)
   - Read two adjacent palette entries

3. **Expand to 16-bit with proper mapping**
   - NOT simple `× 257` (which is `x * 0x101`)
   - Use proper 8→16 bit mapping: `(x * 65535 + 127) / 255`
   - This ensures 0→0, 255→65535, and proper intermediate values

4. **Interpolate at 16-bit precision**
   - Use FastLED's `lerp16by16()` for each channel

5. **Apply brightness scaling**
   - Use `scale16by8()` if brightness < 255

### Code Sketch

```cpp
#include "fl/colorutils.h"
#include "lib8tion.h"

namespace fl {

void ColorFromPalette16(const CRGBPalette16& pal, u16 index,
                        u16* out_r, u16* out_g, u16* out_b,
                        u8 brightness, TBlendType blendType) {

    // Extract 4-bit palette indices (0-15)
    u8 index_4bit = index >> 12;  // High 4 bits
    u8 index_next = (index_4bit + 1) & 0x0F;  // Wrap at 16

    // Calculate blend fraction (0-65535)
    // Low 12 bits of index, scaled to full 16-bit range
    u16 blend_frac = (index & 0x0FFF);  // 0-4095
    blend_frac = (blend_frac * 65535u + 2047u) / 4095u;  // Scale to 0-65535

    // Get palette entries (8-bit CRGB)
    CRGB c1 = pal[index_4bit];
    CRGB c2 = pal[index_next];

    if (blendType == NOBLEND) {
        // No interpolation - use nearest color
        // Use c1 if blend_frac < 32768, else c2
        CRGB color = (blend_frac < 32768) ? c1 : c2;

        // Expand to 16-bit (proper mapping)
        *out_r = scale8_to_16_accurate(color.r);
        *out_g = scale8_to_16_accurate(color.g);
        *out_b = scale8_to_16_accurate(color.b);
    } else {
        // LINEARBLEND - true 16-bit interpolation

        // Expand palette stops to 16-bit (proper mapping, not × 257)
        u16 r1 = scale8_to_16_accurate(c1.r);
        u16 g1 = scale8_to_16_accurate(c1.g);
        u16 b1 = scale8_to_16_accurate(c1.b);

        u16 r2 = scale8_to_16_accurate(c2.r);
        u16 g2 = scale8_to_16_accurate(c2.g);
        u16 b2 = scale8_to_16_accurate(c2.b);

        // Interpolate at 16-bit precision
        *out_r = lerp16by16(r1, r2, blend_frac);
        *out_g = lerp16by16(g1, g2, blend_frac);
        *out_b = lerp16by16(b1, b2, blend_frac);
    }

    // Apply brightness scaling if needed
    if (brightness != 255) {
        *out_r = scale16by8(*out_r, brightness);
        *out_g = scale16by8(*out_g, brightness);
        *out_b = scale16by8(*out_b, brightness);
    }
}

// Helper: Proper 8-bit to 16-bit mapping
// (This might already exist in FastLED as scale8_to_16_accurate,
//  or you can inline it)
static inline u16 scale8_to_16_accurate(u8 x) {
    if (x == 0) return 0;
    if (x == 255) return 65535;
    // Proper mapping: (x * 65535 + 127) / 255
    return (u16)(((u32)x * 65535u + 127u) / 255u);
}

}  // namespace fl
```

---

## Integration: Local Implementation

### Option 1: Header-Only (Simplest)

Create `POV_Project/led_display/src/ColorFromPalette16.h`:

```cpp
#pragma once

#include <FastLED.h>

namespace fl {

// Include implementation here (inline function)
inline void ColorFromPalette16(const CRGBPalette16& pal, u16 index,
                               u16* out_r, u16* out_g, u16* out_b,
                               u8 brightness = 255,
                               TBlendType blendType = LINEARBLEND) {
    // ... implementation ...
}

}  // namespace fl
```

**Usage in sketch:**
```cpp
#include <FastLED.h>
#include "ColorFromPalette16.h"

void loop() {
    u16 r16, g16, b16;
    fl::ColorFromPalette16(myPalette, index, &r16, &g16, &b16);
    // ...
}
```

### Option 2: Separate .cpp File

If function is too large for inlining:

**`ColorFromPalette16.h`:**
```cpp
#pragma once
#include <FastLED.h>

namespace fl {
void ColorFromPalette16(const CRGBPalette16& pal, u16 index,
                        u16* out_r, u16* out_g, u16* out_b,
                        u8 brightness = 255,
                        TBlendType blendType = LINEARBLEND);
}
```

**`ColorFromPalette16.cpp`:**
```cpp
#include "ColorFromPalette16.h"

namespace fl {
// Implementation here
}
```

---

## Testing Strategy

### Manual Testing

**Test 1: Compare against 8-bit palette**

```cpp
void test_palette_16bit_vs_8bit() {
    CRGBPalette16 testPalette = RainbowColors_p;

    // Test at various indices
    for (u16 i = 0; i < 256; i++) {
        // 8-bit result
        CRGB c8 = ColorFromPaletteExtended(testPalette, i << 8);

        // 16-bit result, quantized to 8-bit for comparison
        u16 r16, g16, b16;
        fl::ColorFromPalette16(testPalette, i << 8, &r16, &g16, &b16);
        u8 r8 = r16 >> 8;
        u8 g8 = g16 >> 8;
        u8 b8 = b16 >> 8;

        // Should match (within 1 due to rounding)
        if (abs(c8.r - r8) > 1 || abs(c8.g - g8) > 1 || abs(c8.b - b8) > 1) {
            Serial.print("Mismatch at index ");
            Serial.println(i);
        }
    }
    Serial.println("8-bit comparison complete");
}
```

**Test 2: Verify 16-bit smoothness**

```cpp
void test_16bit_smoothness() {
    CRGBPalette16 testPalette = RainbowColors_p;

    u16 prev_r, prev_g, prev_b;
    fl::ColorFromPalette16(testPalette, 0, &prev_r, &prev_g, &prev_b);

    // Check that adjacent indices produce smooth gradients
    for (u16 i = 1; i < 65535; i++) {
        u16 r16, g16, b16;
        fl::ColorFromPalette16(testPalette, i, &r16, &g16, &b16);

        // Check max delta (should be small for smooth gradient)
        u16 max_delta = max(abs((i16)r16 - (i16)prev_r),
                            max(abs((i16)g16 - (i16)prev_g),
                                abs((i16)b16 - (i16)prev_b)));

        if (max_delta > 100) {  // Arbitrary threshold
            Serial.print("Large jump at index ");
            Serial.println(i);
        }

        prev_r = r16; prev_g = g16; prev_b = b16;
    }
    Serial.println("Smoothness test complete");
}
```

**Test 3: Visual gradient test**

```cpp
void visual_gradient_test() {
    CRGBPalette16 testPalette = RainbowColors_p;

    for (int i = 0; i < NUM_LEDS; i++) {
        u16 index = map(i, 0, NUM_LEDS - 1, 0, 65535);
        u16 r16, g16, b16;

        fl::ColorFromPalette16(testPalette, index, &r16, &g16, &b16);

        // Output to HD107S
        u8 brightness_5bit;
        five_bit_bitshift(r16, g16, b16, 31, &leds[i], &brightness_5bit);
    }
    FastLED.show();
}
```

### Unit Tests (Optional, for FastLED PR)

If extracting to FastLED later, add to `tests/`:

```cpp
#include "doctest.h"
#include "fl/colorutils.h"

TEST_CASE("ColorFromPalette16 basic") {
    CRGBPalette16 pal(CRGB::Black, CRGB::White);

    u16 r, g, b;

    // Test black (index 0)
    fl::ColorFromPalette16(pal, 0, &r, &g, &b);
    CHECK(r == 0);
    CHECK(g == 0);
    CHECK(b == 0);

    // Test white (index 65535)
    fl::ColorFromPalette16(pal, 65535, &r, &g, &b);
    CHECK(r == 65535);
    CHECK(g == 65535);
    CHECK(b == 65535);

    // Test midpoint (should be ~32768)
    fl::ColorFromPalette16(pal, 32768, &r, &g, &b);
    CHECK(r > 30000);
    CHECK(r < 35000);
}
```

---

## Implementation Checklist

- [ ] Create `ColorFromPalette16.h` in POV project
- [ ] Implement function with proper 8→16 bit mapping
- [ ] Test against 8-bit ColorFromPaletteExtended (quantized comparison)
- [ ] Test smoothness with fine-grained indices
- [ ] Visual test on LED strip (gradient should be smoother)
- [ ] Document in POV project README
- [ ] (Optional) Benchmark performance vs 8-bit version

---

## Performance Notes

**Expected overhead vs 8-bit palette:**
- Additional `lerp16by16()` calls (32-bit math instead of 16-bit)
- Proper 8→16 mapping (one division per channel)
- Estimate: ~1.5-2x slower than ColorFromPaletteExtended

**For POV display:** Negligible - palette lookups are tiny fraction of render time

---

## Future FastLED PR Considerations

When extracting to FastLED:

1. **Location:** Add to `src/fl/colorutils.h` and `src/fl/colorutils.cpp.hpp`
2. **Namespace:** Use `fl::` namespace
3. **Testing:** Add unit tests to `tests/test_colorutils.cpp`
4. **Documentation:** Add Doxygen comments
5. **Example:** Create `examples/Palette16Bit/Palette16Bit.ino`
6. **Changelog:** Document in release notes

**PR would need:**
- Implementation (~50 lines)
- Unit tests (~50 lines)
- Example sketch (~100 lines)
- Documentation (~20 lines)

**Total: ~220 lines, ~4 hours work**

---

## Alternative: CRGB16 Wrapper (Future)

If ergonomics matter more than minimalism, wrap in a simple struct:

```cpp
namespace fl {

struct CRGB16 {
    u16 r, g, b;

    CRGB16() : r(0), g(0), b(0) {}
    CRGB16(u16 r, u16 g, u16 b) : r(r), g(g), b(b) {}

    CRGB toCRGB() const {
        return CRGB(r >> 8, g >> 8, b >> 8);
    }
};

CRGB16 ColorFromPalette16(const CRGBPalette16& pal, u16 index,
                          u8 brightness = 255,
                          TBlendType blendType = LINEARBLEND) {
    CRGB16 result;
    ColorFromPalette16(pal, index, &result.r, &result.g, &result.b,
                       brightness, blendType);
    return result;
}

}  // namespace fl
```

**Usage:**
```cpp
fl::CRGB16 color = fl::ColorFromPalette16(myPalette, index);
five_bit_bitshift(color.r, color.g, color.b, 31, &leds[i], &brightness_5bit);
```

More ergonomic, but adds ~50 more lines and a new type. Keep it local unless FastLED maintainer wants it.

---

## Summary

**Implementation path:**
1. Add header-only function to POV project (1 hour)
2. Test against 8-bit palette (30 min)
3. Visual validation on hardware (30 min)
4. Use in production, iterate as needed

**FastLED PR path (if desired later):**
1. Extract to FastLED repo
2. Add unit tests
3. Add example sketch
4. Submit PR with rationale

**Move fast locally, contribute later if it proves valuable.**

