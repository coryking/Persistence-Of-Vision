# FastLED Investigation - January 31, 2026

## Purpose

This investigation examined whether the POV display project should upgrade from its pinned FastLED commit (`ac595965af`, Nov 27, 2025) to current master (`8cd217f137`, Jan 31, 2026). The core questions were:

1. **Is issue #1609 (ESP32 SPI DMA chunking) fixed?** - Can we drop NeoPixelBus?
2. **Can we improve dynamic range?** - There's a big perceptual gap between the darkest color and black on HD107S LEDs. The 5-bit per-LED brightness field could help.
3. **What's new?** - Noise functions, effects, API changes since the pin.

## Key Findings

**Dynamic Range Solution Found:** FastLED has an "HD mode" controller (`HD107ControllerHD`) that uses gamma-optimized 5-bit brightness instead of hardcoding it to max. This gives ~8x finer control at the dark end. Just change `HD107` to `HD107ControllerHD` in `addLeds<>()`.

**SPI Fix Exists But Disabled:** Bulk DMA transfer is available via `#define FASTLED_ESP32_SPI_BULK_TRANSFER 1` but off by default. Worth testing - could eliminate need for NeoPixelBus.

**Noise API Unchanged:** All noise functions (`inoise8`, `inoise16`, `fill_noise8`, etc.) are identical between pinned and current. Safe to upgrade.

**Breaking Changes:** `BulkClockless` API removed (unlikely to affect this project), namespace changed from `ftl/` to `fl/stl/`.

## Investigation Files

| File | Contents |
|------|----------|
| [esp32-spi-analysis.md](esp32-spi-analysis.md) | ESP32 SPI driver code paths, bulk transfer fix, comparison to NeoPixelBus |
| [brightness-and-primitives.md](brightness-and-primitives.md) | How FastLED exposes 5-bit brightness, HD mode controllers, CRGB limitations |
| [noise-effects-api.md](noise-effects-api.md) | Noise function changes (none), new features (fonts, audio), breaking changes |
| [neopixelbus-brightness.md](neopixelbus-brightness.md) | NeoPixelBus per-LED brightness via `DotStarLbgrFeature`, POV flicker warning |

## Hardware Context

- **LEDs:** HD107S (APA102-compatible, SPI clocked, 8-bit RGB + 5-bit brightness)
- **MCU:** ESP32-S3 (Seeed XIAO)
- **Application:** POV (persistence of vision) spinning LED display
- **Current setup:** FastLED for effects + NeoPixelBus for SPI output (workaround for FastLED SPI issues)

## Repository References

| Location | Description |
|----------|-------------|
| `~/projects/FastLED` | Local FastLED repo (upstream remote: `FastLED`, fork: `origin`) |
| `~/projects/POV_Project/led_display/platformio.ini` | Pinned commit reference |
| `~/projects/POV_Project/docs/datasheets/HD107S-LED-Datasheet.txt` | HD107S datasheet |
| `~/projects/POV_Project/docs/datasheets/HD108-led.pdf` | HD108 (16-bit) datasheet |

## FastLED Key Files

```
src/chipsets.h                              # HD107, APA102, HD108 definitions
src/five_bit_hd_gamma.h                     # HD mode gamma algorithm
src/platforms/esp/32/fastspi_esp32*.h       # ESP32 SPI drivers
src/fl/noise.h                              # Noise functions
src/fl/font/truetype.h                      # TrueType font support (new)
src/fl/audio/                               # Audio analysis system (new)
```

## Recommended Next Steps

1. **Quick win:** Try `HD107ControllerHD` instead of `HD107` for better dark-end gradation
2. **Test SPI fix:** Add `#define FASTLED_ESP32_SPI_BULK_TRANSFER 1` and benchmark
3. **If both work:** Consider dropping NeoPixelBus dependency entirely
4. **Hardware upgrade path:** HD108 LEDs offer 16-bit color (65536 levels vs 256)
