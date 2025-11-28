# Effect Refactoring Analysis

**Date:** 2025-11-27
**Context:** Analysis of POV display effects codebase for repeated patterns and optimization opportunities
**Performance Goal:** Reduce VirtualBlobs render time from 651-749 μs to ~100-150 μs

## Executive Summary

This analysis identifies critical refactoring opportunities across 4 effect implementations (VirtualBlobs, PerArmBlobs, SolidArms, RpmArc). Despite only 30 LEDs total, performance is terrible due to:

1. **Massive code duplication** - Same constants, loops, and functions repeated 4+ times
2. **Inefficient angle checking** - `isAngleInArc()` called 150 times per frame (258 μs / 39.6% of total time)
3. **Replicated arm-rendering loops** - Same 3-arm iteration pattern copy-pasted everywhere
4. **Missing pre-computation** - RenderContext could cache expensive calculations

**Top 3 Critical Findings:**

1. **isAngleInArc() is the primary bottleneck** (258 μs, 39.6% of execution) - called 150x per frame with redundant wraparound logic
2. **Hardware constants duplicated 4 times** - Every effect redefines LEDS_PER_ARM, INNER_ARM_START, etc.
3. **Three-arm rendering pattern copy-pasted** - Same loop structure repeated in PerArmBlobs, VirtualBlobs, SolidArms, RpmArc

---

## 1. Repeated Patterns (Found 3+ Times)

### 1.1 Hardware Configuration Constants

**Pattern:** Every effect file redefines identical hardware layout constants

**Occurrences:**
- `/Users/coryking/projects/POV_Project/src/effects/VirtualBlobs.cpp:13-16`
- `/Users/coryking/projects/POV_Project/src/effects/PerArmBlobs.cpp:12-15`
- `/Users/coryking/projects/POV_Project/src/effects/SolidArms.cpp:10-13`
- `/Users/coryking/projects/POV_Project/src/effects/RpmArc.cpp:12-15`
- `/Users/coryking/projects/POV_Project/src/main.cpp:20-34` (original definitions)

**Current Implementation:**
```cpp
// Repeated in EVERY effect file:
constexpr uint16_t LEDS_PER_ARM = 10;
constexpr uint16_t INNER_ARM_START = 10;
constexpr uint16_t MIDDLE_ARM_START = 0;
constexpr uint16_t OUTER_ARM_START = 20;
```

**Proposed Solution:** Extract to shared header
```cpp
// include/hardware_config.h
#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <cstdint>

namespace HardwareConfig {
    constexpr uint16_t LEDS_PER_ARM = 10;
    constexpr uint16_t NUM_ARMS = 3;
    constexpr uint16_t TOTAL_LEDS = 30;

    constexpr uint16_t INNER_ARM_START = 10;
    constexpr uint16_t MIDDLE_ARM_START = 0;
    constexpr uint16_t OUTER_ARM_START = 20;

    // Arm starts as array for iteration
    constexpr uint16_t ARM_STARTS[NUM_ARMS] = {
        INNER_ARM_START,   // Arm 0
        MIDDLE_ARM_START,  // Arm 1
        OUTER_ARM_START    // Arm 2
    };
}

#endif
```

**Performance Impact:** Zero (compile-time constants), but eliminates maintenance burden

---

### 1.2 Angle-in-Arc Checking

**Pattern:** Two nearly identical angle wraparound functions with same logic

**Occurrences:**
- `/Users/coryking/projects/POV_Project/src/blob_types.h:97-108` (`isAngleInArc` for Blob)
- `/Users/coryking/projects/POV_Project/src/effects/RpmArc.cpp:73-93` (`isAngleInRpmArc`)
- **Called 150 times per frame in VirtualBlobs** (30 LEDs × 5 blobs per arm, 3 arms)
- **Timing: 258 μs / 39.6% of total execution time**

**Current Implementation (Blob version):**
```cpp
inline bool isAngleInArc(double angle, const Blob& blob) {
    if (!blob.active) return false;

    double arcEnd = blob.currentStartAngle + blob.currentArcSize;

    // Handle wraparound (e.g., arc from 350° to 10°)
    if (arcEnd > 360.0f) {
        return (angle >= blob.currentStartAngle) || (angle < fmod(arcEnd, 360.0f));
    }

    return (angle >= blob.currentStartAngle) && (angle < arcEnd);
}
```

**Current Implementation (RpmArc version):**
```cpp
static bool isAngleInRpmArc(double angle) {
    double halfWidth = ARC_WIDTH_DEGREES / 2.0;
    double arcStart = ARC_CENTER_DEGREES - halfWidth;
    double arcEnd = ARC_CENTER_DEGREES + halfWidth;

    // Normalize angle to 0-360
    angle = fmod(angle, 360.0);
    if (angle < 0) angle += 360.0;

    // Handle wraparound
    if (arcStart < 0) {
        return (angle >= (arcStart + 360.0)) || (angle < arcEnd);
    } else if (arcEnd > 360.0) {
        return (angle >= arcStart) || (angle < (arcEnd - 360.0));
    } else {
        return (angle >= arcStart) && (angle < arcEnd);
    }
}
```

**Problems:**
1. **Redundant normalization** - Angles are already normalized in RenderContext (from RevolutionTimer)
2. **Expensive fmod()** - Called in hot path, costs ~0.5-1 μs per call
3. **Active check inside function** - Wastes cycles checking `blob.active` 150 times when caller already knows
4. **Double-precision math** - Uses `double` when `float` would suffice

**Proposed Refactored Helper:**
```cpp
// include/angle_utils.h
#ifndef ANGLE_UTILS_H
#define ANGLE_UTILS_H

namespace AngleUtils {
    /**
     * Check if normalized angle (0-360) is within arc range
     * PRECONDITION: angle must be in [0, 360) range (caller normalizes)
     *
     * @param angle Angle to test (0-360°, already normalized)
     * @param arcStart Start of arc (0-360°)
     * @param arcEnd End of arc (may be > 360° for wraparound)
     * @return true if angle is within arc (handles 360° wraparound)
     */
    inline bool isAngleInArc(float angle, float arcStart, float arcEnd) {
        // Assume angle is already in [0, 360) - no normalization needed

        // Handle wraparound (e.g., arc from 350° to 10° means arcEnd = 370°)
        if (arcEnd > 360.0f) {
            return (angle >= arcStart) || (angle < (arcEnd - 360.0f));
        }

        // Normal case: no wraparound
        return (angle >= arcStart) && (angle < arcEnd);
    }

    /**
     * Blob-specific wrapper (calls should be eliminated in favor of batch checks)
     */
    inline bool isAngleInArc(float angle, const struct Blob& blob) {
        // Caller should check blob.active before calling
        float arcEnd = blob.currentStartAngle + blob.currentArcSize;
        return isAngleInArc(angle, blob.currentStartAngle, arcEnd);
    }
}

#endif
```

**Performance Impact:**
- **Current:** 258 μs for 150 calls = 1.72 μs/call
- **Target:** ~0.2-0.5 μs/call (10-15 μs total for 150 calls)
- **Speedup:** 15-20x reduction (~240 μs savings)
- **Key optimizations:**
  - Eliminate `fmod()` by trusting pre-normalized input
  - Use `float` instead of `double`
  - Move `blob.active` check to caller (batch early-exit)

---

### 1.3 Color Blending (RGB Addition)

**Pattern:** Additive RGB color blending with saturation clamping

**Occurrences:**
- `/Users/coryking/projects/POV_Project/src/effects/VirtualBlobs.cpp:175-177` (3 times - inner/middle/outer arms)
- `/Users/coryking/projects/POV_Project/src/effects/PerArmBlobs.cpp:110-112` (3 times - inner/middle/outer arms)

**Current Implementation:**
```cpp
// Repeated 6 times across 2 files:
ledColor.R = min(255, ledColor.R + blobs[i].color.R);
ledColor.G = min(255, ledColor.G + blobs[i].color.G);
ledColor.B = min(255, ledColor.B + blobs[i].color.B);
```

**Proposed Helper:**
```cpp
// include/color_utils.h
#ifndef COLOR_UTILS_H
#define COLOR_UTILS_H

#include <NeoPixelBus.h>

namespace ColorUtils {
    /**
     * Additive blend with saturation (used for overlapping blobs)
     * Clamps each channel to 255 to prevent overflow
     */
    inline void blendAdditive(RgbColor& dst, const RgbColor& src) {
        dst.R = min(255, dst.R + src.R);
        dst.G = min(255, dst.G + src.G);
        dst.B = min(255, dst.B + src.B);
    }

    /**
     * Faster version using saturating arithmetic (if ESP32 supports)
     * TODO: Profile to verify if this is actually faster
     */
    inline void blendAdditiveFast(RgbColor& dst, const RgbColor& src) {
        // Could use __USADD8 intrinsic on ARM Cortex-M4+
        // For now, same as above until profiled
        blendAdditive(dst, src);
    }
}

#endif
```

**Performance Impact:** Minimal (color blending measured at 0 μs in profiling), but improves readability

---

### 1.4 Three-Arm Rendering Loop Pattern

**Pattern:** All effects iterate over 3 arms with nearly identical loop structure

**Occurrences:**
- `/Users/coryking/projects/POV_Project/src/effects/VirtualBlobs.cpp:131-329` (3 copy-pasted loops)
- `/Users/coryking/projects/POV_Project/src/effects/PerArmBlobs.cpp:101-153` (3 copy-pasted loops)
- `/Users/coryking/projects/POV_Project/src/effects/SolidArms.cpp:134-140` (lambda called 3 times)
- `/Users/coryking/projects/POV_Project/src/effects/RpmArc.cpp:129-131` (lambda called 3 times)

**Current Implementation (VirtualBlobs - worst offender):**
```cpp
// Inner arm: LEDs 10-19
for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
    uint16_t physicalLed = INNER_ARM_START + ledIdx;
    uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];
    RgbColor ledColor(0, 0, 0);

    // Check ALL blobs (no arm filtering)
    for (int i = 0; i < MAX_BLOBS; i++) {
        bool angleInArc = blobs[i].active && isAngleInArc(ctx.innerArmDegrees, blobs[i]);
        if (angleInArc) {
            bool radialMatch = isVirtualLedInBlob(virtualPos, blobs[i]);
            if (radialMatch) {
                ledColor.R = min(255, ledColor.R + blobs[i].color.R);
                ledColor.G = min(255, ledColor.G + blobs[i].color.G);
                ledColor.B = min(255, ledColor.B + blobs[i].color.B);
            }
        }
    }
    strip.SetPixelColor(physicalLed, ledColor);
}

// Middle arm: LEDs 0-9
[IDENTICAL COPY with s/INNER/MIDDLE/g and s/innerArmDegrees/middleArmDegrees/g]

// Outer arm: LEDs 20-29
[IDENTICAL COPY with s/INNER/OUTER/g and s/innerArmDegrees/outerArmDegrees/g]
```

**Problem:** 100 lines of code that are 95% identical

**Proposed Helper (VirtualBlobs-specific):**
```cpp
// Helper in VirtualBlobs.cpp (not generic - effect-specific)
static void renderArmVirtualBlobs(
    const RenderContext& ctx,
    float armAngle,
    uint16_t armStart,
    const Blob blobs[MAX_BLOBS],
    const uint8_t PHYSICAL_TO_VIRTUAL[30])
{
    for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
        uint16_t physicalLed = armStart + ledIdx;
        uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];
        RgbColor ledColor(0, 0, 0);

        for (int i = 0; i < MAX_BLOBS; i++) {
            if (blobs[i].active && isAngleInArc(armAngle, blobs[i])) {
                if (isVirtualLedInBlob(virtualPos, blobs[i])) {
                    ColorUtils::blendAdditive(ledColor, blobs[i].color);
                }
            }
        }
        strip.SetPixelColor(physicalLed, ledColor);
    }
}

// Usage in renderVirtualBlobs():
void renderVirtualBlobs(const RenderContext& ctx) {
    renderArmVirtualBlobs(ctx, ctx.innerArmDegrees, INNER_ARM_START, blobs, PHYSICAL_TO_VIRTUAL);
    renderArmVirtualBlobs(ctx, ctx.middleArmDegrees, MIDDLE_ARM_START, blobs, PHYSICAL_TO_VIRTUAL);
    renderArmVirtualBlobs(ctx, ctx.outerArmDegrees, OUTER_ARM_START, blobs, PHYSICAL_TO_VIRTUAL);
}
```

**Performance Impact:**
- Likely neutral or slightly slower (function call overhead)
- But MUCH cleaner code (300+ lines → 50 lines)
- Easier to optimize in one place

**Better Alternative (generic arm iteration):**
```cpp
// include/arm_utils.h
namespace ArmUtils {
    /**
     * Iterate over all 3 arms with callback
     *
     * Example usage:
     *   forEachArm(ctx, [&](uint8_t armIdx, float angle, uint16_t armStart) {
     *       renderMyEffect(angle, armStart);
     *   });
     */
    template<typename Func>
    void forEachArm(const RenderContext& ctx, Func&& callback) {
        callback(0, ctx.innerArmDegrees, HardwareConfig::INNER_ARM_START);
        callback(1, ctx.middleArmDegrees, HardwareConfig::MIDDLE_ARM_START);
        callback(2, ctx.outerArmDegrees, HardwareConfig::OUTER_ARM_START);
    }
}
```

---

### 1.5 PHYSICAL_TO_VIRTUAL Lookup

**Pattern:** Array lookup to convert physical LED index to virtual radial position

**Occurrences:**
- `/Users/coryking/projects/POV_Project/src/effects/VirtualBlobs.cpp:137` (inner arm)
- `/Users/coryking/projects/POV_Project/src/effects/VirtualBlobs.cpp:206` (middle arm)
- `/Users/coryking/projects/POV_Project/src/effects/VirtualBlobs.cpp:274` (outer arm)
- `/Users/coryking/projects/POV_Project/src/effects/RpmArc.cpp:110`

**Current Implementation:**
```cpp
uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];
```

**Timing:** 32 μs for 30 calls = 1.07 μs/call (includes loop overhead)

**Problem:** This is actually quite fast. The issue is it's inside a tight loop where it's recalculated every frame.

**Proposed Optimization:** Pre-compute in RenderContext (see Section 2.3 below)

**Performance Impact:** Minimal (~20 μs savings if pre-computed), but cleaner API

---

## 2. RenderContext Enhancement Opportunities

### 2.1 Current RenderContext Contents

**File:** `/Users/coryking/projects/POV_Project/include/RenderContext.h`

```cpp
struct RenderContext {
    unsigned long currentMicros;        // Current time in microseconds
    float innerArmDegrees;              // Inner arm position in degrees (0-360)
    float middleArmDegrees;             // Middle arm position in degrees (0-360)
    float outerArmDegrees;              // Outer arm position in degrees (0-360)
    unsigned long microsecondsPerRev;   // Time per revolution in microseconds
};
```

**Usage Pattern:** Effects use `ctx.innerArmDegrees` etc. directly in hot loops

---

### 2.2 Pre-Computed Blob Arc Boundaries

**Current Problem:** `isAngleInArc()` recomputes `arcEnd = blob.currentStartAngle + blob.currentArcSize` 150 times per frame

**Proposed Enhancement:**
```cpp
struct BlobArcCache {
    bool active;
    float arcStart;  // blob.currentStartAngle
    float arcEnd;    // blob.currentStartAngle + blob.currentArcSize
};

struct RenderContext {
    // ... existing fields ...

    // Pre-computed blob arc boundaries (updated once per frame)
    BlobArcCache blobArcs[MAX_BLOBS];
};
```

**Population (in main loop before render):**
```cpp
// In main.cpp, after updateBlob() calls:
for (int i = 0; i < MAX_BLOBS; i++) {
    ctx.blobArcs[i].active = blobs[i].active;
    ctx.blobArcs[i].arcStart = blobs[i].currentStartAngle;
    ctx.blobArcs[i].arcEnd = blobs[i].currentStartAngle + blobs[i].currentArcSize;
}
```

**Performance Benefit:** Eliminates 150 floating-point additions per frame (~5-10 μs savings)

---

### 2.3 Pre-Computed Blob Angle Matches Per Arm

**Current Problem:** Each arm checks all 5 blobs' angles independently, even though arm angles don't change within a frame

**Proposed Enhancement:**
```cpp
struct RenderContext {
    // ... existing fields ...

    // Pre-computed blob visibility per arm (150 checks → 15 checks)
    bool blobVisibleOnInnerArm[MAX_BLOBS];   // isAngleInArc(innerArmDegrees, blob[i])
    bool blobVisibleOnMiddleArm[MAX_BLOBS];  // isAngleInArc(middleArmDegrees, blob[i])
    bool blobVisibleOnOuterArm[MAX_BLOBS];   // isAngleInArc(outerArmDegrees, blob[i])
};
```

**Population (in main loop):**
```cpp
// Pre-compute blob visibility for each arm (15 angle checks instead of 150)
for (int i = 0; i < MAX_BLOBS; i++) {
    if (blobs[i].active) {
        float arcEnd = blobs[i].currentStartAngle + blobs[i].currentArcSize;
        ctx.blobVisibleOnInnerArm[i] = AngleUtils::isAngleInArc(ctx.innerArmDegrees, blobs[i].currentStartAngle, arcEnd);
        ctx.blobVisibleOnMiddleArm[i] = AngleUtils::isAngleInArc(ctx.middleArmDegrees, blobs[i].currentStartAngle, arcEnd);
        ctx.blobVisibleOnOuterArm[i] = AngleUtils::isAngleInArc(ctx.outerArmDegrees, blobs[i].currentStartAngle, arcEnd);
    } else {
        ctx.blobVisibleOnInnerArm[i] = false;
        ctx.blobVisibleOnMiddleArm[i] = false;
        ctx.blobVisibleOnOuterArm[i] = false;
    }
}
```

**Updated VirtualBlobs rendering:**
```cpp
// Inner arm
for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
    uint16_t physicalLed = INNER_ARM_START + ledIdx;
    uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];
    RgbColor ledColor(0, 0, 0);

    for (int i = 0; i < MAX_BLOBS; i++) {
        if (ctx.blobVisibleOnInnerArm[i]) {  // CACHED - no angle check!
            if (isVirtualLedInBlob(virtualPos, blobs[i])) {
                ColorUtils::blendAdditive(ledColor, blobs[i].color);
            }
        }
    }
    strip.SetPixelColor(physicalLed, ledColor);
}
```

**Performance Benefit:**
- **Before:** 150 angle checks @ 1.72 μs/call = 258 μs
- **After:** 15 angle checks @ 0.5 μs/call = 7.5 μs (pre-computation cost), 150 array lookups @ ~0.02 μs = 3 μs
- **Savings:** ~247 μs (95% reduction!)

**Trade-off:** 15 bytes added to RenderContext (negligible)

---

### 2.4 RPM Calculation

**Current:** RpmArc recalculates RPM from `microsecondsPerRev` every frame

**File:** `/Users/coryking/projects/POV_Project/src/effects/RpmArc.cpp:29-32`
```cpp
static float calculateRPM(interval_t microsecondsPerRev) {
    if (microsecondsPerRev == 0) return 0.0f;
    return 60000000.0f / static_cast<float>(microsecondsPerRev);
}
```

**Proposed Enhancement:**
```cpp
struct RenderContext {
    // ... existing fields ...

    float currentRPM;  // Pre-calculated 60000000.0 / microsecondsPerRev
};
```

**Performance Benefit:** Eliminates 1 division per frame (~0.5 μs savings - minimal but cleaner)

---

### 2.5 Virtual Position Cache (Per-Arm LED Arrays)

**Current Problem:** Effects look up `PHYSICAL_TO_VIRTUAL[physicalLed]` inside tight loops

**Proposed Enhancement:**
```cpp
struct RenderContext {
    // ... existing fields ...

    // Pre-computed virtual positions for each arm's LEDs (cache-friendly)
    uint8_t innerArmVirtualPos[10];   // PHYSICAL_TO_VIRTUAL[10..19]
    uint8_t middleArmVirtualPos[10];  // PHYSICAL_TO_VIRTUAL[0..9]
    uint8_t outerArmVirtualPos[10];   // PHYSICAL_TO_VIRTUAL[20..29]
};
```

**Population:**
```cpp
// One-time initialization (static data, could be compile-time constant)
for (int i = 0; i < 10; i++) {
    ctx.innerArmVirtualPos[i] = PHYSICAL_TO_VIRTUAL[INNER_ARM_START + i];
    ctx.middleArmVirtualPos[i] = PHYSICAL_TO_VIRTUAL[MIDDLE_ARM_START + i];
    ctx.outerArmVirtualPos[i] = PHYSICAL_TO_VIRTUAL[OUTER_ARM_START + i];
}
```

**Usage:**
```cpp
// Before:
uint16_t physicalLed = INNER_ARM_START + ledIdx;
uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];

// After:
uint8_t virtualPos = ctx.innerArmVirtualPos[ledIdx];
```

**Performance Benefit:**
- Better cache locality (sequential access instead of strided)
- Eliminates addition `INNER_ARM_START + ledIdx`
- Estimated ~15-20 μs savings

**Trade-off:** 30 bytes added to RenderContext (negligible)

---

## 3. Effect-Specific Optimizations

### 3.1 VirtualBlobs (Slowest - 651-749 μs)

**Current Bottlenecks (from profiling):**
1. **Angle checks:** 258 μs (39.6%)
2. **SetPixelColor:** 104-147 μs (16-20%)
3. **Array lookups:** 32 μs (4.9%)
4. **RgbColor construction:** 23 μs (3.5%)

**Optimization Strategy:**

#### 3.1.1 Pre-Compute Angle Checks (Section 2.3)
- **Impact:** 258 μs → ~10 μs (247 μs savings)
- **Complexity:** Low (add fields to RenderContext)

#### 3.1.2 Batch SetPixelColor Calls
**Current:** 30 individual `strip.SetPixelColor(physicalLed, ledColor)` calls

**Problem:** Each call has function overhead + bounds checking

**Proposed:** Direct buffer access
```cpp
// Access NeoPixelBusLg internal buffer directly
RgbColor* pixels = strip.Pixels();  // Get raw buffer

// Inner arm
for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
    uint16_t physicalLed = INNER_ARM_START + ledIdx;
    uint8_t virtualPos = ctx.innerArmVirtualPos[ledIdx];
    RgbColor ledColor(0, 0, 0);

    for (int i = 0; i < MAX_BLOBS; i++) {
        if (ctx.blobVisibleOnInnerArm[i]) {
            if (isVirtualLedInBlob(virtualPos, blobs[i])) {
                ColorUtils::blendAdditive(ledColor, blobs[i].color);
            }
        }
    }
    pixels[physicalLed] = ledColor;  // Direct write - no function call
}
```

**Performance Impact:**
- **Before:** 104 μs for 30 calls = 3.47 μs/call
- **After:** ~30-50 μs total (1-1.5 μs per write)
- **Savings:** ~50-75 μs

**Risk:** Bypasses NeoPixelBusLg's abstraction. Verify `Pixels()` method exists.

#### 3.1.3 Eliminate RgbColor Construction Overhead
**Current:** `RgbColor ledColor(0, 0, 0);` called 30 times per frame (23 μs)

**Proposed:** Re-use static constant
```cpp
static const RgbColor BLACK(0, 0, 0);

// In loop:
RgbColor ledColor = BLACK;  // Copy assignment (faster than constructor)
```

**Performance Impact:** ~10-15 μs savings

#### 3.1.4 Summary - VirtualBlobs Optimizations
| Optimization | Savings | Complexity |
|--------------|---------|------------|
| Pre-compute angle checks | 247 μs | Low |
| Direct buffer writes | 50-75 μs | Medium |
| Eliminate RgbColor(0,0,0) | 10-15 μs | Trivial |
| Use virtual position cache | 15-20 μs | Low |
| **TOTAL** | **322-357 μs** | **Low-Medium** |

**Projected Performance:**
- **Current:** 651-749 μs
- **After optimizations:** 294-427 μs
- **Still not at goal (100-150 μs)** - need deeper algorithmic changes

---

### 3.2 PerArmBlobs (No Timing Data)

**Similar Issues to VirtualBlobs:**
1. Triple copy-pasted arm loops (lines 101-153)
2. Same `isAngleInArc()` calls in hot path
3. Same color blending pattern

**Optimization Strategy:** Apply same refactorings as VirtualBlobs (Sections 3.1.1 - 3.1.4)

**Estimated Performance:** Should see similar 50-60% speedup

---

### 3.3 SolidArms (No Timing Data)

**Current Structure:** Uses lambda function `renderArm()` called 3 times (good pattern!)

**No Major Issues Identified** - already uses local function to avoid duplication

**Minor Optimization:** Extract `OFF_COLOR` and `WHITE` to shared constants (low priority)

---

### 3.4 RpmArc (No Timing Data)

**Current Structure:** Uses lambda function `renderRpmArm()` called 3 times (good pattern!)

**Issues:**
1. Duplicate `isAngleInRpmArc()` function (merge with AngleUtils)
2. `PHYSICAL_TO_VIRTUAL` lookup in loop (use ctx cache)

**Optimization Strategy:**
```cpp
// Before:
uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];

// After:
uint8_t virtualPos = ctx.innerArmVirtualPos[ledIdx];
```

**Estimated Impact:** ~10-15 μs savings

---

## 4. Proposed Helper API

### 4.1 Core Utilities (High Priority)

```cpp
// include/hardware_config.h
namespace HardwareConfig {
    constexpr uint16_t LEDS_PER_ARM = 10;
    constexpr uint16_t NUM_ARMS = 3;
    constexpr uint16_t TOTAL_LEDS = 30;

    constexpr uint16_t INNER_ARM_START = 10;
    constexpr uint16_t MIDDLE_ARM_START = 0;
    constexpr uint16_t OUTER_ARM_START = 20;

    constexpr uint16_t ARM_STARTS[NUM_ARMS] = {
        INNER_ARM_START, MIDDLE_ARM_START, OUTER_ARM_START
    };
}

// include/angle_utils.h
namespace AngleUtils {
    // Fast angle-in-arc check (assumes angle already normalized to 0-360)
    inline bool isAngleInArc(float angle, float arcStart, float arcEnd);

    // Normalize angle to [0, 360) range (use sparingly - expensive)
    inline float normalizeAngle(float angle);
}

// include/color_utils.h
namespace ColorUtils {
    // Additive blend with saturation
    inline void blendAdditive(RgbColor& dst, const RgbColor& src);

    // Common colors
    static const RgbColor BLACK(0, 0, 0);
    static const RgbColor WHITE(255, 255, 255);
}

// include/arm_utils.h
namespace ArmUtils {
    // Iterate over all 3 arms with callback
    template<typename Func>
    void forEachArm(const RenderContext& ctx, Func&& callback);
}
```

---

### 4.2 Enhanced RenderContext (Critical for Performance)

```cpp
// include/RenderContext.h
struct RenderContext {
    // ===== Timing =====
    unsigned long currentMicros;
    unsigned long microsecondsPerRev;
    float currentRPM;  // NEW: Pre-calculated

    // ===== Arm Angles (normalized 0-360°) =====
    float innerArmDegrees;
    float middleArmDegrees;
    float outerArmDegrees;

    // ===== Blob Visibility Cache (NEW) =====
    // Pre-computed angle checks (150 checks → 15 checks)
    bool blobVisibleOnInnerArm[MAX_BLOBS];
    bool blobVisibleOnMiddleArm[MAX_BLOBS];
    bool blobVisibleOnOuterArm[MAX_BLOBS];

    // ===== Virtual Position Cache (NEW) =====
    // Pre-computed PHYSICAL_TO_VIRTUAL lookups per arm
    uint8_t innerArmVirtualPos[10];
    uint8_t middleArmVirtualPos[10];
    uint8_t outerArmVirtualPos[10];
};

// Population helper (called in main loop before render)
void populateRenderContext(
    RenderContext& ctx,
    unsigned long currentMicros,
    const RevolutionTimer& revTimer,
    const Blob blobs[MAX_BLOBS]);
```

**Memory Impact:**
- **Before:** 24 bytes (5 fields × ~4-8 bytes)
- **After:** ~69 bytes (original + 15 bools + 30 uint8_t)
- **Increase:** +45 bytes (negligible on ESP32-S3 with 512KB SRAM)

---

### 4.3 Effect Helper Functions (Medium Priority)

```cpp
// Helper for blob-based effects (VirtualBlobs, PerArmBlobs)
namespace BlobEffectUtils {
    /**
     * Render one arm for virtual blob effect
     * (Eliminates 200+ lines of duplicated code)
     */
    void renderArmVirtualBlobs(
        const RenderContext& ctx,
        uint8_t armIndex,  // 0=inner, 1=middle, 2=outer
        uint16_t armStart,
        const bool blobVisibleOnArm[MAX_BLOBS],
        const uint8_t armVirtualPos[10],
        const Blob blobs[MAX_BLOBS],
        NeoPixelBusLg<DotStarBgrFeature, DotStarSpi40MhzMethod>& strip);
}
```

---

## 5. Implementation Roadmap

### Phase 1: Low-Hanging Fruit (1-2 hours)
**Goal:** Eliminate duplicate constants, improve code maintainability

1. Create `include/hardware_config.h` with shared constants
2. Create `include/color_utils.h` with `blendAdditive()` helper
3. Update all 4 effects to use shared constants
4. Extract `isAngleInArc()` to `include/angle_utils.h`

**Expected Impact:** Zero performance change, cleaner codebase

---

### Phase 2: RenderContext Pre-Computation (2-3 hours)
**Goal:** Reduce VirtualBlobs from 651 μs to ~300-400 μs

1. Add blob visibility cache to RenderContext (Section 2.3)
2. Add virtual position cache to RenderContext (Section 2.5)
3. Add RPM field to RenderContext (Section 2.4)
4. Update VirtualBlobs to use cached values
5. Update PerArmBlobs to use cached values

**Expected Impact:**
- VirtualBlobs: 651 μs → 300-400 μs (40-50% reduction)
- PerArmBlobs: Similar speedup

---

### Phase 3: Direct Buffer Writes (1 hour)
**Goal:** Further reduce SetPixelColor overhead

1. Verify `NeoPixelBusLg::Pixels()` method exists
2. Replace `strip.SetPixelColor()` with direct buffer writes in VirtualBlobs
3. Test for correctness (compare visual output before/after)

**Expected Impact:** VirtualBlobs: 300-400 μs → 250-325 μs (additional 15-20% reduction)

---

### Phase 4: Algorithmic Refactoring (If Still Not Fast Enough)
**Goal:** Hit 100-150 μs target for VirtualBlobs

**Deeper Changes Required:**
1. **Spatial partitioning** - Only check blobs near current angle (octree/grid)
2. **Early-exit optimization** - Skip LEDs with no nearby blobs
3. **SIMD color blending** - Use ESP32-S3 vector instructions if available
4. **Loop unrolling** - Manually unroll LED iteration (compiler may not optimize)

**Complexity:** High - requires profiling to validate

---

## 6. Validation Plan

### 6.1 Performance Validation

**Metric:** Frame render time (μs) measured via `esp_timer_get_time()`

**Test Cases:**
1. VirtualBlobs at 2800 RPM (worst case - 7.14 μs per degree)
2. VirtualBlobs at 700 RPM (best case - 28.57 μs per degree)
3. PerArmBlobs at various RPMs
4. RpmArc at various RPMs

**Success Criteria:**
- VirtualBlobs: < 150 μs per frame (current: 651-749 μs)
- PerArmBlobs: < 100 μs per frame (no baseline)
- RpmArc: < 50 μs per frame (no baseline)

### 6.2 Correctness Validation

**Method:** Visual inspection on spinning display

**What to Check:**
1. Blobs appear at correct angles (no position drift)
2. Color blending matches original (no saturation/gamma changes)
3. No flickering or artifacts introduced

**Test Cases:**
1. Single blob (inner arm only)
2. All 5 blobs active
3. RPM sweep (700 → 2800 RPM)

---

## 7. Open Questions

### 7.1 Why Is SetPixelColor So Slow?

**Observation:** 104-147 μs for 30 calls = 3.5-4.9 μs per call

**Expected:** ~0.5-1 μs per call for simple buffer write

**Hypothesis:**
1. NeoPixelBusLg may be doing extra work (gamma correction? bounds checking?)
2. Function call overhead from extern declaration
3. Cache misses on pixel buffer

**Action:** Profile SetPixelColor internals to identify bottleneck

---

### 7.2 Is Timing Instrumentation Affecting Results?

**Observation:** "Timing instrumentation overhead: 300 μs (46.1%)"

**Concern:** Are `#ifdef ENABLE_DETAILED_TIMING` blocks skewing measurements?

**Action:**
1. Measure VirtualBlobs with timing disabled
2. Compare against baseline

---

### 7.3 Can We Hit 100 μs Without Algorithmic Changes?

**Current Best-Case Projection (All Phase 1-3 Optimizations):**
- VirtualBlobs: 651 μs - 322 μs (optimizations) = **329 μs**
- **Still 2.2x slower than target (100-150 μs)**

**Implication:** May need Phase 4 (spatial partitioning, SIMD, etc.) to hit goal

**Question for User:** Is 300-350 μs acceptable, or is 100-150 μs a hard requirement?

---

## 8. Conclusion

### Summary of Findings

1. **Massive code duplication** - Hardware constants, angle checking, arm loops repeated 4+ times
2. **isAngleInArc() is the bottleneck** - 258 μs (40% of total time) due to 150 redundant calls
3. **RenderContext is under-utilized** - Pre-computing angle checks could save 95% of overhead
4. **SetPixelColor is suspiciously slow** - Needs deeper investigation

### Recommended Next Steps

1. **Implement Phase 1** (low-hanging fruit) - Clean up duplicates, zero risk
2. **Implement Phase 2** (RenderContext pre-computation) - Biggest performance win
3. **Profile again** - Measure actual speedup, identify remaining bottlenecks
4. **Decide on Phase 3/4** - Based on whether 300-350 μs is acceptable

### Expected Outcome

- **Conservative estimate:** 40-50% speedup (651 μs → 300-400 μs)
- **Optimistic estimate:** 60-70% speedup (651 μs → 200-300 μs) if SetPixelColor optimizes well
- **To hit 100-150 μs:** Will likely need algorithmic changes (Phase 4)

---

**End of Report**
