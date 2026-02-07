# FastLED Library Reference for Effect Development

A comprehensive catalog of FastLED's utility functions, math helpers, color tools, noise generators, and other building blocks beyond basic LED control. Organized for quick lookup when building POV display effects.

**Library location:** `.pio/libdeps/seeed_xiao_esp32s3/FastLED/`

> **Note:** We use FastLED for color math and utilities only. Final LED data transfer uses NeoPixelBus via SPI. See AGENTS.md.

---

## Table of Contents

1. [lib8tion Math Library](#lib8tion-math-library)
2. [Scaling & Dimming](#scaling--dimming)
3. [Trigonometry](#trigonometry)
4. [Easing Functions](#easing-functions)
5. [Waveform Generators](#waveform-generators)
6. [Beat Generators (BPM)](#beat-generators-bpm)
7. [Linear Interpolation & Mapping](#linear-interpolation--mapping)
8. [Random Number Generation](#random-number-generation)
9. [Perlin & Simplex Noise](#perlin--simplex-noise)
10. [Color Types (CRGB, CHSV, HSV16)](#color-types)
11. [HSV ↔ RGB Conversion](#hsv--rgb-conversion)
12. [CRGB Methods (Per-Pixel)](#crgb-methods)
13. [Palette System](#palette-system)
14. [Predefined Palettes & Colors](#predefined-palettes--colors)
15. [Fill Functions](#fill-functions)
16. [Blending & Fading](#blending--fading)
17. [Blur](#blur)
18. [Gamma Correction & Color Temperature](#gamma-correction--color-temperature)
19. [Fixed-Point Types](#fixed-point-types)
20. [Timekeeping](#timekeeping)
21. [Power Management](#power-management)
22. [Utility Functions](#utility-functions)

---

## lib8tion Math Library

`lib8tion` (pronounced "libation") is FastLED's core 8-bit/16-bit integer math library. All operations avoid floating-point.

**Headers:** `src/lib8tion.h`, `src/platforms/shared/math8.h`

### Saturating Arithmetic

Clamps results instead of wrapping on overflow/underflow.

| Function | Description |
|----------|-------------|
| `qadd8(i, j)` | Add two `uint8_t`, clamp at 255 |
| `qsub8(i, j)` | Subtract two `uint8_t`, clamp at 0 |
| `qadd7(i, j)` | Add two `int8_t`, clamp at ±127 |
| `qmul8(i, j)` | Multiply two `uint8_t`, clamp at 255 |

### Wrapping Arithmetic

Standard operations that wrap on overflow (lower bits only).

| Function | Description |
|----------|-------------|
| `add8(i, j)` | 8-bit wrapping add |
| `sub8(i, j)` | 8-bit wrapping subtract |
| `mul8(i, j)` | 8×8 multiply, lower 8 bits |
| `add8to16(i, j)` | Add `uint8_t` to `uint16_t` |

### Averaging

| Function | Description |
|----------|-------------|
| `avg8(i, j)` | Average two `uint8_t`: `(i + j) >> 1` |
| `avg8r(i, j)` | Average with rounding: `(i + j + 1) >> 1` |
| `avg16(i, j)` | Average two `uint16_t` |
| `avg16r(i, j)` | Average `uint16_t` with rounding |
| `avg7(i, j)` | Average two signed `int8_t` |
| `avg15(i, j)` | Average two signed `int16_t` |

### Modulo & Absolute Value

| Function | Description |
|----------|-------------|
| `mod8(a, m)` | `a % m` (repeated subtraction) |
| `addmod8(a, b, m)` | `(a + b) % m` |
| `submod8(a, b, m)` | `(a - b) % m` |
| `abs8(i)` | Absolute value of `int8_t` |

### Square Root

| Function | Description |
|----------|-------------|
| `sqrt16(x)` | Square root of `uint16_t` → `uint8_t` (3× faster than Arduino `sqrt()`) |
| `sqrt8(x)` | Square root of `uint8_t` |

---

## Scaling & Dimming

**Headers:** `src/platforms/shared/scale8.h`, `src/platforms/scale8.h`

### Scaling (Fractional Multiply)

`scale8(i, scale)` = `(i × scale) / 256`. Think of `scale` as a fraction: 128 = 50%, 64 = 25%, etc.

| Function | Description |
|----------|-------------|
| `scale8(i, scale)` | Scale `uint8_t` by fraction (can reach zero) |
| `scale8_video(i, scale)` | Scale preserving non-zero (if `i > 0` and `scale > 0`, result > 0) |
| `scale16(i, scale)` | Scale `uint16_t` by `fract16` |
| `scale16by8(i, scale)` | Scale `uint16_t` by `uint8_t` fraction |
| `nscale8x3(r, g, b, scale)` | In-place scale 3 values (RGB) |
| `nscale8x3_video(r, g, b, scale)` | In-place video scale 3 values |

### Dimming & Brightening

Non-linear gamma-like curves for perceptual dimming.

| Function | Description |
|----------|-------------|
| `dim8_raw(x)` | Dim: `scale8(x, x)` (gamma ≈ 2.0) |
| `dim8_video(x)` | Dim preserving non-zero values |
| `dim8_lin(x)` | Linear dim: halves below 128, gamma above |
| `brighten8_raw(x)` | Inverse of `dim8_raw` |
| `brighten8_video(x)` | Inverse of `dim8_video` |
| `brighten8_lin(x)` | Inverse of `dim8_lin` |

---

## Trigonometry

**Header:** `src/platforms/shared/trig8.h`

### 8-bit (Fastest)

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `sin8(theta)` | 0–255 (full circle) | 0–255 (unsigned) | ~98% accurate, lookup-table based |
| `cos8(theta)` | 0–255 | 0–255 | `sin8(theta + 64)` |

**Note:** Output 0–255 maps to the range -1…+1. Midpoint (128) = zero crossing. Use for unsigned contexts where you want 0-based output.

### 16-bit (Higher Precision)

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `sin16(theta)` | 0–65535 (full circle) | -32767 to +32767 (signed) | >99% accurate |
| `cos16(theta)` | 0–65535 | -32767 to +32767 | `sin16(theta + 16384)` |

---

## Easing Functions

Attempt to smooth transitions. Apply to a linear 0–255 (or 0–65535) input to get curved output.

**Headers:** `src/lib8tion.h`, `src/fl/ease.h`

### Classic lib8tion Easing (8-bit and 16-bit)

| Function | Description | Speed |
|----------|-------------|-------|
| `ease8InOutQuad(i)` | Quadratic S-curve | ~13 cycles (AVR) |
| `ease8InOutCubic(i)` | Cubic S-curve: `3x² - 2x³` | ~18 cycles |
| `ease8InOutApprox(i)` | Fast approximate S-curve (within ~2% of cubic) | ~7 cycles |
| `ease16InOutQuad(i)` | 16-bit quadratic | |
| `ease16InOutCubic(i)` | 16-bit cubic | |

### Extended Easing (fl/ease.h)

Full set of separate in/out/in-out variants:

**8-bit:** `easeInQuad8`, `easeOutQuad8`, `easeInOutQuad8`, `easeInCubic8`, `easeOutCubic8`, `easeInOutCubic8`, `easeInSine8`, `easeOutSine8`, `easeInOutSine8`

**16-bit:** Same names with `16` suffix.

**Generic dispatch:** `ease8(EaseType type, i)` / `ease16(EaseType type, i)` — apply easing by enum value.

---

## Waveform Generators

Convert a monotonically increasing counter (e.g., `millis()` or angle) into oscillating output.

**Header:** `src/lib8tion.h`

| Function | Output Shape | Description |
|----------|-------------|-------------|
| `triwave8(in)` | △ Triangle | Linear up 0→254, then down. 3 cycles on AVR |
| `quadwave8(in)` | ∿ Smooth S | `ease8InOutQuad(triwave8(in))` — spends more time at peaks |
| `cubicwave8(in)` | ∿ Smoother S | `ease8InOutCubic(triwave8(in))` — even more time at peaks |
| `squarewave8(in, pulsewidth)` | ⎍ Square | 0 or 255. `pulsewidth` controls duty cycle (128 = 50%) |

**Usage pattern:**
```cpp
uint8_t pos = millis() / 10;        // steadily increasing
uint8_t brightness = triwave8(pos); // oscillates smoothly
```

---

## Beat Generators (BPM)

Generate waveforms synchronized to beats-per-minute. Excellent for music-reactive or rhythmic effects.

**Header:** `src/lib8tion.h`

### Sawtooth (Ramp)

| Function | Output | Description |
|----------|--------|-------------|
| `beat8(bpm, timebase)` | 0–255 | 8-bit sawtooth ramp at given BPM |
| `beat16(bpm, timebase)` | 0–65535 | 16-bit sawtooth |
| `beat88(bpm_88, timebase)` | 0–65535 | Q8.8 format BPM for fractional BPM values |

### Sine (Oscillating)

| Function | Output | Description |
|----------|--------|-------------|
| `beatsin8(bpm, low, high, timebase, phase)` | low–high | 8-bit sine wave oscillating between `low` and `high` |
| `beatsin16(bpm, low, high, timebase, phase)` | low–high | 16-bit sine wave |
| `beatsin88(bpm_88, low, high, timebase, phase)` | low–high | Q8.8 BPM sine wave |

**Usage pattern:**
```cpp
uint8_t hue = beatsin8(30);              // hue oscillates at 30 BPM
uint8_t pos = beatsin8(13, 0, NUM_LEDS); // position sweeps back and forth
uint8_t brightness = beatsin8(60, 100, 255, 0, 128); // pulse with phase offset
```

---

## Linear Interpolation & Mapping

**Header:** `src/lib8tion.h`

### Lerp

| Function | Description |
|----------|-------------|
| `lerp8by8(a, b, frac)` | Lerp between two `uint8_t`. `frac`: 0 = a, 255 = b |
| `lerp16by16(a, b, frac)` | Lerp `uint16_t` with 16-bit fraction |
| `lerp16by8(a, b, frac)` | Lerp `uint16_t` with 8-bit fraction |
| `lerp15by8(a, b, frac)` | Signed lerp (`int16_t`) with 8-bit fraction |
| `lerp15by16(a, b, frac)` | Signed lerp with 16-bit fraction |

### Value Mapping

| Function | Description |
|----------|-------------|
| `map8(in, rangeStart, rangeEnd)` | Map 0–255 to arbitrary range (faster than Arduino `map()`) |
| `map_range(val, in_min, in_max, out_min, out_max)` | Generic range mapping (template, `src/fl/map_range.h`) |
| `map_range_clamped(...)` | Same but clamps output to range |

### Integer Scaling (Type Conversion)

**Header:** `src/platforms/intmap.h`

Scale values between bit widths while preserving relative position (bit replication).

| Function | Description |
|----------|-------------|
| `map8_to_16(x)` | `uint8_t` → `uint16_t` (×0x0101) |
| `map16_to_8(x)` | `uint16_t` → `uint8_t` (right-shift with rounding) |
| `map8_to_32(x)` | `uint8_t` → `uint32_t` (×0x01010101) |
| `map16_to_32(x)` | `uint16_t` → `uint32_t` |
| `map32_to_16(x)` | `uint32_t` → `uint16_t` |
| `map32_to_8(x)` | `uint32_t` → `uint8_t` |

Signed variants: `smap8_to_16`, `smap16_to_8`, etc.

---

## Random Number Generation

**Header:** `src/lib8tion/random8.h`

Fast LCG-based PRNG. Formula: `X(n+1) = (2053 × X(n) + 13849) % 65536`

| Function | Description |
|----------|-------------|
| `random8()` | Random 0–255 |
| `random8(lim)` | Random 0 to `lim-1` |
| `random8(min, lim)` | Random `min` to `lim-1` |
| `random16()` | Random 0–65535 |
| `random16(lim)` | Random 0 to `lim-1` |
| `random16(min, lim)` | Random `min` to `lim-1` |
| `random16_set_seed(seed)` | Set PRNG seed |
| `random16_add_entropy(entropy)` | Mix in entropy |

---

## Perlin & Simplex Noise

Coherent noise for organic-looking patterns (fire, clouds, water, flowing effects).

**Header:** `src/noise.h`

### 8-bit Perlin Noise

| Function | Description |
|----------|-------------|
| `inoise8(x)` | 1D noise → 0–255 |
| `inoise8(x, y)` | 2D noise → 0–255 |
| `inoise8(x, y, z)` | 3D noise → 0–255 |
| `inoise8_raw(x)` | 1D raw noise → -70 to 70 |
| `inoise8_raw(x, y)` | 2D raw noise |
| `inoise8_raw(x, y, z)` | 3D raw noise |

### 16-bit Perlin Noise

| Function | Description |
|----------|-------------|
| `inoise16(x)` | 1D noise → 0–65535 |
| `inoise16(x, y)` | 2D noise → 0–65535 |
| `inoise16(x, y, z)` | 3D noise → 0–65535 |
| `inoise16(x, y, z, t)` | 4D noise (with time dimension) |
| `inoise16_raw(...)` | Raw variants → -18000 to 18000 |

### Simplex Noise (32-bit inputs)

| Function | Description |
|----------|-------------|
| `snoise16(x)` | 1D simplex → 0–65535 |
| `snoise16(x, y)` | 2D simplex |
| `snoise16(x, y, z)` | 3D simplex |
| `snoise16(x, y, z, w)` | 4D simplex |

### Noise Fill Helpers

| Function | Description |
|----------|-------------|
| `fill_noise8(leds, n, octaves, x, scale, hue_oct, hue_x, hue_scale, time)` | Fill LED array with 8-bit noise colors |
| `fill_noise16(leds, n, ...)` | Fill with 16-bit noise colors |
| `fill_2dnoise8(leds, w, h, ...)` | Fill 2D matrix with noise |
| `fill_raw_noise8(data, n, octaves, x, scalex, time)` | Fill raw `uint8_t` array with noise |
| `fill_raw_2dnoise8(data, w, h, octaves, x, sx, y, sy, time)` | Fill 2D raw data with noise |

**Usage pattern for POV effects:**
```cpp
// Flowing fire-like noise
uint16_t t = millis() * 5;
for (int ring = 0; ring < NUM_RINGS; ring++) {
    uint8_t noise_val = inoise8(ring * 30, t);
    // Map noise to palette color...
}
```

---

## Color Types

### CRGB (8-bit RGB)

**Header:** `src/fl/rgb8.h`

```cpp
struct CRGB {
    union { uint8_t r; uint8_t red; };
    union { uint8_t g; uint8_t green; };
    union { uint8_t b; uint8_t blue; };
    uint8_t raw[3];  // Array access: raw[0]=r, raw[1]=g, raw[2]=b
};
```

Constructors: `CRGB(r, g, b)`, `CRGB(0xRRGGBB)`, `CRGB(HTMLColorCode)`, `CRGB(CHSV)`.

### CHSV / hsv8 (8-bit HSV)

**Header:** `src/fl/hsv.h`

```cpp
struct CHSV {
    uint8_t h;  // Hue: 0-255 = full color wheel
    uint8_t s;  // Saturation: 0=gray, 255=fully saturated
    uint8_t v;  // Value: 0=black, 255=brightest
};
```

### HSV16 (16-bit HSV, High Precision)

**Header:** `src/fl/hsv16.h`

```cpp
struct HSV16 {
    uint16_t h, s, v;  // 16-bit channels
};
```

Methods: `ToRGB()`, `operator CRGB()` (auto-conversion), `colorBoost(sat_fn, lum_fn)`.

Convert from CRGB: `myColor.toHSV16()`.

---

## HSV ↔ RGB Conversion

**Header:** `src/hsv2rgb.h`

| Function | Description |
|----------|-------------|
| `hsv2rgb_rainbow(hsv)` | HSV→RGB emphasizing yellow/orange (best for most LED effects) |
| `hsv2rgb_spectrum(hsv)` | HSV→RGB with even spectrum distribution |
| `hsv2rgb_raw(hsv, rgb)` | Fast raw conversion (max hue = 191) |
| `hsv2rgb_fullspectrum(hsv)` | Constant-brightness conversion |
| `rgb2hsv_approximate(rgb)` | RGB→HSV (approximate, lossy) |

**Predefined Hue Constants:**

| Constant | Value | Color |
|----------|-------|-------|
| `HUE_RED` | 0 | Red |
| `HUE_ORANGE` | 32 | Orange |
| `HUE_YELLOW` | 64 | Yellow |
| `HUE_GREEN` | 96 | Green |
| `HUE_AQUA` | 128 | Aqua |
| `HUE_BLUE` | 160 | Blue |
| `HUE_PURPLE` | 192 | Purple |
| `HUE_PINK` | 224 | Pink |

---

## CRGB Methods

**Header:** `src/fl/rgb8.h`

### Setting Color

| Method | Description |
|--------|-------------|
| `setRGB(r, g, b)` | Set RGB directly |
| `setHSV(hue, sat, val)` | Set from HSV (auto-converts) |
| `setHue(hue)` | Set hue at max saturation & value |
| `setColorCode(0xRRGGBB)` | Set from packed color code |

### Arithmetic (all saturating)

| Operator | Description |
|----------|-------------|
| `c1 += c2` | Add colors (saturate at 255) |
| `c1 -= c2` | Subtract colors (saturate at 0) |
| `c *= d` | Multiply by scalar |
| `c /= d` | Divide by scalar |
| `c1 \|= c2` | "OR" = take max of each channel |
| `c1 &= c2` | "AND" = take min of each channel |
| `-c` | Invert: `(255-r, 255-g, 255-b)` |

### Scaling & Fading

| Method | Description |
|--------|-------------|
| `nscale8(scale)` | Scale by N/256ths (can reach black) |
| `nscale8_video(scale)` | Scale preserving non-zero values |
| `scale8(scale)` | Returns new scaled CRGB (non-destructive) |
| `fadeToBlackBy(amount)` | Fade toward black (can reach it) |
| `fadeLightBy(amount)` | Fade preserving non-zero |
| `nscale8(CRGB mask)` | Per-channel scaling with color mask |
| `maximizeBrightness(limit)` | Scale to use full brightness range |

### Analysis

| Method | Description |
|--------|-------------|
| `getLuma()` | Perceived brightness: `(54R + 183G + 18B) / 256` |
| `getAverageLight()` | Simple average of R, G, B |
| `getParity()` | Returns 0 or 1 based on channel sum |

### Blending

| Method | Description |
|--------|-------------|
| `lerp8(other, amount)` | Linear interpolation (8-bit precision) |
| `lerp16(other, frac)` | Linear interpolation (16-bit precision) |

### Conversion

| Method | Description |
|--------|-------------|
| `toHSV16()` | Convert to high-precision `HSV16` |
| `colorBoost(sat_fn, lum_fn)` | Boost saturation while preserving hue |

---

## Palette System

**Header:** `src/fl/colorutils.h`

### Palette Classes

| Class | Entries | RAM Size | Use Case |
|-------|---------|----------|----------|
| `CRGBPalette16` | 16 | 48 bytes | Most common — interpolates to 256 on lookup |
| `CRGBPalette32` | 32 | 96 bytes | Higher fidelity |
| `CRGBPalette256` | 256 | 768 bytes | Full resolution (no interpolation) |
| `CHSVPalette16` | 16 | 48 bytes | HSV-space palette |

### Creating Palettes

```cpp
// From solid color
CRGBPalette16 pal(CRGB::Blue);

// From gradient (2-4 colors)
CRGBPalette16 pal(CRGB::Black, CRGB::Red, CRGB::Yellow, CRGB::White);

// From predefined
CRGBPalette16 pal = LavaColors_p;

// Custom gradient (PROGMEM)
DEFINE_GRADIENT_PALETTE(myPal) {
    0,    0,   0,   0,    // black at position 0
    128,  255, 0,   0,    // red at position 128
    255,  255, 255, 255   // white at position 255
};
CRGBPalette16 pal = myPal;
```

### Palette Color Lookup

| Function | Description |
|----------|-------------|
| `ColorFromPalette(pal, index, brightness, blendType)` | Get interpolated color. `index` 0–255 maps across palette |
| `ColorFromPaletteExtended(pal, index16, brightness, blendType)` | Higher precision with 16-bit index |

**Blend types:** `LINEARBLEND` (smooth, default), `NOBLEND` (stepping), `LINEARBLEND_NOWRAP` (no hue wrap).

### Palette Transitions

| Function | Description |
|----------|-------------|
| `nblendPaletteTowardPalette(current, target, maxChanges)` | Smoothly morph one palette toward another. Call repeatedly. |
| `UpscalePalette(src16, dst256)` | Upscale 16-entry palette to 256 entries |

---

## Predefined Palettes & Colors

### Built-in Palettes (PROGMEM)

| Palette | Description |
|---------|-------------|
| `RainbowColors_p` | Full rainbow spectrum |
| `RainbowStripeColors_p` | Rainbow with black stripes |
| `CloudColors_p` | Blue/white cloud colors |
| `LavaColors_p` | Red/yellow/black lava |
| `OceanColors_p` | Ocean blues |
| `ForestColors_p` | Forest greens |
| `PartyColors_p` | Vibrant party colors |
| `HeatColors_p` | Black → red → yellow → white heat map |

### Predefined Color Constants

Over 140 named colors accessible as `CRGB::ColorName`:

**Common:** `Black`, `White`, `Red`, `Green`, `Blue`, `Yellow`, `Cyan`, `Magenta`, `Orange`, `Purple`, `Pink`, `Gold`, `Silver`

**Special:** `FairyLight` (0xFFE42D, warm LED fairy light), `Plaid` (0xCC5533)

**Grayscale:** `Gray0` through `Gray100` (TCL-style percentages)

---

## Fill Functions

**Header:** `src/fl/fill.h`

### Solid & Rainbow

| Function | Description |
|----------|-------------|
| `fill_solid(leds, n, color)` | Fill array with single color |
| `fill_rainbow(leds, n, startHue, deltaHue)` | Fill with rainbow gradient |
| `fill_rainbow_circular(leds, n, startHue, reversed)` | Rainbow that wraps end → start |

### Gradients

| Function | Description |
|----------|-------------|
| `fill_gradient(leds, n, c1, c2, direction)` | 2-color HSV gradient |
| `fill_gradient(leds, n, c1, c2, c3, direction)` | 3-color HSV gradient |
| `fill_gradient(leds, n, c1, c2, c3, c4, direction)` | 4-color HSV gradient |
| `fill_gradient_RGB(leds, n, c1, c2)` | RGB-space gradient (2-4 colors) |

**Gradient direction codes:** `SHORTEST_HUES` (default), `LONGEST_HUES`, `FORWARD_HUES`, `BACKWARD_HUES` — controls which way around the color wheel the hue interpolates.

### Palette Fill

| Function | Description |
|----------|-------------|
| `fill_palette(leds, n, startIdx, deltaIdx, pal, brightness, blendType)` | Fill from palette, stepping `deltaIdx` per LED |
| `fill_palette_circular(leds, n, startIdx, pal, brightness, blendType, reversed)` | Fill palette wrapping circularly |
| `map_data_into_colors_through_palette(leds, n, data, dataLen, pal, brightness, blendType)` | Map arbitrary data values through palette |

---

## Blending & Fading

**Header:** `src/fl/colorutils.h`

### Color Blending

| Function | Description |
|----------|-------------|
| `blend(c1, c2, amount)` | Blend two CRGB colors (returns new color) |
| `blend(c1, c2, amount, direction)` | Blend two CHSV colors with hue direction |
| `nblend(existing, overlay, amount)` | Destructive blend into `existing` |
| `blend(src1, src2, dest, count, amount)` | Blend two arrays into destination |
| `nblend(existing, overlay, count, amount)` | Destructive array blend |

### Array Fading

| Function | Description |
|----------|-------------|
| `fadeToBlackBy(leds, n, amount)` | Fade array toward black (reaches it) |
| `fadeLightBy(leds, n, amount)` | Fade array preserving non-zero (never fully black) |
| `nscale8(leds, n, scale)` | Scale array brightness (can reach zero) |
| `nscale8_video(leds, n, scale)` | Scale array preserving non-zero |
| `fadeUsingColor(leds, n, colormask)` | Fade through a color mask (e.g., warm fade: `CRGB(200,100,50)` fades through warm tones before black) |

### Heat Color

| Function | Description |
|----------|-------------|
| `HeatColor(temperature)` | Black-body radiation: 0 = black, 85 = red, 170 = yellow, 255 = white. Great for fire effects. |

---

## Blur

**Header:** `src/fl/blur.h`

| Function | Description |
|----------|-------------|
| `blur1d(leds, n, amount)` | 1D blur (spreads to 2 neighbors) |
| `blur2d(leds, w, h, amount, xymap)` | 2D blur (spreads to 8 neighbors) |
| `blurRows(leds, w, h, amount, xymap)` | Blur each row horizontally |
| `blurColumns(leds, w, h, amount, xymap)` | Blur each column vertically |

**Blur amount guide:** 0 = none, 64 = moderate, 172 = maximum smooth spreading. 173–255 = wider but increasingly flickery.

---

## Gamma Correction & Color Temperature

### Gamma Correction

**Headers:** `src/fl/colorutils.h`, `src/fl/gamma.h`

| Function | Description |
|----------|-------------|
| `applyGamma_video(brightness, gamma)` | Apply gamma to single `uint8_t` |
| `applyGamma_video(color, gamma)` | Apply uniform gamma to CRGB |
| `applyGamma_video(color, gR, gG, gB)` | Per-channel gamma to CRGB |
| `napplyGamma_video(leds, n, gamma)` | Apply gamma to array (in-place) |
| `gamma_2_8(value)` | Gamma LUT at power 2.8 → `uint16_t` |
| `gamma16(rgb, &r16, &g16, &b16)` | Gamma correction returning 16-bit values |

### Color Correction Constants

**Header:** `src/color.h`

**LED Type Corrections:**

| Constant | Value | Use Case |
|----------|-------|----------|
| `TypicalSMD5050` / `TypicalLEDStrip` | 0xFFB0F0 | Standard LED strips (SK6812, WS2812, etc.) |
| `Typical8mmPixel` / `TypicalPixelString` | 0xFFE08C | Through-hole pixel strings |
| `UncorrectedColor` | 0xFFFFFF | No correction |

### Color Temperature Constants

**Header:** `src/color.h`

| Constant | Kelvin | Use Case |
|----------|--------|----------|
| `Candle` | 1900K | Warm candlelight |
| `Tungsten40W` | 2600K | Warm incandescent |
| `Tungsten100W` | 2850K | Standard incandescent |
| `Halogen` | 3200K | Halogen lamp |
| `DirectSunlight` | 6000K | Neutral daylight |
| `OvercastSky` | 7000K | Cool daylight |
| `ClearBlueSky` | 20000K | Very cool blue |

Additional gaseous sources: `WarmFluorescent`, `StandardFluorescent`, `CoolWhiteFluorescent`, `MercuryVapor`, `SodiumVapor`, `MetalHalide`, `HighPressureSodium`.

### Color Boost (Alternative to Gamma)

| Function | Description |
|----------|-------------|
| `color.colorBoost(sat_fn, lum_fn)` | Boost saturation while preserving hue (better than gamma for WS2812) |
| `CRGB::colorBoost(src, dst, count, sat_fn, lum_fn)` | Batch color boost |

---

## Fixed-Point Types

**Header:** `src/lib8tion/qfx.h`

| Type | Bits | Integer.Fraction | Description |
|------|------|-------------------|-------------|
| `fract8` | 8 | 0.8 | Fraction 0–255 = 0.0–1.0 |
| `fract16` | 16 | 0.16 | Fraction 0–65535 = 0.0–1.0 |
| `accum88` | 16 | 8.8 | Q8.8: integer 0–255 + 8-bit fraction (used for BPM) |
| `q44` | 8 | 4.4 | 4 integer bits, 4 fractional |
| `q62` | 8 | 2.6 | 2 integer bits, 6 fractional |
| `q88` | 16 | 8.8 | Same as accum88 |
| `q124` | 16 | 4.12 | 4 integer bits, 12 fractional |

**Float conversion:** `sfract15ToFloat(y)` / `floatToSfract15(f)` — convert between float and 16-bit signed fixed-point.

---

## Timekeeping

**Header:** `src/lib8tion.h`

### Time Getters

| Function | Description |
|----------|-------------|
| `seconds16()` | Seconds since boot (16-bit) |
| `minutes16()` | Minutes since boot (16-bit) |
| `hours8()` | Hours since boot (8-bit) |
| `bseconds16()` | "Binary seconds" (1024ms units) |

### Periodic Execution Macros

| Macro | Description |
|-------|-------------|
| `EVERY_N_MILLIS(N) { ... }` | Execute block every N milliseconds |
| `EVERY_N_SECONDS(N) { ... }` | Execute block every N seconds |
| `EVERY_N_MINUTES(N) { ... }` | Execute block every N minutes |
| `EVERY_N_HOURS(N) { ... }` | Execute block every N hours |
| `EVERY_N_MILLISECONDS_DYNAMIC(func) { ... }` | Dynamic period (function returns period) |
| `EVERY_N_MILLISECONDS_RANDOM(min, max) { ... }` | Random interval between min/max |

Named variants with `_I(NAME, N)` suffix allow resetting or accessing the timer externally.

---

## Power Management

**Header:** `src/power_mgt.h`

| Function | Description |
|----------|-------------|
| `set_max_power_in_volts_and_milliamps(volts, mA)` | Set power budget |
| `set_max_power_in_milliwatts(mW)` | Set power budget in milliwatts |
| `calculate_unscaled_power_mW(leds, n)` | Calculate power draw at max brightness |
| `calculate_max_brightness_for_power_mW(leds, n, target_bright, max_mW)` | Get safe brightness for power budget |
| `calculate_max_brightness_for_power_vmA(leds, n, target_bright, V, mA)` | Get safe brightness from V/mA |

---

## Utility Functions

### Clamping

**Header:** `src/fl/clamp.h`

| Function | Description |
|----------|-------------|
| `clamp(value, min, max)` | Clamp value to range (template) |

### Blending (8-bit)

**Header:** `src/platforms/shared/math8.h`

| Function | Description |
|----------|-------------|
| `blend8(a, b, amountOfB)` | Blend two `uint8_t` values |

### Memory Operations (AVR-optimized, libc aliases on ESP32)

**Header:** `src/lib8tion/memmove.h`

`memmove8()`, `memcpy8()`, `memset8()` — standard memory ops.

### Useful Macros

| Macro | Description |
|-------|-------------|
| `FL_MIN(a, b)` | Minimum |
| `FL_MAX(a, b)` | Maximum |
| `FL_ABS(x)` | Absolute value |
| `FL_PI` | Pi (3.14159...) |
| `FL_PROGMEM` | Flash storage modifier |

---

## Quick Reference: Which Function for What?

| I want to... | Use |
|--------------|-----|
| Smoothly pulse brightness | `beatsin8(bpm, low, high)` |
| Fire / heat effect | `HeatColor(temperature)` + `HeatColors_p` palette |
| Flowing organic patterns | `inoise8(x, y)` or `inoise16(x, y)` |
| Smooth color transition | `blend(c1, c2, amount)` or `nblendPaletteTowardPalette()` |
| Rainbow across LEDs | `fill_rainbow()` or `fill_palette()` with `RainbowColors_p` |
| Fade trail behind moving dot | `fadeToBlackBy(leds, n, 20)` each frame |
| Map sensor reading to color | `ColorFromPalette(pal, sensorValue)` |
| Smooth eased animation | `ease8InOutCubic(linear_position)` |
| Random sparkle | `random8() < 50` to trigger, `CRGB::White` for spark |
| Perceptual dimming | `dim8_video(brightness)` or `scale8_video()` |
| Timed color changes | `EVERY_N_SECONDS(10) { targetPalette = newPal; }` |
| Rhythmic movement | `beat8(bpm)` for position, `beatsin8(bpm)` for oscillation |
| Blur/soften display | `blur1d(leds, n, 64)` |
| Anti-aliased position | `lerp8by8(colorA, colorB, fractionalPos)` |
