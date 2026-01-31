# NeoPixelBus Brightness Control for HD107S

*Investigation Date: January 31, 2026*

## Key Finding: NeoPixelBus Exposes Per-LED 5-bit Brightness

NeoPixelBus provides per-LED brightness control for APA102/HD107S LEDs through the "L" (Luminance) feature classes.

## Critical Detail: W Channel Input Range

**The W channel expects 0-31 directly. It does NOT scale 0-255 down to 0-31.**

From `DotStarL4ByteFeature::applyPixelColor()`:

```cpp
*p++ = 0xE0 | (color.W < 31 ? color.W : 31);
```

This means:
- Pass W=0 to W=31: used directly as brightness
- Pass W=32 to W=255: clamped to 31 (max brightness)
- There is NO mapping/scaling - just clamping

This matches FastLED's `five_bit_hd_gamma_bitshift()` output which produces 0-31.

## Feature Options

### DotStarBgrFeature (Brightness Hardcoded to Max)

```cpp
NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(ledCount);
strip.SetPixelColor(0, RgbColor(255, 0, 0));  // brightness ALWAYS 31
```

- Uses `RgbColor` (3 bytes)
- 5-bit brightness field hardcoded to 31

### DotStarLbgrFeature (Per-LED Brightness)

```cpp
NeoPixelBus<DotStarLbgrFeature, DotStarSpi40MhzMethod> strip(ledCount);
strip.SetPixelColor(0, RgbwColor(255, 0, 0, 31));  // brightness 31 (max)
strip.SetPixelColor(1, RgbwColor(255, 0, 0, 15));  // brightness 15
strip.SetPixelColor(2, RgbwColor(255, 0, 0, 1));   // brightness 1 (dim)
```

- Uses `RgbwColor` (4 bytes: R, G, B, W)
- W field = 5-bit brightness (0-31, clamped if higher)

## Wire Protocol

**DotStar/APA102 Frame Structure:**
```
Byte 0: 111bbbbb  (0xE0 | brightness, where bbbbb = 5-bit brightness 0-31)
Byte 1: Blue
Byte 2: Green
Byte 3: Red
```

The three high bits (111) are the frame marker. The five low bits are brightness.

## POV Flicker Warning - Context

NeoPixelBus examples include this warning:
> "also note that it is not useful for POV displays as it will cause more flicker"

**What this warning is about:**
- The 5-bit brightness field is **current limiting**, not PWM
- Changing brightness changes the LED's steady-state current draw
- At POV rotation speeds, rapidly varying brightness across pixels could produce quantization artifacts (32 discrete levels vs smooth gradients)

**What this warning is NOT about:**
- It's not about PWM frequency - the 5-bit field doesn't use PWM
- The RGB channels use high-frequency PWM (~26kHz), the 5-bit field does not

**Bottom line:** This needs empirical testing on the actual spinning display to determine if it's a real concern or overly cautious.

## Comparison Table

| Feature | Color Type | W Channel | Brightness |
|---------|-----------|-----------|------------|
| DotStarBgrFeature | RgbColor | N/A | Fixed 31 |
| DotStarLbgrFeature | RgbwColor | 0-31 direct | Per-LED |
| DotStarLrgb64Feature | Rgbw64Color | 0-31 direct | Per-LED |

## Key Files (NeoPixelBus)

```
src/internal/features/DotStarL4ByteFeature.h   # applyPixelColor() implementation
src/internal/colors/RgbwColor.h                # RgbwColor struct
```
