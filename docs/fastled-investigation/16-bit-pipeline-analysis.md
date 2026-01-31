# 16-Bit Render Pipeline Analysis

*Investigation Date: January 31, 2026*

## Problem Statement

The hd_gamma_test project demonstrates improved dark-end gradation using FastLED's `five_bit_hd_gamma_bitshift()`. However, the render pipeline operates in 8-bit space before gamma expansion:

```
8-bit HSV → 8-bit RGB → gamma(8→16) → quantize(16→8+5)
```

Banding can occur in the 8-bit render stage, before gamma expansion recovers precision.

## Key Discovery

The core quantization function accepts 16-bit input directly:

```cpp
// Location: src/fl/five_bit_hd_gamma.h
void five_bit_bitshift(u16 r16, u16 g16, u16 b16, u8 brightness,
                       CRGB* out, u8* out_power_5bit);
```

The "HD gamma" wrapper `five_bit_hd_gamma_bitshift()` does:
1. `gamma16(CRGB)` → expands 8-bit to 16-bit with γ=2.8
2. Color correction scaling (optional)
3. Calls `five_bit_bitshift()` with the 16-bit values

For a 16-bit render pipeline, you can skip step 1 and call `five_bit_bitshift()` directly.

## Hardware Constraint

HD107S accepts: 8-bit RGB + 5-bit brightness per LED.

Effective dynamic range: ~13 bits per channel (8 bits × 32 brightness levels).

Working in 16-bit provides 3 extra bits of precision for render math before quantization.

## Pipeline Options

### Option A: 16-Bit Linear → Gamma at Output

**Pipeline:**
```
16-bit noise/math (linear) → gamma16(16→16) → five_bit_bitshift()
```

**Concept:**
- Render in linear light space (physically accurate blending)
- Apply gamma correction at output for perceptual uniformity

**Challenge:**
FastLED's `gamma_2_8()` expects 8-bit input. A 16-bit gamma LUT would need 65536 entries (128KB).

**Alternative:** Apply gamma as a math operation instead of LUT:
```cpp
// Approximate γ=2.8 on 16-bit value
// output = (input^2.8 / 65535^1.8) scaled to 16-bit
```

### Option B: 16-Bit Perceptual → Skip Gamma

**Pipeline:**
```
16-bit noise/math (perceptual) → five_bit_bitshift() directly
```

**Concept:**
- Render in perceptual space (like sRGB)
- 16-bit values represent perceived brightness, not physical light
- No gamma step needed - values pass through to quantization

**Trade-off:**
- Blending in perceptual space is less physically accurate
- Gradients may appear slightly different
- Simpler implementation

### Option C: 8-Bit Render → Existing HD Gamma (Baseline)

**Pipeline:**
```
8-bit HSV → 8-bit RGB → five_bit_hd_gamma_bitshift()
```

**Current approach in hd_gamma_test.**

- Banding originates in 8-bit render space
- HD gamma helps at output but can't recover lost precision

## Comparison: Where Banding Occurs

| Stage | 8-Bit Pipeline | 16-Bit Pipeline |
|-------|---------------|-----------------|
| Noise generation | 256 levels | 65536 levels |
| Color blending | 256 levels | 65536 levels |
| HSV→RGB conversion | 256 levels | 65536 levels |
| Gamma correction | 65536 levels (expanded) | N/A or 65536 |
| Quantization | 8+5 bit output | 8+5 bit output |

The 16-bit pipeline delays quantization until the final step.

## 16-Bit Workflow Sketch

Using FastLED primitives for a 16-bit render:

```cpp
// 1. Generate 16-bit noise
uint16_t noise_val = inoise16(x * 256, y * 256, time);

// 2. Create 16-bit color components
uint16_t r16 = scale16(noise_val, max_red);
uint16_t g16 = scale16(noise_val, max_green);
uint16_t b16 = scale16(noise_val, max_blue);

// 3. Apply easing or other 16-bit math
r16 = easeInOutQuad16(r16);

// 4. Blend with another color
r16 = lerp16by16(r16, other_r16, blend_frac);

// 5. Quantize to 8+5 for output
CRGB out_rgb;
uint8_t out_brightness;
fl::five_bit_bitshift(r16, g16, b16, 255, &out_rgb, &out_brightness);
```

## HSV16 Workflow

For hue-based effects:

```cpp
// 1. Create 16-bit HSV
HSV16 color;
color.h = angle * 182;  // 0-65535 hue from 0-360°
color.s = 65535;        // full saturation
color.v = brightness16; // 16-bit value

// 2. Convert to 8-bit RGB (loses precision here)
CRGB rgb = color.ToRGB();

// 3. Then apply HD gamma...
```

**Issue:** `HSV16::ToRGB()` returns 8-bit CRGB, losing the 16-bit precision.

**Alternative:** Implement 16-bit HSV→RGB that outputs `u16 r16, g16, b16` directly.

## Gamma Considerations

### Why Gamma Exists

LEDs emit light linearly with PWM duty cycle, but human perception is logarithmic. Gamma correction maps perceptual values (what you want) to physical values (what LEDs do).

### When to Apply Gamma

| Approach | Gamma Applied | Notes |
|----------|---------------|-------|
| Linear 16-bit | At output (before quantization) | Most accurate blending |
| Perceptual 16-bit | Never (skip it) | Input already "looks right" |
| 8-bit with HD gamma | Built into `five_bit_hd_gamma_bitshift()` | Current approach |

### Gamma LUT Options

**8-bit input (512 bytes):**
```cpp
const u16 _gamma_2_8[256];  // Exists in FastLED
```

**16-bit input (128KB):**
Would need 65536 entries. Impractical for embedded.

**Computed gamma:**
```cpp
// For 16-bit input, compute instead of LUT
float normalized = input / 65535.0f;
float gamma_out = powf(normalized, 2.8f);
uint16_t output = gamma_out * 65535;
```

Or use integer approximation with fixed-point math.

## Open Questions

1. **Does skipping gamma (Option B) produce acceptable visual results?**
   - The hd_gamma_test could be modified to compare

2. **Is there a fast integer approximation for 16-bit gamma?**
   - Piecewise linear? Polynomial approximation?

3. **Does HSV16 provide enough benefit if ToRGB() drops to 8-bit?**
   - May need custom 16-bit HSV→RGB

## Related Files

| File | Contents |
|------|----------|
| 16-bit-primitives.md | API reference for 16-bit functions |
| hd-gamma-math.md | How five_bit_bitshift() works internally |
| brightness-and-primitives.md | HD mode overview |
