# HD Gamma Algorithm Deep Dive

*Investigation Date: January 31, 2026*

## Overview

FastLED's `five_bit_hd_gamma_bitshift()` decomposes 8-bit RGB into optimized 8-bit RGB + 5-bit brightness for APA102/HD107 LEDs.

**Conceptually similar to RGBW:** Like how RGBW strips extract common brightness to the white channel, this extracts a brightness factor to the 5-bit field. The difference: RGBW maximizes efficiency, this maximizes dark-end precision.

## The Algorithm

**Location:** `src/fl/five_bit_hd_gamma.h`

### Step 1: Gamma Correction (8-bit to 16-bit)

```cpp
gamma16(color, &r16, &g16, &b16);
```

Applies gamma 2.8 curve via lookup table. Converts 8-bit input to 16-bit intermediate for precision.

### Step 2: Color Correction (optional)

```cpp
if (colors_scale.r != 0xff) {
    r16 = scale16by8(r16, colors_scale.r);
}
```

Applies per-channel color temperature correction if specified.

### Step 3: Apply Global Brightness

```cpp
if (brightness != 0xff) {
    r16 = scale16by8(r16, brightness);
    // same for g16, b16
}
```

Scales by FastLED's global brightness (0-255).

### Step 4: Find Maximum and Quantize to 5-bit

```cpp
uint16_t scale = max3(r16, g16, b16);
scale = (scale + (2047 - (scale >> 5))) >> 11;  // Closed-form 16-bit to 5-bit
```

This formula (added in v3.10.2, credit to @gwgill) converts the 16-bit max value to 5-bit (0-31). It replaced an iterative approach that had quantization artifacts.

### Step 5: Scale RGB Back to 8-bit

```cpp
static uint32_t bright_scale[32] = { /* precomputed factors */ };
scalef = bright_scale[scale];
r8 = (r16 * scalef + 0x808000) >> 24;  // Multiply, round, extract 8-bit
```

The lookup table contains factors computed as: `ix/31 * 255/65536 * 256`

### Output

- `out_colors`: 8-bit RGB (scaled to complement the brightness)
- `out_power_5bit`: 5-bit brightness (0-31)

## Why "8x Better Dark End"

**Standard mode:** brightness=31 (max), RGB=1
- Only 1 distinct level above black

**HD mode:** brightness=1, RGB=31
- 31 distinct levels at similar perceived brightness
- The 5-bit field provides coarse control, RGB provides fine control

## Function Signature

```cpp
void five_bit_hd_gamma_bitshift(
    CRGB colors,              // Input: 8-bit RGB
    CRGB colors_scale,        // Color correction (255,255,255 for none)
    fl::u8 global_brightness, // 0-255
    CRGB *out_colors,         // Output: scaled 8-bit RGB
    fl::u8 *out_power_5bit    // Output: 0-31
);
```

## Standalone Usage

This function can be called directly, independent of FastLED's LED output:

```cpp
#include "fl/five_bit_hd_gamma.h"

CRGB input(r, g, b);
CRGB output;
uint8_t brightness_5bit;

fl::five_bit_hd_gamma_bitshift(
    input,
    CRGB(255, 255, 255),  // no color correction
    255,                   // full brightness
    &output,
    &brightness_5bit
);
// output now contains scaled RGB
// brightness_5bit contains 0-31
```

## Gamma Curve Behavior

The gamma 2.8 lookup table is designed for human perception. At the dark end:
- Input RGB values 1-3 may map to 16-bit values that round to zero after the full algorithm
- This is perceptually correct (humans can't distinguish near-black values) but may not be what you expect

**Test project should validate:** Does this improve the RGB(1,0,0) to RGB(0,0,0) gulf, or does gamma correction make it worse by crushing those values?

## Key Files

```
src/fl/five_bit_hd_gamma.h   # Algorithm implementation
src/fl/gamma.h               # gamma16() function
src/fl/ease.cpp.hpp          # gamma_2_8 lookup table (256 entries)
```
