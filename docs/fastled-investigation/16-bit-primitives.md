# FastLED 16-Bit Primitives Reference

*Investigation Date: January 31, 2026*

## Overview

FastLED provides 16-bit variants of noise, math, easing, and color functions. No `CRGB16` type exists - work with separate `u16` values for r/g/b channels.

## 16-Bit Color Types

### HSV16

**Location:** `src/fl/hsv16.h`

```cpp
struct HSV16 {
    u16 h;  // 0-65535
    u16 s;  // 0-65535
    u16 v;  // 0-65535

    CRGB ToRGB() const;           // Convert to 8-bit RGB
    operator CRGB() const;         // Implicit conversion
    HSV16(const CRGB& rgb);        // Construct from 8-bit RGB
};
```

Utility: `scale8_to_16_accurate(u8 x)` → `(x * 65535 + 127) / 255`

## 16-Bit Noise Functions

**Location:** `src/noise.h`, `src/noise.cpp.hpp`

### Perlin Noise (returns 0-65535)

```cpp
uint16_t inoise16(uint32_t x);
uint16_t inoise16(uint32_t x, uint32_t y);
uint16_t inoise16(uint32_t x, uint32_t y, uint32_t z);
uint16_t inoise16(uint32_t x, uint32_t y, uint32_t z, uint32_t t);
```

### Raw Perlin Noise (returns signed -18k to +18k)

```cpp
int16_t inoise16_raw(uint32_t x);
int16_t inoise16_raw(uint32_t x, uint32_t y);
int16_t inoise16_raw(uint32_t x, uint32_t y, uint32_t z);
int16_t inoise16_raw(uint32_t x, uint32_t y, uint32_t z, uint32_t w);
```

### Simplex Noise (returns 0-65535)

```cpp
uint16_t snoise16(uint32_t x);
uint16_t snoise16(uint32_t x, uint32_t y);
uint16_t snoise16(uint32_t x, uint32_t y, uint32_t z);
uint16_t snoise16(uint32_t x, uint32_t y, uint32_t z, uint32_t w);
```

**Coordinate format:** 16.16 fixed-point (high 16 bits = integer, low 16 bits = fraction)

## 16-Bit Math Functions

### Scaling

**Location:** `src/platforms/shared/scale8.h`

```cpp
uint16_t scale16by8(uint16_t i, fract8 scale);   // (i * scale) / 256
uint16_t scale16(uint16_t i, fract16 scale);     // (i * scale) / 65536
```

### Linear Interpolation

**Location:** `src/lib8tion.h`

```cpp
uint16_t lerp16by16(uint16_t a, uint16_t b, fract16 frac);  // Blend with 16-bit fraction
uint16_t lerp16by8(uint16_t a, uint16_t b, fract8 frac);    // Blend with 8-bit fraction
int16_t lerp15by16(int16_t a, int16_t b, fract16 frac);     // Signed 15-bit blend
int16_t lerp15by8(int16_t a, int16_t b, fract8 frac);       // Signed 15-bit blend
```

## 16-Bit Easing Functions

**Location:** `src/fl/ease.h`

All take `u16` input (0-65535), return `u16` output (0-65535):

```cpp
u16 easeInQuad16(u16 i);
u16 easeOutQuad16(u16 i);
u16 easeInOutQuad16(u16 i);
u16 easeInCubic16(u16 i);
u16 easeOutCubic16(u16 i);
u16 easeInOutCubic16(u16 i);
u16 easeInSine16(u16 i);
u16 easeOutSine16(u16 i);
u16 easeInOutSine16(u16 i);
```

## 16-Bit Gamma

**Location:** `src/fl/ease.h`, `src/fl/ease.cpp.hpp`

```cpp
u16 gamma_2_8(u8 value);  // LUT-based, 256 entries, γ=2.8
```

Converts 8-bit linear → 16-bit gamma-corrected. Used internally by HD gamma functions.

**Location:** `src/fl/gamma.h`

```cpp
void gamma16(const CRGB& rgb, u16* r16, u16* g16, u16* b16);
```

Applies `gamma_2_8()` to all three channels.

## Fixed-Point Types

**Location:** `src/lib8tion/qfx.h`

```cpp
q44   // 4.4 fixed-point (uint8_t)
q62   // 6.2 fixed-point (uint8_t)
q88   // 8.8 fixed-point (uint16_t) - most useful for 16-bit work
q124  // 12.4 fixed-point (uint16_t)
```

## Palette Functions with 16-bit Index

**Location:** `src/fl/colorutils.h`

```cpp
CRGB ColorFromPaletteExtended(const CRGBPalette32& pal, u16 index, u8 brightness, TBlendType);
CRGB ColorFromPaletteExtended(const CRGBPalette256& pal, u16 index, u8 brightness, TBlendType);
```

16-bit index provides smoother interpolation between palette entries.

## 16-Bit to 8+5 Quantization

**Location:** `src/fl/five_bit_hd_gamma.h`

```cpp
void five_bit_bitshift(u16 r16, u16 g16, u16 b16, u8 brightness,
                       CRGB* out, u8* out_power_5bit);
```

Accepts 16-bit RGB directly. Quantizes to 8-bit RGB + 5-bit brightness for APA102/HD107 output.

The wrapper `five_bit_hd_gamma_bitshift()` adds gamma expansion before calling this.

## Key Files

| File | Contents |
|------|----------|
| `src/fl/hsv16.h` | HSV16 type |
| `src/noise.h` | 16-bit noise declarations |
| `src/lib8tion.h` | lerp16by16, lerp16by8, etc. |
| `src/platforms/shared/scale8.h` | scale16by8, scale16 |
| `src/fl/ease.h` | 16-bit easing functions |
| `src/fl/gamma.h` | gamma16() |
| `src/fl/five_bit_hd_gamma.h` | five_bit_bitshift() |
