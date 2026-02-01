# FastLED `five_bit_hd_gamma_bitshift` Investigation

**Investigation Date:** 2026-02-01
**FastLED Repository:** /Users/coryking/projects/FastLED
**Pinned Commit Under Analysis:** `ac595965af72db9c6f41e63e878bb564a66299fe`
**Pinned Commit Date:** 2025-11-27
**Current Master HEAD at time of investigation:** `8cd217f137`

---

## Purpose of This Document

This document contains factual findings about the `five_bit_hd_gamma_bitshift` function in FastLED, its dependencies, and changes between the pinned commit and current master.

---

## Table of Contents

1. [Function Overview](#function-overview)
2. [Call Chain Analysis](#call-chain-analysis)
3. [Changelog: Pinned Commit to Master](#changelog-pinned-commit-to-master)
4. [PROGMEM Performance Regression](#progmem-performance-regression)
5. [Gamma LUT Implementation](#gamma-lut-implementation)
6. [Platform-Specific Behavior](#platform-specific-behavior)
7. [Available Knobs and Parameters](#available-knobs-and-parameters)
8. [Workarounds](#workarounds)
9. [Raw Git Data](#raw-git-data)

---

## Function Overview

### Location
- **Header:** `src/fl/five_bit_hd_gamma.h`
- **Namespace:** `fl`

### Signature
```cpp
void five_bit_hd_gamma_bitshift(
    CRGB colors,           // Input: 8-bit RGB pixel
    CRGB colors_scale,     // Input: Per-channel color correction (0-255 each)
    fl::u8 global_brightness,  // Input: Master brightness (0-255)
    CRGB *out_colors,      // Output: Gamma-corrected 8-bit RGB
    fl::u8 *out_power_5bit // Output: 5-bit global brightness (0-31)
);
```

### Purpose
Converts 8-bit RGB values to gamma-corrected output suitable for LED chipsets with 5-bit global brightness registers (APA102, SK9822, HD107, HD108, etc.). The function:
1. Applies gamma 2.8 correction (8-bit → 16-bit)
2. Applies per-channel color scaling
3. Decomposes the result into 8-bit RGB + 5-bit brightness

---

## Call Chain Analysis

### Complete Call Graph
```
five_bit_hd_gamma_bitshift(CRGB, CRGB, u8, CRGB*, u8*)
│
├── [Early exit if global_brightness == 0]
│
├── five_bit_hd_gamma_function(CRGB, u16*, u16*, u16*)  [inline]
│   └── gamma16(CRGB, u16*, u16*, u16*)  [inline, in gamma.h]
│       └── gamma_2_8(u8) × 3  [in ease.cpp.hpp]
│           └── FL_PGM_READ_WORD_NEAR(&_gamma_2_8[value])
│               └── [PLATFORM DEPENDENT - see below]
│
├── scale16by8(u16, u8) × 3  [inline, only if scale != 0xFF]
│   └── (i * (scale + 1)) >> 8
│
└── five_bit_bitshift(u16, u16, u16, u8, CRGB*, u8*)  [inline]
    ├── scale16by8(u16, u8) × 3  [only if brightness != 0xFF]
    ├── max3() lambda  [2 comparisons]
    ├── bright_scale[32] LUT access  [static, in RAM]
    └── (r16 * scalef + 0x808000) >> 24 × 3
```

### FL_PGM_READ_WORD_NEAR Platform Behavior

**On AVR (FASTLED_USE_PROGMEM=1):**
```cpp
#define FL_PGM_READ_WORD_NEAR(x) (pgm_read_word_near(x))
// Direct flash read instruction
```

**On ESP32, ESP8266, ARM, WASM, and other platforms (FASTLED_USE_PROGMEM=0):**
```cpp
// Defined in src/platforms/null_progmem.h
#define FL_PGM_READ_WORD_NEAR(x) (fl_pgm_read_word_near_safe(x))

static inline fl::u16 fl_pgm_read_word_near_safe(const void* addr) {
    return fl_progmem_safe_read<fl::u16>(addr);
}

template<typename T>
static inline T fl_progmem_safe_read(const void* addr) {
    T result;
    fl::memcpy(&result, addr, sizeof(T));  // ← Function call
    return result;
}
```

**fl::memcpy location:** `src/fl/stl/cstring.cpp.hpp` (separate translation unit)
```cpp
void* memcpy(void* dest, const void* src, size_t n) noexcept {
    return ::memcpy(dest, src, n);
}
```

---

## Changelog: Pinned Commit to Master

### Changes to `src/fl/five_bit_hd_gamma.h`

| Date | Commit | Description | Impact |
|------|--------|-------------|--------|
| 2026-01-27 | `6c3c36ed0b859b40eaa9f331b1a2ef3ad0cdeeed` | Override mechanism removed | `FASTLED_FIVE_BIT_HD_BITSHIFT_FUNCTION_OVERRIDE` and `FASTLED_FIVE_BIT_HD_GAMMA_FUNCTION_OVERRIDE` no longer functional |
| 2026-01-23 | `f5bbf8d988afa73739556846814a1a12ecb8e5c7` | Unity build for fl | `gamma.cpp.hpp` stub added |
| 2026-01-16 | `e6f5a0bf90e19650460474b19f53d150fc3cdbba` | Removed unused namespace declarations | No functional change |
| 2025-12-17 | `1f1d41f5ffb9ca06c21f1a49ee9def76544fd899` | Added `FL_NO_INLINE_IF_AVR` attribute | Prevents inlining on AVR to avoid register exhaustion |
| 2025-12-16 | `8b547a06bab3b08ad992eafbd3b148494ac3be56` | Include path changes | `ftl/math.h` → `fl/stl/math.h` |

### Changes to Dependencies

| File | Changes Since Pinned Commit |
|------|----------------------------|
| `src/fl/gamma.h` | Include path change only (`ftl/stdint.h` → `fl/stl/stdint.h`) |
| `src/fl/ease.h` | No functional changes |
| `src/fl/ease.cpp.hpp` | No changes to `gamma_2_8()` or LUT |
| `src/platforms/null_progmem.h` | Include path change only (`ftl/cstring.h` → `fl/stl/cstring.h`) |
| `src/platforms/shared/scale8.h` | No changes |
| `src/lib8tion/scale8.h` | No changes |

### Algorithm Stability

The core algorithm in `five_bit_bitshift()` (the "closed-form solution" with `bright_scale[32]` LUT) has not changed since:
- **Commit:** `5b0bc2d1ac42c169d6b11c2ede0a0b2d496ffc0c`
- **Date:** 2025-07-19
- **Message:** "fixes https://github.com/FastLED/FastLED/issues/1968"

The pinned commit (2025-11-27) already contains this algorithm.

---

## PROGMEM Performance Regression

### Timeline

| Date | Commit | State |
|------|--------|-------|
| Pre-2025-06-30 | Before `1506304cac2fadd630ab51172308afca64abcaf8` | Direct pointer cast |
| 2025-06-30 | `1506304cac2fadd630ab51172308afca64abcaf8` | memcpy chain introduced |
| 2025-11-27 | `ac595965af72db9c6f41e63e878bb564a66299fe` (pinned) | memcpy chain present |
| 2026-02-01 | Current HEAD | memcpy chain still present |

### Before (Pre-2025-06-30)
```cpp
// src/platforms/null_progmem.h
#define FL_PGM_READ_BYTE_NEAR(x) (*((const uint8_t *)(x)))
#define FL_PGM_READ_WORD_NEAR(x) (*((const uint16_t *)(x)))
#define FL_PGM_READ_DWORD_NEAR(x) (*((const uint32_t *)(x)))
```

### After (2025-06-30 to Present)
```cpp
// src/platforms/null_progmem.h
template<typename T>
static inline T fl_progmem_safe_read(const void* addr) {
    T result;
    fl::memcpy(&result, addr, sizeof(T));
    return result;
}

#define FL_PGM_READ_WORD_NEAR(x) (fl_pgm_read_word_near_safe(x))
```

### Reason for Change
Commit message: "Add safe memory access macros for PROGMEM variables"

The direct pointer cast `*((const uint16_t *)(x))` violates C++ strict aliasing rules. The memcpy approach is technically correct per the C++ standard and avoids potential undefined behavior with aggressive compiler optimizations.

### Performance Implications
- **Direct cast:** Single load instruction (~1-3 cycles)
- **memcpy chain:** Function call to `fl::memcpy` (in separate TU) → `::memcpy`
- **Per-LED overhead:** 3 gamma lookups × function call overhead
- **Mitigation:** LTO (Link-Time Optimization) may inline the calls if enabled

---

## Gamma LUT Implementation

### Location
`src/fl/ease.cpp.hpp`

### LUT Definition
```cpp
const u16 _gamma_2_8[256] FL_PROGMEM = {
    0,     0,     0,     1,     1,     2,     4,     6,     8,     11,
    14,    18,    23,    29,    35,    41,    49,    57,    67,    77,
    // ... 256 entries total
    62246, 62896, 63549, 64207, 64869, 65535
};

u16 gamma_2_8(u8 value) {
    return FL_PGM_READ_WORD_NEAR(&_gamma_2_8[value]);
}
```

### Characteristics
- **Input range:** 0-255 (8-bit)
- **Output range:** 0-65535 (16-bit)
- **Gamma exponent:** 2.8 (hardcoded)
- **Storage:** 512 bytes (256 × 2 bytes)
- **Location:** PROGMEM on AVR, RAM on other platforms

### Not Configurable
- Gamma exponent cannot be changed without modifying source
- No toe/shoulder controls
- No black level offset

---

## Platform-Specific Behavior

### FASTLED_USE_PROGMEM Settings

| Platform | FASTLED_USE_PROGMEM | PROGMEM Access Method |
|----------|--------------------|-----------------------|
| AVR | 1 | `pgm_read_word_near()` (direct flash read) |
| ESP32 | 0 | `fl::memcpy` chain |
| ESP8266 | 0 | `fl::memcpy` chain |
| STM32 | 0 | `fl::memcpy` chain |
| RP2040 | 0 | `fl::memcpy` chain |
| SAMD21/51 | 0 | `fl::memcpy` chain |
| Teensy 3.x | 1 | `pgm_read_word_near()` |
| Teensy 4.x | 1 | `pgm_read_word_near()` |
| nRF52 | 0 | `fl::memcpy` chain |
| WASM | 0 | `fl::memcpy` chain |

Source: `src/platforms/*/led_sysdefs_*.h` files

---

## Available Knobs and Parameters

### Function Parameters

| Parameter | Type | Range | Effect |
|-----------|------|-------|--------|
| `colors` | CRGB | 0-255 per channel | Input pixel |
| `colors_scale` | CRGB | 0-255 per channel | Per-channel multiplier (255 = no change) |
| `global_brightness` | u8 | 0-255 | Master brightness |

### Compile-Time Options (Pinned Commit Only)

These options exist in the pinned commit but were **removed** in commit `6c3c36ed0b` (2026-01-27):

| Define | Effect |
|--------|--------|
| `FASTLED_FIVE_BIT_HD_BITSHIFT_FUNCTION_OVERRIDE` | Allows replacing `five_bit_hd_gamma_bitshift()` |
| `FASTLED_FIVE_BIT_HD_GAMMA_FUNCTION_OVERRIDE` | Allows replacing `five_bit_hd_gamma_function()` |

### What Cannot Be Configured
- Gamma exponent (fixed at 2.8)
- Black level / toe
- Quantization levels (fixed at 32 for 5-bit output)
- bright_scale LUT values

---

## Workarounds

### Workaround 1: Override PROGMEM Macros

Define before including FastLED to restore direct pointer access:

```cpp
#define FL_PGM_READ_BYTE_NEAR(x) (*((const uint8_t *)(x)))
#define FL_PGM_READ_WORD_NEAR(x) (*((const uint16_t *)(x)))
#define FL_PGM_READ_DWORD_NEAR(x) (*((const uint32_t *)(x)))

#include <FastLED.h>
```

**Tradeoff:** Violates C++ strict aliasing rules. May cause undefined behavior with aggressive optimizations. Works in practice on most embedded compilers.

### Workaround 2: Bypass Gamma LUT

Call `five_bit_bitshift()` directly with pre-computed 16-bit values:

```cpp
#include "fl/five_bit_hd_gamma.h"

// You provide gamma-corrected 16-bit values
fl::u16 r16 = /* your gamma-corrected red */;
fl::u16 g16 = /* your gamma-corrected green */;
fl::u16 b16 = /* your gamma-corrected blue */;

CRGB out;
fl::u8 brightness_5bit;

fl::five_bit_bitshift(r16, g16, b16, 255, &out, &brightness_5bit);
```

### Workaround 3: Pin to Pre-Regression Commit

Pin to before `1506304cac2fadd630ab51172308afca64abcaf8` (2025-06-30) for direct pointer access.

**Tradeoff:** Loses the closed-form brightness algorithm added in `5b0bc2d1ac` (2025-07-19) which fixes quantization issues.

### (Failed) Workaround 4: Enable LTO

Enable Link-Time Optimization in your build. This may allow the compiler to inline `fl::memcpy` calls across translation units.  We did testing and this doesn't actually work as some of the framework libaries don't support lto.

**PlatformIO example:**

```ini
build_flags = -flto
```

---

## Raw Git Data

### Key Commits

```
# Closed-form solution added (already in pinned commit)
5b0bc2d1ac42c169d6b11c2ede0a0b2d496ffc0c 2025-07-19 fixes https://github.com/FastLED/FastLED/issues/1968

# PROGMEM memcpy regression (already in pinned commit)
1506304cac2fadd630ab51172308afca64abcaf8 2025-06-30 Add safe memory access macros for PROGMEM variables

# Pinned commit
ac595965af72db9c6f41e63e878bb564a66299fe 2025-11-27 fix(wasm): add main-thread video recording with worker frame transfer

# Override mechanism removed (after pinned commit)
6c3c36ed0b859b40eaa9f331b1a2ef3ad0cdeeed 2026-01-27 five_bit had gamma bitshift is no longe roverridable by the user

# AVR noinline added (after pinned commit)
1f1d41f5ffb9ca06c21f1a49ee9def76544fd899 2025-12-17 feat(encoder): add noinline attribute for AVR to prevent register exhaustion
```

### Files Analyzed

```
src/fl/five_bit_hd_gamma.h
src/fl/gamma.h
src/fl/ease.h
src/fl/ease.cpp.hpp
src/platforms/null_progmem.h
src/platforms/shared/scale8.h
src/lib8tion/scale8.h
src/fl/stl/cstring.h
src/fl/stl/cstring.cpp.hpp
src/fastled_progmem.h
src/fl/chipsets/apa102.h
```

### Diff: Pinned Commit to HEAD for five_bit_hd_gamma.h

```diff
- Include path: ftl/math.h → fl/stl/math.h
- Added: #include "fl/compiler_control.h"
- Removed: FASTLED_FIVE_BIT_HD_BITSHIFT_FUNCTION_OVERRIDE mechanism (~30 lines)
- Removed: FASTLED_FIVE_BIT_HD_GAMMA_FUNCTION_OVERRIDE mechanism (~15 lines)
- Removed: internal_builtin_five_bit_hd_gamma_bitshift() indirection
- Added: FL_NO_INLINE_IF_AVR attribute to five_bit_hd_gamma_bitshift()
- Added: FL_NO_INLINE_IF_AVR attribute to five_bit_bitshift()
```

---

## Document Metadata

- **Generated by:** Claude Code investigation
- **FastLED version range:** `ac595965af` (2025-11-27) to `8cd217f137` (2026-01-31)
- **Investigation scope:** `five_bit_hd_gamma_bitshift` function and direct dependencies
