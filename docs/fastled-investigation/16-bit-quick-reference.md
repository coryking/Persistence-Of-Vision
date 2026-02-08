# FastLED 16-Bit Quick Reference

*For POV Display Applications - Updated February 8, 2026*

## TL;DR

`ColorFromPaletteExtended` has NO hidden 16-bit precision. Simple promotion (× 257) gives **zero quality improvement** - same 256 discrete levels, just numerically scaled up.

**FastLED provides excellent 16-bit noise and math, but NO 16-bit color types or palette functions.**

---

## Available 16-Bit Features ✅

### Noise Functions
```cpp
u16 inoise16(u32 x, u32 y, u32 z);     // Perlin noise 0-65535
u16 snoise16(u32 x, u32 y, u32 z);     // Simplex noise 0-65535

// Shape helpers for POV displays
HSV16 noiseRingHSV16(float angle, u32 time, float radius);
HSV16 noiseCylinderHSV16(float angle, float height, u32 time, float radius);
```

### Math Primitives
```cpp
u16 scale16(u16 i, fract16 scale);          // (i * scale) / 65536
u16 lerp16by16(u16 a, u16 b, fract16 frac); // 16-bit linear interpolation
u16 easeInOutCubic16(u16 i);                // 16-bit easing
```

### Color Types
```cpp
struct HSV16 { u16 h, s, v; };  // 16-bit HSV
CRGB ToRGB() const;              // Convert to 8-bit RGB
```

### Gamma & HD Output
```cpp
u16 gamma_2_8(u8 value);  // 8-bit → 16-bit gamma (LUT, γ=2.8)

// Accept 16-bit input, output 8+5 HD format
void five_bit_bitshift(u16 r16, u16 g16, u16 b16, u8 brightness,
                       CRGB* out, u8* out_power_5bit);
```

---

## Missing 16-Bit Features ❌

### No CRGB16 Type
```cpp
// Does NOT exist in FastLED
struct CRGB16 { u16 r, g, b; };  // Must be implemented manually
```

### No 16-Bit Palette Functions
```cpp
// ColorFromPaletteExtended is 8-bit only
CRGB ColorFromPaletteExtended(const CRGBPalette16& pal, u16 index);
//    ^^^^                                              ^^^ 16-bit index, but...
//    8-bit output with 8-bit interpolation math

// Internal implementation uses scale8():
u8 result = ((u16)color * (u16)blend_factor) >> 8;
//          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
//          16-bit intermediate, immediately truncated to 8-bit
```

**Implementation detail:** Exactly 256 discrete color values between any two palette stops.

---

## Palette Precision: The Verdict

### Simple Promotion (CRGB × 257)
```cpp
// Expand 8-bit to 16-bit
u16 r16 = leds[i].r * 257;
u16 g16 = leds[i].g * 257;
u16 b16 = leds[i].b * 257;
```

**Quality:** ❌ **Zero improvement**
- Same 256 discrete levels, just numerically scaled up
- Adjacent palette indices that produce the same 8-bit output still produce the same value
- Spreads 256 levels across a 65536 range without adding intermediate values

### Custom 16-Bit Palette Interpolation
```cpp
CRGB16 ColorFromPalette16(const CRGBPalette16& pal, u16 index) {
    u8 idx1 = index >> 12;  // 0-15
    u8 idx2 = (idx1 + 1) & 0x0F;
    u16 blend = (index & 0x0FFF) * 16;  // Scale to 0-65535

    // Get palette stops (8-bit)
    CRGB c1 = pal[idx1];
    CRGB c2 = pal[idx2];

    // Expand to 16-bit (proper mapping)
    u16 r1 = c1.r * 257, r2 = c2.r * 257;
    u16 g1 = c1.g * 257, g2 = c2.g * 257;
    u16 b1 = c1.b * 257, b2 = c2.b * 257;

    // Interpolate at 16-bit precision
    CRGB16 result;
    result.r = lerp16by16(r1, r2, blend);
    result.g = lerp16by16(g1, g2, blend);
    result.b = lerp16by16(b1, b2, blend);
    return result;
}
```

**Quality:** ✅ **Real improvement**
- True sub-8-bit color precision
- Smooth gradients (65536 levels per palette segment)
- But this is **new** precision, not **recovered** precision

---

## Recommendations for POV Display

### 1. Use 16-Bit Noise ✅ (Easy Win)

**Why:** `inoise16()` provides genuine sub-8-bit precision for smoother animations.

```cpp
// Example: 16-bit noise for POV ring
for (int i = 0; i < NUM_LEDS; i++) {
    float angle = (i * 2.0f * PI) / NUM_LEDS;
    HSV16 hsv = noiseRingHSV16(angle, millis() * 16, 2.0f);
    leds[i] = hsv.ToRGB();  // Convert to 8-bit for output
}
```

**Cost:** Zero - just use the function
**Benefit:** Smoother noise animations

### 2. Custom 16-Bit Palettes (If Needed)

**Use case:** Applications requiring smooth gradients with no visible banding

**Implementation:**
1. Define `CRGB16 { u16 r, g, b; }`
2. Write `ColorFromPalette16()` using `lerp16by16()`
3. Keep palette stops as 8-bit CRGB (they're control points)

**Cost:** Custom code (~50 lines)
**Benefit:** 65536 discrete colors per palette segment vs 256

### 3. Use HD Gamma Pipeline for Output ✅

**Rationale:** HD107S LEDs support 8+5 format. FastLED's `five_bit_bitshift()` accepts 16-bit input directly.

```cpp
// Option A: From 8-bit CRGB (with gamma expansion)
u8 brightness_5bit;
CRGB color = five_bit_hd_gamma_bitshift(leds[i], &brightness_5bit);

// Option B: From 16-bit channels (direct)
u16 r16, g16, b16;  // 16-bit color values
u8 brightness_5bit;
CRGB color_8bit;
five_bit_bitshift(r16, g16, b16, 31, &color_8bit, &brightness_5bit);
```

**Cost:** Zero - FastLED provides this
**Benefit:** Better dark-end dynamic range

---

## Code Patterns

### Pattern 1: 16-Bit Noise → 8-Bit Output

```cpp
// Generate 16-bit noise
u32 time = millis() << 4;  // 16.16 fixed-point
u16 noise = inoise16(x_coord, y_coord, time);

// Apply 16-bit effects
noise = scale16(noise, brightness_16bit);
noise = easeInOutCubic16(noise);

// Quantize to 8-bit output
leds[i].r = noise >> 8;
```

### Pattern 2: 16-Bit Noise → 8+5 HD Output

```cpp
// Generate 16-bit RGB noise
u32 time = millis() << 4;
u16 r16 = inoise16(x_coord, time);
u16 g16 = inoise16(x_coord + 0x10000, time);
u16 b16 = inoise16(x_coord + 0x20000, time);

// Apply 16-bit effects
r16 = scale16(r16, brightness_16bit);
g16 = scale16(g16, brightness_16bit);
b16 = scale16(b16, brightness_16bit);

// Quantize to 8+5 output
u8 brightness_5bit;
CRGB color_8bit;
five_bit_bitshift(r16, g16, b16, 31, &color_8bit, &brightness_5bit);
```

### Pattern 3: HSV16 Workflow (Simplest)

```cpp
// Shape-based noise generates HSV16 directly
float angle = (i * 2.0f * PI) / NUM_LEDS;
HSV16 hsv = noiseRingHSV16(angle, millis() * 16, 1.5f);

// Apply HSV-space effects (optional)
hsv.s = scale16(hsv.s, saturation_boost);
hsv.v = easeOutCubic16(hsv.v);

// Convert to 8-bit RGB (FastLED handles this)
leds[i] = hsv.ToRGB();
```

---

## Files to Read

| Priority | File | Why |
|----------|------|-----|
| 🔥 | **16-bit-pipeline-status.md** | Complete analysis, all details |
| ⭐ | `src/fl/noise.h` | Shape-based noise helpers (ring, cylinder) |
| ⭐ | `src/fl/hsv16.h` | HSV16 type definition |
| 💡 | `src/lib8tion.h` | 16-bit math primitives |
| 💡 | `src/fl/five_bit_hd_gamma.h` | HD gamma pipeline |

---

## Summary

**Noise effects:** FastLED's 16-bit support is excellent and ready to use.

**Palette effects:**
- 256 discrete levels acceptable → Use `ColorFromPaletteExtended` as-is (8-bit interpolation)
- Smooth gradients required → Implement custom `ColorFromPalette16()` with `lerp16by16()`
- Simple promotion (× 257) → Provides no quality benefit

**HD107S output:**
- `five_bit_bitshift()` - Accepts 16-bit input, outputs 8+5 format
- `five_bit_hd_gamma_bitshift()` - Convenience wrapper for 8-bit input with gamma expansion

