# FastLED 16-Bit Pipeline Status

*Investigation Date: February 8, 2026*

## Executive Summary

`ColorFromPaletteExtended` does NOT have hidden 16-bit precision. The interpolation math uses 16-bit intermediates but immediately truncates back to 8-bit before returning. Each palette segment produces exactly 256 discrete color values.

**16-bit support in FastLED:**
- ✅ Excellent 16-bit noise functions (`inoise16`, `snoise16`, shape helpers)
- ✅ Complete 16-bit math primitives (`scale16`, `lerp16by16`, easing functions)
- ✅ `HSV16` type with 16-bit HSV → 8-bit RGB conversion
- ✅ 16-bit gamma correction and HD output quantization
- ❌ No CRGB16 type (16-bit RGB color)
- ❌ No 16-bit palette interpolation functions
- **Conclusion:** 16-bit clean rendering requires custom color types and palette interpolation

---

## 1. Palette Interpolation Precision Analysis

### Current Implementation Analysis

**File:** `src/fl/colorutils.cpp.hpp:254-299`

```cpp
CRGB ColorFromPaletteExtended(const CRGBPalette32 &pal, u16 index,
                              u8 brightness, TBlendType blendType) {
    // Extract palette index and offset
    u8 index_5bit = (index >> 11);
    u8 offset = (u8)(index >> 3);

    // Get palette entries (8-bit CRGB)
    const CRGB *entry = &(pal[0]) + index_5bit;
    u8 red1 = entry->red;
    u8 green1 = entry->green;
    u8 blue1 = entry->blue;

    if (blend) {
        // Calculate scaling factors
        u8 f1 = 255 - offset;  // 8-bit

        // Scale using 8-bit precision
        red1 = scale8_LEAVING_R1_DIRTY(red1, f1);
        green1 = scale8_LEAVING_R1_DIRTY(green1, f1);
        blue1 = scale8_LEAVING_R1_DIRTY(blue1, f1);

        // Second palette entry
        u8 red2 = entry->red;
        red2 = scale8_LEAVING_R1_DIRTY(red2, offset);
        // ... same for green2, blue2

        cleanup_R1();

        // Add results (8-bit)
        red1 += red2;
        green1 += green2;
        blue1 += blue2;
    }

    return CRGB(red1, green1, blue1);  // 8-bit output
}
```

**What `scale8_LEAVING_R1_DIRTY` does:**
```cpp
// From src/platforms/shared/scale8.h
u8 scale8(u8 i, fract8 scale) {
    return ((u16)i * (u16)scale) >> 8;  // 16-bit intermediate, 8-bit result
}
```

### Key Findings

1. **Input:** 16-bit index (0-65535) for smooth addressing
2. **Palette stops:** 8-bit CRGB values (fixed, cannot change)
3. **Interpolation math:**
   - Uses `scale8()` which multiplies two 8-bit values → 16-bit intermediate
   - **Immediately truncates back to 8-bit via `>> 8`**
   - No rounding correction
4. **Output:** 8-bit CRGB

**Conclusion:** The 16-bit intermediate in `scale8()` is a mathematical artifact of multiplying two uint8_t values, NOT deliberate higher-precision storage. There is NO hidden precision being thrown away.

### What About blend8_16bit?

FastLED has a better blending function that DOES use 32-bit intermediates with proper rounding:

```cpp
// From src/platforms/shared/math8.h:143
u8 blend8_16bit(u8 a, u8 b, u8 amountOfB) {
    u32 partial;
    i16 delta = (i16)b - (i16)a;

    // (a * 65536 + (b - a) * amountOfB * 257 + 32768) / 65536
    partial = ((u32)a << 16);
    partial += (u32)delta * amountOfB * 257;  // Maps 0-255 to 0-65535
    partial += 0x8000;  // Rounding

    return partial >> 16;
}
```

**Status:** This function exists but is **only used by `nblend()`** for blending two CRGBs together. The palette functions don't use it.

### Implications for POV Displays

**Simple promotion (CRGB × 257 → 16-bit):**
- ✅ Provides exactly as much precision as palette functions produce (256 levels)
- ✅ Zero code complexity
- ✅ Fast
- ❌ No quality improvement - same stair-stepping, just in a bigger container

**Custom 16-bit palette interpolation:**
- ✅ Genuine quality improvement (true sub-8-bit color precision)
- ✅ Could use `blend8_16bit()` or full 16-bit lerp with `u16` channels
- ❌ New precision, not recovered precision
- ❌ Requires writing custom palette functions
- ❌ No CRGB16 type - requires separate u16 r,g,b channels or custom implementation

---

## 2. What FastLED Offers for 16-Bit Rendering

### 16-Bit Color Types

#### HSV16 ✅

**File:** `src/fl/hsv16.h`

```cpp
struct HSV16 {
    u16 h;  // 0-65535 (full circle)
    u16 s;  // 0-65535 (0% to 100% saturation)
    u16 v;  // 0-65535 (0% to 100% value/brightness)

    // Constructors
    HSV16(u16 h, u16 s, u16 v);
    HSV16(const CRGB& rgb);  // 8-bit RGB → 16-bit HSV

    // Conversion to 8-bit RGB
    CRGB ToRGB() const;
    operator CRGB() const { return ToRGB(); }
};
```

**Utility function:**
```cpp
u16 scale8_to_16_accurate(u8 x) {
    return (u16)(((u32)x * 65535 + 127) / 255);
}
```

This maps 8-bit values to 16-bit with proper rounding (not just `x * 257`).

#### CRGB16 ❌

**Does not exist.** Implementation options:
- Use separate `u16 r, g, b` variables
- Or create a custom struct:
  ```cpp
  struct CRGB16 {
      u16 r, g, b;
  };
  ```

### 16-Bit Noise Functions ✅

**File:** `src/noise.h`, `src/fl/noise.h`

#### Perlin Noise (returns 0-65535)

```cpp
u16 inoise16(u32 x);
u16 inoise16(u32 x, u32 y);
u16 inoise16(u32 x, u32 y, u32 z);
u16 inoise16(u32 x, u32 y, u32 z, u32 t);
```

#### Simplex Noise (returns 0-65535)

```cpp
u16 snoise16(u32 x);
u16 snoise16(u32 x, u32 y);
u16 snoise16(u32 x, u32 y, u32 z);
u16 snoise16(u32 x, u32 y, u32 z, u32 w);
```

**Coordinate format:** 16.16 fixed-point
- High 16 bits = integer part
- Low 16 bits = fractional part
- Example: `x = 3 << 16` = position 3.0, `x = (3 << 16) | 32768` = position 3.5

#### Shape-Based Noise Helpers ✅

**File:** `src/fl/noise.h`

For geometric patterns, FastLED provides convenience functions that sample multiple z-slices:

```cpp
// Ring patterns (angular)
HSV16 noiseRingHSV16(float angle, u32 time, float radius = 1.0f);
CRGB noiseRingCRGB(float angle, u32 time, float radius = 1.0f);

// Sphere patterns (azimuth + polar)
HSV16 noiseSphereHSV16(float angle, float phi, u32 time, float radius = 1.0f);
CRGB noiseSphereCRGB(float angle, float phi, u32 time, float radius = 1.0f);

// Cylinder patterns (angular + height)
HSV16 noiseCylinderHSV16(float angle, float height, u32 time, float radius = 1.0f);
CRGB noiseCylinderCRGB(float angle, float height, u32 time, float radius = 1.0f);
```

**Note:** These functions sample three z-slices (at `time`, `time + 0x10000`, `time + 0x20000`) to generate independent H/S/V or R/G/B components.

**Extent constants for mapping:**
```cpp
// From src/fl/noise.h:22
constexpr u16 NOISE16_EXTENT_MIN = 9000;
constexpr u16 NOISE16_EXTENT_MAX = 59500;
```

These bounds capture ~98%+ of inoise16's practical range, useful for rescaling to full 0-65535.

### 16-Bit Math Primitives ✅

**File:** `src/lib8tion.h`, `src/platforms/shared/scale8.h`

#### Scaling

```cpp
u16 scale16by8(u16 i, fract8 scale);   // (i * scale) / 256
u16 scale16(u16 i, fract16 scale);     // (i * scale) / 65536
```

#### Linear Interpolation

```cpp
u16 lerp16by16(u16 a, u16 b, fract16 frac);  // 16-bit blend factor
u16 lerp16by8(u16 a, u16 b, fract8 frac);    // 8-bit blend factor
i16 lerp15by16(i16 a, i16 b, fract16 frac);  // Signed 15-bit
i16 lerp15by8(i16 a, i16 b, fract8 frac);    // Signed 15-bit
```

#### Averaging

```cpp
u16 avg16(u16 i, u16 j);       // (i + j) / 2
u16 avg16r(u16 i, u16 j);      // (i + j + 1) / 2 (rounded up)
```

### 16-Bit Easing Functions ✅

**File:** `src/fl/ease.h`

All take `u16` input (0-65535), return `u16` output (0-65535):

```cpp
// Quadratic
u16 easeInQuad16(u16 i);
u16 easeOutQuad16(u16 i);
u16 easeInOutQuad16(u16 i);

// Cubic
u16 easeInCubic16(u16 i);
u16 easeOutCubic16(u16 i);
u16 easeInOutCubic16(u16 i);

// Sine
u16 easeInSine16(u16 i);
u16 easeOutSine16(u16 i);
u16 easeInOutSine16(u16 i);
```

**Note:** These are accurate implementations. The older `ease*8()` functions in `lib8tion.h` are intentionally simplified/approximated for speed, but these 16-bit versions prioritize correctness.

### 16-Bit Gamma Correction ✅

**File:** `src/fl/ease.h`, `src/fl/gamma.h`

```cpp
// LUT-based 8→16 bit gamma (γ=2.8)
u16 gamma_2_8(u8 value);  // 256-entry lookup table

// Apply to all three channels
void gamma16(const CRGB& rgb, u16* r16, u16* g16, u16* b16);
```

**Usage:**
```cpp
CRGB color(128, 64, 32);
u16 r16, g16, b16;
gamma16(color, &r16, &g16, &b16);
// r16, g16, b16 now contain gamma-corrected 16-bit values
```

### HD Gamma → 8+5 Quantization ✅

**File:** `src/fl/five_bit_hd_gamma.h`

For APA102/HD107S LEDs (8-bit RGB + 5-bit brightness):

```cpp
void five_bit_bitshift(u16 r16, u16 g16, u16 b16, u8 brightness,
                       CRGB* out, u8* out_power_5bit);
```

Accepts 16-bit RGB directly and quantizes to 8-bit RGB + 5-bit brightness.

**Convenience wrapper with gamma expansion:**
```cpp
CRGB five_bit_hd_gamma_bitshift(const CRGB& color, u8* out_power_5bit);
```

This function:
1. Applies `gamma_2_8()` to expand 8-bit → 16-bit per channel
2. Calls `five_bit_bitshift()` to quantize to 8+5 format

---

## 3. Building a 16-Bit Clean Rendering Pipeline

### Option A: Hybrid 16/8-Bit (Recommended)

**Architecture:**
1. **Noise generation:** Use `inoise16()` → u16 values
2. **Color mapping:** Map noise to HSV16 or separate u16 r,g,b channels
3. **Effects:** Use 16-bit math (`scale16`, `lerp16by16`, easing)
4. **Output quantization:** Convert to 8-bit CRGB or 8+5 HD format

**Pros:**
- Real sub-8-bit precision in rendering
- Leverages FastLED's 16-bit noise and math
- Output matches hardware capabilities (8-bit RGB or 8+5 HD)

**Cons:**
- No CRGB16 type - work with separate channels or HSV16
- Palette functions still 8-bit - need custom implementation

**Code sketch:**
```cpp
// Generate 16-bit noise
u32 time = millis() << 4;  // Fixed-point time
u16 noise_r = inoise16(x_coord, time);
u16 noise_g = inoise16(x_coord + 0x10000, time);
u16 noise_b = inoise16(x_coord + 0x20000, time);

// Apply 16-bit effects
noise_r = scale16(noise_r, brightness_16bit);
noise_g = scale16(noise_g, brightness_16bit);
noise_b = scale16(noise_b, brightness_16bit);

// Quantize to 8-bit output
u8 r = noise_r >> 8;
u8 g = noise_g >> 8;
u8 b = noise_b >> 8;
leds[i] = CRGB(r, g, b);
```

### Option B: Full 16-Bit with Custom Types

**Architecture:**
1. Define `CRGB16` struct with `u16 r, g, b`
2. Write custom palette interpolation using `lerp16by16()`
3. Keep entire pipeline in 16-bit until final output

**Pros:**
- True 16-bit throughout
- Custom palette interpolation can use `blend8_16bit()` approach

**Cons:**
- Significant custom code
- No FastLED palette infrastructure

**Custom palette interpolation sketch:**
```cpp
struct CRGB16 {
    u16 r, g, b;
};

CRGB16 ColorFromPalette16(const CRGBPalette16& pal, u16 index) {
    // Extract palette indices
    u8 index_4bit = index >> 12;  // 0-15
    u16 blend_frac = index & 0x0FFF;  // 0-4095, scale to 0-65535
    blend_frac = (blend_frac * 65535u) / 4095u;

    // Get palette entries (8-bit)
    CRGB c1 = pal[index_4bit];
    CRGB c2 = pal[(index_4bit + 1) & 0x0F];

    // Expand to 16-bit (× 257 for proper mapping)
    u16 r1 = c1.r * 257;
    u16 g1 = c1.g * 257;
    u16 b1 = c1.b * 257;
    u16 r2 = c2.r * 257;
    u16 g2 = c2.g * 257;
    u16 b2 = c2.b * 257;

    // Interpolate at 16-bit precision
    CRGB16 result;
    result.r = lerp16by16(r1, r2, blend_frac);
    result.g = lerp16by16(g1, g2, blend_frac);
    result.b = lerp16by16(b1, b2, blend_frac);

    return result;
}
```

### Option C: Simple Promotion (Least Effort)

**Architecture:**
1. Use FastLED's 8-bit palettes as-is
2. Multiply output by 257 to expand 8-bit → 16-bit

**Pros:**
- Zero custom code
- Fast

**Cons:**
- No real precision gain
- Still 256 discrete levels, just spread across 65536 range

---

## 4. Recommendations for POV Display

### For Noise Effects

✅ **Use FastLED's 16-bit noise directly**
- `inoise16()` or `snoise16()` gives true sub-8-bit precision
- Use shape helpers (`noiseRingHSV16`, `noiseCylinderHSV16`) for POV patterns
- Apply 16-bit math (`scale16`, `lerp16by16`, easing functions)

Example:
```cpp
// POV ring pattern with 16-bit noise
for (int i = 0; i < NUM_LEDS; i++) {
    float angle = (i * 2.0f * PI) / NUM_LEDS;
    HSV16 hsv = noiseRingHSV16(angle, millis() * 16, 2.0f);
    leds[i] = hsv.ToRGB();  // Convert to 8-bit for output
}
```

### For Palette Effects

**Decision point:** Is true 16-bit palette interpolation required?

**If YES (smoothest gradients):**
- Implement custom `ColorFromPalette16()` as shown in Option B
- Use `lerp16by16()` for interpolation
- Keep palette stops as 8-bit CRGB (they're the control points, not the interpolated values)

**If NO (good enough):**
- Use FastLED's `ColorFromPaletteExtended()` with 16-bit index
- Provides 256 distinct colors between stops, often imperceptible
- Simple promotion (× 257) available if u16 output format is required

### For HD107S Output

✅ **Use FastLED's HD gamma pipeline**

Two paths:

**Path 1: Direct 8+5 from 8-bit input**
```cpp
leds[i] = CRGB(r8, g8, b8);
u8 brightness_5bit;
five_bit_hd_gamma_bitshift(leds[i], &brightness_5bit);
// leds[i] now has gamma-corrected 8-bit RGB
// brightness_5bit has 5-bit global brightness
```

**Path 2: Direct 8+5 from 16-bit input**
```cpp
u16 r16, g16, b16;  // 16-bit color values
u8 brightness_5bit;
CRGB out;
five_bit_bitshift(r16, g16, b16, 31, &out, &brightness_5bit);
// out has 8-bit RGB, brightness_5bit has 5-bit brightness
```

---

## 5. Key Files Reference

| File | Contents |
|------|----------|
| `src/fl/hsv16.h` | HSV16 type definition |
| `src/fl/hsv16.cpp.hpp` | HSV16 ↔ RGB conversion |
| `src/noise.h` | inoise16, snoise16 declarations |
| `src/fl/noise.h` | Shape-based noise helpers (ring, sphere, cylinder) |
| `src/lib8tion.h` | lerp16by16, lerp16by8 |
| `src/platforms/shared/scale8.h` | scale16by8, scale16 |
| `src/platforms/shared/math8.h` | blend8_16bit (unused by palettes!) |
| `src/fl/ease.h` | 16-bit easing functions |
| `src/fl/gamma.h` | gamma16() |
| `src/fl/five_bit_hd_gamma.h` | five_bit_bitshift(), HD gamma pipeline |
| `src/fl/colorutils.h` | ColorFromPaletteExtended (8-bit interpolation) |
| `src/fl/colorutils.cpp.hpp` | Palette implementation |

---

## 6. Summary

### What FastLED Offers ✅

- **Excellent 16-bit noise** (Perlin, Simplex, shape helpers)
- **Complete 16-bit math library** (scale, lerp, easing)
- **HSV16 type** with 16-bit HSV → 8-bit RGB conversion
- **16-bit gamma correction** (LUT-based, γ=2.8)
- **HD gamma pipeline** for 8+5 output (APA102/HD107S)

### What FastLED Does NOT Offer ❌

- **No CRGB16 type** (RGB color with 16-bit channels)
- **No 16-bit palette interpolation** (ColorFromPaletteExtended is 8-bit only)
- **No 16-bit RGB output path** (everything ends at 8-bit CRGB)

### Palette Strategy: Simple Promotion vs Custom 16-Bit

**Question:** Should palettes use simple promotion (× 257) or custom 16-bit interpolation?

**Answer:**

- **Simple promotion provides ZERO quality improvement** - same 256 discrete levels, just numerically scaled up. Adjacent palette indices that produce the same 8-bit output still produce the same value after promotion.

- **Custom 16-bit palette interpolation provides REAL quality improvement** - true sub-8-bit color precision. However, this is new precision, not recovered precision, because ColorFromPaletteExtended never had sub-8-bit precision to begin with.

**Recommendation:** When building a 16-bit clean rendering pipeline (particularly for noise effects), custom palette functions using `lerp16by16()` provide true sub-8-bit color precision and smooth gradients without visible banding.

