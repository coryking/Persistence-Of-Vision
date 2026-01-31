# FastLED Noise, Effects & API Changes

*Investigation Date: January 31, 2026*
*Comparing: Pinned commit ac595965af (Nov 27, 2025) → Current master (8cd217f137)*
*Total commits between: ~961*

## Noise Functions: NO CHANGES

**Good news:** All core noise functions are **completely unchanged** between your pinned version and current master.

### Unchanged Noise API

```cpp
// 8-bit noise (all overloads work identically)
uint8_t inoise8(uint16_t x);
uint8_t inoise8(uint16_t x, uint16_t y);
uint8_t inoise8(uint16_t x, uint16_t y, uint16_t z);

// 16-bit noise
uint16_t inoise16(uint32_t x);
uint16_t inoise16(uint32_t x, uint32_t y);
uint16_t inoise16(uint32_t x, uint32_t y, uint32_t z);

// Raw noise (no scaling)
int8_t inoise8_raw(uint16_t x, uint16_t y, uint16_t z);
int16_t inoise16_raw(uint32_t x, uint32_t y, uint32_t z);

// Simplex noise
int16_t snoise16(uint32_t x);
int16_t snoise16(uint32_t x, uint32_t y);
int16_t snoise16(uint32_t x, uint32_t y, uint32_t z);
int16_t snoise16(uint32_t x, uint32_t y, uint32_t z, uint32_t w);

// Fill functions
void fill_noise8(CRGB *leds, int num_leds, ...);
void fill_noise16(CRGB *leds, int num_leds, ...);
void fill_2dnoise8(CRGB *leds, int width, int height, ...);
void fill_2dnoise16(CRGB *leds, int width, int height, ...);
```

### Shape Noise Functions (Already in your pinned version)

These were NOT new - they existed before your pin:

```cpp
// Ring patterns (great for POV!)
void noiseRingHSV16(CHSV* leds, uint16_t count, float radius, ...);
void noiseRingHSV8(CHSV* leds, uint16_t count, float radius, ...);
void noiseRingCRGB(CRGB* leds, uint16_t count, float radius, ...);

// Sphere/Cylinder patterns
void noiseSphereHSV16(...);
void noiseCylinderHSV16(...);
```

Location: `src/fl/noise.h`

---

## NEW Features Since Your Pin

### TrueType Font Rendering (MAJOR)

Full `.ttf`/`.ttc` font support:

```cpp
#include "fl/font/truetype.h"
#include "fl/font/truetype.cpp.hpp"  // Include ONCE in one .cpp

auto font = fl::Font::loadDefault();          // Covenant5x5 embedded font
fl::FontRenderer renderer(font, 10.0f);       // 10px height
fl::GlyphBitmap glyph = renderer.render('A'); // Render with antialiasing

// Draw to LEDs
for (int y = 0; y < glyph.height; ++y) {
    for (int x = 0; x < glyph.width; ++x) {
        uint8_t alpha = glyph.getPixel(x, y);  // 0-255 grayscale
        // Use alpha for blending
    }
}
```

Files: `src/fl/font/truetype.h`, `src/fl/font/truetype.cpp.hpp`

### Audio System v2.0 (MAJOR)

Real-time audio analysis with 20 components:

```cpp
#include "fl/audio/audio_context.h"

// Core detectors
BeatDetector, TempoDetector, FrequencyBands

// Advanced detectors
Energy, Transient, Note, Downbeat, Dynamics, Pitch,
Silence, Vocal, Percussion, Chord, Key, Mood, Buildup, Drop
```

AudioContext pattern: FFT computed once, shared across all detectors with lazy evaluation.

Location: `src/fl/audio/`

### Vorbis Audio Decoder

```cpp
#include "fl/codec/vorbis.h"

auto decoder = Vorbis::createDecoder();
decoder->begin(byteStream);
decoder->decodeNextFrame(&sample);

// Or decode all at once
auto samples = Vorbis::decodeAll(byteStream);
```

### Fx2dTo1d Adapter

Sample 2D effects into 1D LED strips:

```cpp
#include "fl/fx/fx2d_to_1d.h"

// Wrap any Fx2d effect and sample it using a ScreenMap
// Supports NEAREST and BILINEAR interpolation
```

---

## New Chipset Support

### HD108 (16-bit color!)

```cpp
FastLED.addLeds<HD108, DATA_PIN, CLOCK_PIN, RGB>(leds, NUM_LEDS);
```
- 65,536 levels per channel (vs 256 for HD107S)
- Automatic gamma 2.8 correction
- Same 5-bit brightness control
- 40MHz clock, 27kHz PWM

### WS2812B-V5 / WS2812B-Mini-V3

```cpp
FastLED.addLeds<WS2812BV5, DATA_PIN, GRB>(leds, NUM_LEDS);
```
- Tighter timing specs (220/580ns vs 250/625ns)

### UCS7604 (16-bit RGBW, BETA)

```cpp
FastLED.addLeds<UCS7604HD, DATA_PIN, GRB>(leds, NUM_LEDS);
```
- 16-bit color resolution
- Universal platform support

---

## New ESP32 Drivers

### LCD I80 Driver (ESP32-S3/P4)

```cpp
#define FASTLED_ESP32_LCD_DRIVER
#include "FastLED.h"
```
- Up to 16 parallel strips
- 1-80 MHz PCLK
- Serial.print() debugging supported

### Runtime Driver Control (ESP32)

```cpp
FastLED.setExclusiveDriver("RMT");     // Use only RMT
FastLED.setExclusiveDriver("SPI");     // Use only SPI
FastLED.setDriverEnabled("PARLIO", true);
FastLED.isDriverEnabled("RMT");        // Query state
```

---

## BREAKING CHANGES

### BulkClockless API REMOVED

If you use any of these, you must migrate:

```cpp
// REMOVED - will not compile
FastLED.addBulkLeds(...)
BulkClockless<...>
BulkStripConfig
```

**Migration:** Use Channel API (`fl/channels/channel.h`)

### Namespace Change: ftl → fl

```cpp
// Old (deprecated, still works with warning)
#include "ftl/math.h"

// New
#include "fl/stl/math.h"
```

### Header Path Changes

Old paths still work via trampolines but emit deprecation warnings:
- `src/fx/2d/blend.h` → `src/fl/fx/2d/blend.h`

---

## New Math Utilities

```cpp
#include "fl/stl/math.h"

// New functions
sinf, cosf, sqrtf, expf, logf, log2f, powf, fabsf,
atan2f, hypotf, atan, asin, acos, tan, ldexp

// qsort available
#include "fl/stl/cstdlib.h"
qsort(array, count, size, compare_func);
```

---

## Upgrade Compatibility Summary

### Safe (no changes needed):
- All noise functions (inoise8, inoise16, fill_noise8, etc.)
- Color utilities (blend, nblend, fadeToBlackBy, HeatColor)
- Palette functions (ColorFromPalette, etc.)
- CRGB/CHSV data types

### Requires code changes:
- BulkClockless users → migrate to Channel API
- `ftl/` includes → change to `fl/stl/`
- ESP32-S3: Avoid GPIO19/20 for LED pins (now enforced with static_assert)

### New opportunities:
- HD107ControllerHD for better dynamic range
- TrueType fonts for text effects
- Audio system for music-reactive effects
- noiseRing* functions for POV displays
