# FastLED Investigation - January 31, 2026 (Updated February 8, 2026)

## Purpose

This investigation examined FastLED's HD mode for improved dark-end dynamic range on HD107S LEDs, whether the ESP32 SPI fix allows dropping NeoPixelBus, and **16-bit rendering pipeline capabilities** for POV display applications.

## Core Questions

1. **Can we improve dark-end dynamic range?** The gap between RGB(1,0,0) and black is too large. The 5-bit per-LED brightness field could help.
2. **Is the ESP32 SPI fix working?** FastLED has `FASTLED_ESP32_SPI_BULK_TRANSFER` but reports suggest issues on ESP32-S3.
3. **Can we use FastLED effects with NeoPixelBus output?** Hybrid approach for best of both.
4. **What 16-bit support exists in FastLED?** For noise effects, palettes, and rendering pipeline. **(NEW - Feb 8, 2026)**

## Key Findings

### HD Mode for Better Dark End

FastLED's `HD107ControllerHD` (or standalone `five_bit_hd_gamma_bitshift()`) uses the 5-bit brightness field intelligently:
- Standard: brightness=31, RGB=1 = 1 step above black
- HD mode: brightness=1, RGB=31 = 31 steps at similar perceived brightness

See: brightness-and-primitives.md, hd-gamma-math.md

### NeoPixelBus Per-LED Brightness

`DotStarLbgrFeature` with `RgbwColor` exposes the 5-bit brightness field:
- W channel expects 0-31 directly (NOT scaled from 0-255)
- Matches FastLED's `five_bit_hd_gamma_bitshift()` output

See: neopixelbus-brightness.md

### Hybrid Approach Validated

FastLED effects -> `five_bit_hd_gamma_bitshift()` -> NeoPixelBus `RgbwColor` output works:
- API compatibility confirmed
- Per-pixel overhead negligible (~36us for 144 LEDs)

See: fastled-neopixelbus-hybrid.md

### ESP32 SPI Bulk Transfer

Fix exists (`FASTLED_ESP32_SPI_BULK_TRANSFER=1`) but needs validation on ESP32-S3. Jan 2025 reports suggest the code path may not be invoked correctly.

See: esp32-spi-analysis.md

### 16-Bit Rendering Support **(NEW - Feb 8, 2026)**

FastLED provides extensive 16-bit support for noise and math, but **NOT** for palettes or RGB color output:

**What FastLED Offers:**
- ✅ `inoise16()`, `snoise16()` - True 16-bit Perlin/Simplex noise
- ✅ `HSV16` - 16-bit HSV color type with 8-bit RGB conversion
- ✅ Shape-based noise helpers (`noiseRingHSV16`, `noiseCylinderHSV16`, `noiseSphereHSV16`)
- ✅ 16-bit math primitives (`scale16`, `lerp16by16`, easing functions)
- ✅ 16-bit gamma correction (`gamma_2_8`, `gamma16`)

**What FastLED Does NOT Offer:**
- ❌ No `CRGB16` type (16-bit RGB color)
- ❌ No 16-bit palette interpolation (`ColorFromPaletteExtended` is 8-bit only)
- ❌ Palette interpolation has NO hidden 16-bit precision - simple promotion (× 257) gives zero quality improvement

**Key Insight:** `ColorFromPaletteExtended` uses 8-bit math (`scale8`) that creates 16-bit intermediates but immediately truncates back to 8-bit. There's no hidden precision being thrown away - each palette segment produces exactly 256 discrete color values.

**Recommendation:** For 16-bit clean rendering pipeline:
- Use `inoise16()` for noise effects (genuine sub-8-bit precision)
- Write custom 16-bit palette interpolation using `lerp16by16()` if smooth gradients are critical
- Use `five_bit_bitshift()` to accept 16-bit input for HD107S output (8+5 format)

See: **16-bit-pipeline-status.md** (comprehensive analysis)

## Investigation Files

| File | Contents |
|------|----------|
| **16-bit-quick-reference.md** | **🔥 START HERE: TL;DR for 16-bit support, code patterns, recommendations** |
| **16-bit-pipeline-status.md** | **Comprehensive 16-bit support status, palette precision analysis, recommendations** |
| 16-bit-primitives.md | FastLED 16-bit API reference (noise, math, easing, HSV16) |
| 16-bit-pipeline-analysis.md | Options for 16-bit render pipeline with 8+5 output |
| brightness-and-primitives.md | HD mode overview, CRGB structure, standalone gamma function |
| hd-gamma-math.md | Algorithm deep dive, how 5-bit decomposition works |
| neopixelbus-brightness.md | DotStarLbgrFeature, W channel behavior, POV warning context |
| fastled-neopixelbus-hybrid.md | Hybrid approach validation, implementation sketch |
| esp32-spi-analysis.md | Bulk transfer fix, NeoPixelBus comparison, ESP32-S3 concerns |

## Test Project Learning Goals

A test project should empirically validate:

### 1. Dark-End Improvement
- Does HD gamma actually improve the RGB(1,0,0) to RGB(0,0,0) gulf?
- Visual comparison: dark gradient 0-32 with and without HD mode
- Does gamma 2.8 help or hurt at the extreme dark end?

### 2. POV Flicker Check
- NeoPixelBus warns about 5-bit brightness causing "more flicker" for POV
- The 5-bit field is current limiting, not PWM - is this warning valid?
- Empirical test: spin the display with varying brightness, look for artifacts

### 3. ESP32-S3 SPI Validation
- Does `FASTLED_ESP32_SPI_BULK_TRANSFER` actually work on ESP32-S3?
- Logic analyzer: verify batched transactions occur
- Compare timing to NeoPixelBus baseline

### 4. Hybrid Approach Verification
- FastLED effects -> gamma decomposition -> NeoPixelBus output
- Confirm 5-bit brightness appears correctly on wire
- Frame timing impact

## Hardware Context

- **LEDs:** HD107S (APA102-compatible, 8-bit RGB + 5-bit brightness)
- **MCU:** ESP32-S3 (Seeed XIAO)
- **Application:** POV spinning LED display

## Repository References

| Location | Description |
|----------|-------------|
| `~/projects/FastLED` | Local FastLED repo |
| `~/projects/POV_Project/led_display/` | POV display firmware |
| `~/projects/POV_Project/docs/datasheets/HD107S-LED-Datasheet.txt` | HD107S datasheet |

## FastLED Key Files

```
src/fl/five_bit_hd_gamma.h                      # HD gamma algorithm
src/fl/chipsets/apa102.h                        # HD107ControllerHD
src/platforms/esp/32/core/fastspi_esp32*.h      # ESP32 SPI drivers
```
