# POV Perlin Noise Color Theory

Lessons learned implementing single-channel Perlin noise with palette mapping for the POV display.

## The Problem: HSV vs Palette Mapping

FastLED's `noiseCylinderHSV8()` samples noise **3 times** (for H, S, V channels). We wanted a single-channel version for palette mapping - theoretically 3x faster.

### Key Insight: HSV Hue is Circular, Palettes are Linear

- **HSV Hue**: 0° wraps to 360° seamlessly (circular)
- **Palette Index**: 0 and 255 are hard endpoints (linear)

This matters because Perlin noise values at the extremes transition smoothly in HSV (wrap around), but hit "walls" with palettes.

## The Fix: ColorFromPaletteExtended

**Bug found**: `ColorFromPalette()` takes `uint8_t` index. We were passing `uint16_t` - it was being **truncated**, causing random-looking dots instead of smooth flow.

**Solution**: Use `ColorFromPaletteExtended()` which accepts `uint16_t` for full 16-bit precision (65,536 gradient steps).

```cpp
// Wrong - truncates to 8-bit
CRGB color = ColorFromPalette(palette, paletteIndex16, 255, LINEARBLEND);

// Correct - full 16-bit precision
CRGB color = ColorFromPaletteExtended(palette, paletteIndex16, 255, LINEARBLEND);
```

## Perlin Noise Distribution: Center-Biased

Perlin noise does NOT uniformly distribute across 0-65535. It follows a **bell curve / Gaussian distribution** - values cluster around the middle (~65% in center range).

### Testing the Hypothesis

Created a diagnostic palette:
- Entries 0-2: Pure RED
- Entries 6-9: Pure GREEN
- Entries 13-15: Pure MAGENTA

**Result**: ~65%+ green with rare red/magenta flashes. Confirmed center-bias.

### Implications for Palette Design

CRGBPalette16 has 16 entries spread across 0-255:
- **Entries 6-9**: Seen MOST often (~65%)
- **Entries 0-2, 13-15**: Seen RARELY (accent colors)

## Two Palette Strategies

### Strategy 1: Bright-Center Palettes

Put your primary colors in the middle, accents at edges.

```
[dark accents] → [PRIMARY COLORS] → [bright accents]
     0-2              6-9              13-15
    (rare)          (common)           (rare)
```

Good for: Traditional flowing effects where you want a dominant color.

### Strategy 2: Dark-Center Palettes (Recommended for POV)

Put BLACK in the middle, bright colors at edges.

```
[bright pops] → [NEAR BLACK] → [bright pops]
     0-2            6-9           13-15
    (rare)        (common)        (rare)
```

**Why this works better for POV:**
- Most of the time LEDs are nearly OFF
- Can drive at HIGHER brightness without washing out
- Rare accent colors POP dramatically against dark background
- Looks like: embers, bioluminescence, fireflies, void lightning

**The physics behind this:** On a spinning POV display, "dim" values don't render as smooth darkness. PWM dimming means the LED is at 100% brightness for a short duration - and on a spinning display, those brief pulses appear as discrete sparkles rather than blended dimness. Dark-centered palettes work *with* this effect: the "dark" regions become a subtle sparkle texture, and bright accents pop cleanly against it. See `docs/led_display/EFFECT_SYSTEM_DESIGN.md` § "The Spinning Brightness Floor" for the full explanation.

## Noise Rescaling

Raw `inoise16()` output ranges ~9000-59500, not 0-65535. FastLED provides rescaling:

```cpp
uint16_t raw = inoise16(nx, ny, nz, time);
uint16_t rescaled = fl::map_range_clamped(raw,
    fl::NOISE16_EXTENT_MIN,  // 9000
    fl::NOISE16_EXTENT_MAX,  // 59500
    uint16_t(0), uint16_t(65535));
```

## Implementation Summary

### noiseCylinderPalette16()

Single-channel cylindrical noise for palette mapping:

```cpp
inline uint16_t noiseCylinderPalette16(float angle, float height, uint32_t time, float radius) {
    // Cylindrical → Cartesian
    float x = cosf(angle);
    float y = sinf(angle);

    // Scale to noise space
    uint32_t nx = static_cast<uint32_t>((x + 1.0f) * 0.5f * radius * 0xffff);
    uint32_t ny = static_cast<uint32_t>((y + 1.0f) * 0.5f * radius * 0xffff);
    uint32_t nz = static_cast<uint32_t>(height * radius * 0xffff);

    // Single noise sample + rescale
    uint16_t raw = inoise16(nx, ny, nz, time);
    return fl::map_range_clamped(raw, fl::NOISE16_EXTENT_MIN, fl::NOISE16_EXTENT_MAX,
                                 uint16_t(0), uint16_t(65535));
}
```

### Usage in Effect

```cpp
uint16_t paletteIndex = noiseCylinderPalette16(angleRadians, height, timeOffset, radius);
CRGB color = ColorFromPaletteExtended(palette, paletteIndex, 255, LINEARBLEND);
```

## Palette Definitions

See `include/effects/NoiseField.h` for full palette definitions.

- `EmberDark_p` - Black with fiery orange/red pops
- `AbyssDark_p` - Black with cyan/white pops
- `VoidDark_p` - Black with purple/magenta pops
- `FireflyDark_p` - Black with green/yellow pops

## Performance

- **3x faster**: 1 noise sample vs 3 (HSV version)
- **Same quality**: Cylindrical mapping eliminates 0°/360° seam
- **Better for POV**: Dark-center palettes allow higher brightness
