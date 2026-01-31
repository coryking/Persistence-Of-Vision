# NeoPixelBus Brightness Control for HD107S

*Investigation Date: January 31, 2026*

## Key Finding: YES - NeoPixelBus Exposes Per-LED Brightness

NeoPixelBus provides **full per-LED brightness control** for APA102/HD107S LEDs through different feature classes.

## Feature Options

### Option A: DotStarBgrFeature (Your Current Setup)

```cpp
NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(ledCount);
strip.SetPixelColor(0, RgbColor(255, 0, 0));  // Red, brightness ALWAYS 31
```

- Uses `RgbColor` (8-bit R, G, B)
- Brightness: **Fixed at maximum (31/31)**
- The 5-bit field is hardcoded to `0xFF` in `DotStarX4ByteFeature`

### Option B: DotStarLbgrFeature (Per-LED Brightness!)

```cpp
NeoPixelBus<DotStarLbgrFeature, DotStarSpi40MhzMethod> strip(ledCount);
strip.SetPixelColor(0, RgbwColor(255, 0, 0, 31));   // Red, brightness 31 (max)
strip.SetPixelColor(1, RgbwColor(255, 0, 0, 15));   // Red, brightness 15 (half)
strip.SetPixelColor(2, RgbwColor(255, 0, 0, 1));    // Red, brightness 1 (dim)
```

- Uses `RgbwColor` (8-bit R, G, B, W)
- The **W field controls brightness** (0-255, clamped to 0-31)
- Feature class: `DotStarL4ByteFeature`

### Option C: DotStarLrgb64Feature (16-bit Precision)

```cpp
NeoPixelBus<DotStarLrgb64Feature, DotStarSpi40MhzMethod> strip(ledCount);
strip.SetPixelColor(0, Rgbw64Color(65535, 0, 0, 31));  // Red, brightness 31
```

- Uses `Rgbw64Color` (16-bit R, G, B, W)
- Higher precision for sophisticated effects
- W field still clamped to 5-bit (0-31) for the hardware

## How the 5-bit Brightness Encodes

From `DotStarL4ByteFeature::applyPixelColor()`:

```cpp
*p++ = 0xE0 | (color.W < 31 ? color.W : 31); // upper three bits always 111
*p++ = color.B;
*p++ = color.G;
*p++ = color.R;
```

- Takes W field from RgbwColor (0-255)
- Clamps to 5-bit range (0-31)
- Encodes as: `111xxxxx` where `xxxxx` = brightness bits

## Frame Structure Comparison

**Current (DotStarBgrFeature):**
```
Byte 0: 11111111 (brightness = 31, always max)
Byte 1: Blue
Byte 2: Green
Byte 3: Red
```

**Per-LED (DotStarLbgrFeature):**
```
Byte 0: 111bbbbb (b = 5-bit brightness from W field, 0-31)
Byte 1: Blue
Byte 2: Green
Byte 3: Red
```

## Comparison Table

| Feature | Color Type | Brightness | Per-LED? |
|---------|-----------|------------|----------|
| DotStarBgrFeature | RgbColor | Fixed 31 | No |
| DotStarLbgrFeature | RgbwColor | W field → 0-31 | **Yes** |
| DotStarLrgb64Feature | Rgbw64Color | W field → 0-31 | **Yes** |

## Migration Path for Your Project

```cpp
// Change declaration from:
NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);

// To:
NeoPixelBus<DotStarLbgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);

// Change color setting from:
strip.SetPixelColor(i, RgbColor(r, g, b));

// To:
strip.SetPixelColor(i, RgbwColor(r, g, b, brightness)); // brightness 0-31
```

## Important Warning for POV Displays

From NeoPixelBus's own `Hd108Test.ino` example:

> "also note that it is not useful for POV displays as it will cause more flicker"

The per-LED brightness may introduce visual artifacts at high rotation speeds. **Test carefully** before using for your POV display.

## NeoPixelBus vs FastLED Comparison

| Feature | FastLED | NeoPixelBus |
|---------|---------|-------------|
| Per-LED Brightness | No (global only via setBrightness) | **Yes** (via W field) |
| HD Mode Gamma | Yes (HD107ControllerHD) | No built-in |
| Color Type | CRGB (3 bytes) | RgbwColor (4 bytes) |
| Brightness Range | 0-255 → 0-31 (global) | 0-31 per LED |

## Recommendations

### For Better Dark-End Dynamic Range:

**Option 1: FastLED HD Mode** (simpler)
- Use `HD107ControllerHD`
- Global brightness optimization, no per-LED control
- Compatible with all FastLED effects

**Option 2: NeoPixelBus Per-LED** (more control)
- Use `DotStarLbgrFeature` with `RgbwColor`
- True per-LED brightness control
- Must manage brightness yourself
- **Test for POV flicker issues**

**Option 3: Hybrid Approach**
- Use FastLED for effects/color math (CRGB arrays)
- Convert to NeoPixelBus RgbwColor for output with computed brightness
- Most flexible but most complex
