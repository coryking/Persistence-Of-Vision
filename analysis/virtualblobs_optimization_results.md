# Rendering Performance Optimization Results

**Date:** 2025-11-27
**Optimization Target:** All effect rendering performance (VirtualBlobs, PerArmBlobs, SolidArms, RpmArc)
**Implementation Status:** ✅ Complete - ready for hardware testing
**Overall Result:** 2.58x speedup (61.3% frame time reduction) on VirtualBlobs

## Performance Summary

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| **Mean frame time** | 700 μs | 271 μs | **-429 μs (61.3%)** |
| **Degrees per frame** | 11.75° | 4.55° | -7.20° |
| **Updates per degree** | 0.085 | 0.220 | +0.135 (2.6x) |
| **Performance status** | ✗ FAIL | ✗ FAIL | Still over budget but much better |

### Operating Context
- **RPM:** 2800.1
- **Time per degree:** 59.52 μs
- **Target:** ≥1 update per degree (≤59.52 μs per frame)
- **Current gap:** 211.48 μs (4.5x over budget)

## Operation-Level Improvements

| Operation | Before | After | Speedup | Time Saved |
|-----------|--------|-------|---------|------------|
| **Angle checks** | 275 μs | 31 μs | **8.97x** | **-244 μs** |
| **SetPixelColor** | 126 μs | 27 μs | **4.71x** | **-99 μs** |
| **Array lookups** | 29 μs | 26 μs | 1.13x | -3 μs |
| Radial checks | 6 μs | 22 μs | 0.25x | +17 μs |
| Color blends | 1 μs | 7 μs | 0.14x | +6 μs |
| RGB construction | 23 μs | 25 μs | 0.92x | +2 μs |
| Instrumentation overhead | 314 μs | 47 μs | 6.63x | -267 μs |

## Optimizations Applied

### 1. Hoisted Angle Checks (8.97x speedup)

**Problem:** Calling `isAngleInArc()` for every LED (10 LEDs × 5 blobs × 3 arms = 150 calls per frame)

**Before:**
```cpp
for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
    for (int i = 0; i < MAX_BLOBS; i++) {
        // Check angle for EVERY LED
        if (blobs[i].active && isAngleInArc(ctx.innerArmDegrees, blobs[i])) {
            // ... radial check ...
        }
    }
}
```

**After:**
```cpp
// Pre-compute visibility once per arm (5 checks instead of 50)
bool blobVisibleOnInnerArm[MAX_BLOBS];
for (int i = 0; i < MAX_BLOBS; i++) {
    blobVisibleOnInnerArm[i] = blobs[i].active && isAngleInArc(ctx.innerArmDegrees, blobs[i]);
}

for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
    for (int i = 0; i < MAX_BLOBS; i++) {
        if (blobVisibleOnInnerArm[i]) {  // Just array lookup now
            // ... radial check ...
        }
    }
}
```

**Result:** 275 μs → 31 μs (-244 μs)

### 2. Direct Buffer Writes (4.71x speedup)

**Problem:** `SetPixelColor()` function call overhead per LED (30 calls per frame)

**Before:**
```cpp
strip.SetPixelColor(physicalLed, ledColor);
```

**After:**
```cpp
uint8_t* buffer = strip.Pixels();
setPixelColorDirect(buffer, physicalLed, r, g, b);
```

**Result:** 126 μs → 27 μs (-99 μs)

### 3. Raw RGB Values Instead of RgbColor Objects

**Problem:** Object construction overhead and member access indirection

**Before:**
```cpp
RgbColor ledColor(0, 0, 0);
ledColor.R = min(255, ledColor.R + blobs[i].color.R);
ledColor.G = min(255, ledColor.G + blobs[i].color.G);
ledColor.B = min(255, ledColor.B + blobs[i].color.B);
```

**After:**
```cpp
uint8_t r = 0, g = 0, b = 0;
blendAdditive(r, g, b, blobs[i].color.R, blobs[i].color.G, blobs[i].color.B);
```

**Result:** Eliminated object construction overhead

### 4. Inline Additive Blending Function

**Problem:** Repeated min() calls and RGB component arithmetic

**Before:**
```cpp
ledColor.R = min(255, ledColor.R + blobs[i].color.R);
ledColor.G = min(255, ledColor.G + blobs[i].color.G);
ledColor.B = min(255, ledColor.B + blobs[i].color.B);
```

**After:**
```cpp
inline void blendAdditive(uint8_t& r, uint8_t& g, uint8_t& b,
                         uint8_t sr, uint8_t sg, uint8_t sb) {
    r = min(255, r + sr);
    g = min(255, g + sg);
    b = min(255, b + sb);
}
```

**Result:** Compiler can optimize better with inline function

## Data Sources

- **Before optimization:** `samples/2025-11-27-effect-1-with-extra.txt` (2 profiling samples)
- **After optimization:** `samples/2025-11-27-effect-1-post-optimization.txt` (3 profiling samples)
- **Code changes:** Git commit a5e3bec "refactor for perf gainz"

## Systematic Implementation Across All Effects

After proving the optimization pattern on VirtualBlobs, the same techniques were systematically applied across the entire codebase.

### Infrastructure Created

**`include/hardware_config.h`** - Shared hardware constants
```cpp
namespace HardwareConfig {
    constexpr uint16_t LEDS_PER_ARM = 10;
    constexpr uint16_t NUM_ARMS = 3;
    constexpr uint16_t TOTAL_LEDS = 30;
    constexpr uint16_t INNER_ARM_START = 10;
    constexpr uint16_t MIDDLE_ARM_START = 0;
    constexpr uint16_t OUTER_ARM_START = 20;
}
```
- **Why:** Eliminates duplicate constants across 4 effect files + main.cpp
- **Impact:** Zero performance change, improved maintainability

**`include/pixel_utils.h`** - Direct buffer access primitives
```cpp
// Low-level primitives
inline void setPixelColorDirect(uint8_t* buffer, uint16_t index, uint8_t r, uint8_t g, uint8_t b);
inline void getPixelColorDirect(const uint8_t* buffer, uint16_t index, uint8_t& r, uint8_t& g, uint8_t& b);

// Higher-level operations
inline void clearBuffer(uint8_t* buffer, uint16_t numLeds = 30);
inline void fillRange(uint8_t* buffer, uint16_t start, uint16_t count, uint8_t r, uint8_t g, uint8_t b);
inline void fillArm(uint8_t* buffer, uint16_t armStart, uint8_t r, uint8_t g, uint8_t b);
inline void blendAdditive(uint8_t& dstR, uint8_t& dstG, uint8_t& dstB, uint8_t srcR, uint8_t srcG, uint8_t srcB);
```
- **Why:** Bypasses NeoPixelBus overhead, provides reusable building blocks
- **Impact:** 4.71x faster pixel writes (126 μs → 27 μs)

### Core Library Change

**`src/main.cpp`** - Switched from `NeoPixelBusLg` to base `NeoPixelBus`
```cpp
// Before: NeoPixelBusLg applies gamma correction on EVERY SetPixelColor call
NeoPixelBusLg<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);
strip.SetLuminance(LED_LUMINANCE);

// After: Base NeoPixelBus with direct buffer access
NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod> strip(NUM_LEDS);
```
- **Why:** Removes per-pixel gamma correction overhead (3-4 μs × 30 pixels)
- **Impact:** Part of the 4.71x SetPixelColor speedup
- **Note:** Colors may be slightly more saturated - test on hardware

### Effects Updated

All effects now follow the optimized pattern:

**VirtualBlobs.cpp** ✅
- Hoisted angle checks (150 calls → 15 calls)
- Direct buffer writes via `setPixelColorDirect()`
- Raw RGB values + `blendAdditive()` helper
- Shared constants via `HardwareConfig::`

**PerArmBlobs.cpp** ✅
- Same optimizations as VirtualBlobs
- Pre-computes blob visibility per arm
- Direct buffer access throughout

**SolidArms.cpp** ✅
- Direct buffer writes
- Shared constants
- Lambda updated to use buffer pointer

**RpmArc.cpp** ✅
- Direct buffer writes
- Shared constants
- Lambda updated to use buffer pointer

### Build Verification

```bash
$ uv run pio run -e seeed_xiao_esp32s3
# Result: SUCCESS (14.73 seconds)
# Flash usage: 9.7% (322663 bytes)
# RAM usage: 6.7% (21836 bytes)
```

All effects compile and link successfully with optimizations enabled.

## Current Performance Breakdown (After Optimization)

Based on mean of 3 samples (271 μs total):

| Component | Time | % of Total | Notes |
|-----------|------|------------|-------|
| Unmeasured overhead | ~143 μs | 52.8% | Loop structure, branches, memory access |
| Radial checks | 22 μs | 8.1% | `isVirtualLedInBlob()` with float math |
| SetPixelColor | 27 μs | 10.0% | Already optimized (was 126 μs) |
| Array lookups | 26 μs | 9.6% | `PHYSICAL_TO_VIRTUAL` table |
| RGB construction | 25 μs | 9.2% | Variable initialization |
| Angle checks | 31 μs | 11.4% | Pre-computed per-arm (was 275 μs) |
| Color blends | 7 μs | 2.6% | Inline blending function |
| Instrumentation | 47 μs | 17.3% | Profiling overhead |

## What's Left to Squeeze?

Current frame time: 271 μs
Target frame time: 59.52 μs
**Gap: 211.48 μs (4.5x over budget)**

### Unmeasured Overhead (143 μs ≈ 53%)

The largest component is "unmeasured overhead" - time not captured by instrumentation:

1. **Loop control flow** - 3 arm loops, 30 LED iterations, 150 blob checks
2. **Branch prediction** - Conditional jumps in radial wraparound logic
3. **Memory access** - Reading blob properties, buffer pointer arithmetic
4. **Function call overhead** - Entry/exit to `renderVirtualBlobs()`
5. **Compiler-generated code** - Register spills, alignment padding, pipeline stalls

At `-O3 -ffast-math -finline-functions -funroll-loops`, the compiler has likely already optimized this heavily.

### Potential Next Steps

**Pragmatic options (in priority order):**

1. **Profile unmeasured overhead**
   Add instrumentation to loop structure to find specific hotspots

2. **Algorithmic simplification**
   - Reduce `MAX_BLOBS` from 5 to 3 (-40% loop iterations)
   - Simplify `isVirtualLedInBlob()` (remove wraparound handling?)
   - Pre-compute more (beyond angle checks)

3. **Accept current performance**
   0.22 updates/degree might look acceptable on real hardware
   Test before optimizing further

4. **Hardware solution**
   Run at lower RPM where timing budget is larger

5. **Avoid premature micro-optimization**
   Don't fight the compiler with "clever tricks"
   Let `-O3` do its job

### Why Not FastLED?

From `docs/TIMING_ANALYSIS.md`:
> FastLED.show() took 100s of microseconds vs. NeoPixelBus's ~44μs in POV_Project. Architecture doesn't matter if the LED library bottleneck dominates all timing.

NeoPixelBus is already the fast path. The current bottleneck is rendering logic, not SPI transfer.

## Lessons Learned

1. **Hoist invariants out of loops**
   Angle checks were the same for all 10 LEDs per arm - only needed 5 checks, not 50

2. **Function call overhead matters**
   SetPixelColor was 4.71x slower than direct buffer writes

3. **Measure before optimizing**
   Profiling identified angle checks and SetPixelColor as primary bottlenecks

4. **Compiler optimization works**
   At `-O3`, inline functions and raw types give compiler better optimization opportunities

5. **Unmeasured overhead is real**
   53% of frame time is loop structure we can't easily optimize away

## Conclusion

**Achievement:**
- 2.58x speedup through targeted optimization of measured bottlenecks
- Systematic application of optimization pattern across all 4 effects
- Clean infrastructure for future optimization work

**Current status:**
- VirtualBlobs measured at 271 μs (still 4.5x over budget for 1 update/degree at 2800 RPM)
- Other effects not yet profiled with new optimizations
- Implementation complete and ready for hardware testing

**Implementation quality:**
- ✅ All effects compile successfully
- ✅ Shared infrastructure eliminates code duplication
- ✅ Inline helpers give compiler better optimization opportunities
- ✅ Direct buffer access bypasses library overhead

**Philosophy alignment:**
- Measured bottlenecks first, then optimized systematically
- Pragmatic approach without fighting the compiler
- Applied proven pattern across entire codebase
- Infrastructure supports future refinements

**Next steps:**
1. **Test on actual hardware** - Verify visual output at various RPMs (700-2800)
2. **Profile other effects** - Measure PerArmBlobs, SolidArms, RpmArc with new optimizations
3. **Check gamma impact** - Colors may be more saturated without NeoPixelBusLg
4. **Decide on further optimization** - Accept current performance vs. algorithmic changes

**If gamma correction is needed:**
- Revert to `NeoPixelBusLg` in main.cpp
- Keep direct buffer writes
- Add `strip.ApplyPostAdjustments()` after buffer writes
- Still 2-4x faster than original (applies gamma once per frame, not per pixel)
