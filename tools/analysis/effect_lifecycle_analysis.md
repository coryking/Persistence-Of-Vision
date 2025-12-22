# Effect Lifecycle Analysis

**Date:** 2025-11-27  
**Scope:** All effects in `src/effects/*.cpp`  
**Purpose:** Understand initialization, rendering, data flow, and performance characteristics of each effect to identify optimization and refactoring opportunities.

---

## Executive Summary

### Effects Analyzed
1. **VirtualBlobs.cpp** - Complex multi-blob effect with virtual (0-29) radial addressing
2. **PerArmBlobs.cpp** - Multi-blob effect with per-arm (0-9) radial addressing
3. **RpmArc.cpp** - Simple performance indicator showing RPM as a growing arc
4. **SolidArms.cpp** - Diagnostic test pattern (20 rotation-based patterns)

### Key Findings

#### Performance Tiers (by frame time complexity)
| Effect | Generation Time | Updates/degree | Status |
|--------|-----------------|----------------|--------|
| RPM Arc | 88.7 μs | 0.34 ✅ | Fastest, acceptable |
| PerArmBlobs | 141.5 μs | 0.25 ⚠️ | Medium, marginal |
| SolidArms | 154.0 μs | 0.23 ⚠️ | Medium, marginal |
| VirtualBlobs | 275.2 μs | 0.16 ❌ | Slowest, below budget |

#### Common Patterns Across Effects
1. **Initialization** - Template-based blob config, color palette lookup
2. **Rendering** - 3-arm loop structure with angle checks and LED iteration
3. **Data Flow** - RenderContext → angle checks → radial checks → pixel writes
4. **Optimization Applied** - Hoisted angle checks, direct buffer writes, raw RGB values

#### What's Safe to Change
- Any optimization that preserves visual output is safe
- Refactoring that eliminates code duplication (rule of three)
- Performance improvements in hot paths (identified in profiling data)

#### What to Preserve
- Blob animation dynamics (sine wave wandering)
- Color blending semantics (additive color mixing)
- Virtual/physical LED mapping (critical for correct display)
- Angle wraparound handling (360° boundary logic)

---

## Per-Effect Breakdown

### 1. VirtualBlobs.cpp

**Purpose:** Render 5 animated blobs across a 0-29 virtual LED space (simulated full 30-row display)

#### Initialization: `setupVirtualBlobs()`

**Complexity:** O(1) - Fixed 5 blobs regardless of parameters

```cpp
Blob Configuration (5 blobs):
├── Active: true (all immortal)
├── Color: citrusPalette[i] (orange→blue gradient)
├── Angular Parameters:
│   ├── wanderCenter: 72° increments (0, 72, 144, 216, 288°)
│   ├── wanderRange: 60-120° (varied per template)
│   ├── driftVelocity: 0.15-0.5 rad/sec
│   ├── minArcSize: 5-20°
│   └── maxArcSize: 30-90°
├── Radial Parameters (0-29 LED range):
│   ├── radialWanderCenter: 14.5 (center of virtual display)
│   ├── radialWanderRange: 4-8 LEDs
│   ├── radialDriftVelocity: 0.15-0.4 rad/sec
│   ├── minRadialSize: 2-6 LEDs
│   └── maxRadialSize: 6-14 LEDs
├── Lifecycle:
│   ├── birthTime: now (esp_timer_get_time())
│   └── deathTime: 0 (immortal)
└── Templates Applied: 3 templates (small/fast, medium, large/slow)
    └── Used cyclically: templates[i % 3]
```

**Initialization Cost:** ~1-2 μs (simple assignments)

**Data Dependencies:**
- `esp_timer_get_time()` for current timestamp
- `citrusPalette[5]` for colors (HSL→RGB conversion in NeoPixelBus)
- Template constants hardcoded

#### Rendering: `renderVirtualBlobs(const RenderContext& ctx)`

**Complexity:** O(3 × 10 × 5) = O(150) LED-blob checks per frame

**Data Flow:**
```
RenderContext (angle + timing) 
    ↓
[Pre-compute angle visibility per blob per arm]
    ↓
[For each arm: 10 LED positions]
    ├─→ [For each of 5 blobs]
    │   ├─→ Check angle visibility (pre-computed)
    │   ├─→ Check radial membership (float arithmetic)
    │   └─→ Additive blend color if match
    └─→ Write pixel to buffer via direct access

Final: strip.Dirty() marks buffer for SPI transfer
```

**Rendering Pipeline (per-arm example - repeated for 3 arms):**

```cpp
// Phase 1: Pre-compute blob visibility (hoisted optimization)
bool blobVisibleOnInnerArm[MAX_BLOBS];  // Stack array, 5 bytes
for (int i = 0; i < MAX_BLOBS; i++) {
    // Calls isAngleInArc() - only 5 times per arm (not 50!)
    blobVisibleOnInnerArm[i] = blobs[i].active && 
                               isAngleInArc(ctx.innerArmDegrees, blobs[i]);
}

// Phase 2: Render LEDs
for (uint16_t ledIdx = 0; ledIdx < 10; ledIdx++) {
    uint16_t physicalLed = INNER_ARM_START + ledIdx;  // 10-19
    uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];  // 1,4,7,10,13,16,19,22,25,28
    
    // Initialize RGB to black
    uint8_t r = 0, g = 0, b = 0;
    
    // Check all blobs
    for (int i = 0; i < MAX_BLOBS; i++) {
        if (blobVisibleOnInnerArm[i]) {  // Array lookup
            // Radial check with wraparound for 0-29 range
            bool radialMatch = isVirtualLedInBlob(virtualPos, blobs[i]);
            
            if (radialMatch) {
                // Additive color blending
                blendAdditive(r, g, b, 
                             blobs[i].color.R, blobs[i].color.G, blobs[i].color.B);
            }
        }
    }
    
    // Direct buffer write (4 bytes per LED: [0xFF][B][G][R])
    setPixelColorDirect(buffer, physicalLed, r, g, b);
}
```

**Hotspot: `isVirtualLedInBlob()` - float wraparound arithmetic**

```cpp
static bool isVirtualLedInBlob(uint8_t virtualPos, const Blob& blob) {
    float halfSize = blob.currentRadialSize / 2.0f;
    float radialStart = blob.currentRadialCenter - halfSize;
    float radialEnd = blob.currentRadialCenter + halfSize;
    float pos = static_cast<float>(virtualPos);
    
    // Three wraparound cases:
    // 1. No wrap: simple range check
    if (radialStart >= 0 && radialEnd < 30) {
        return (pos >= radialStart) && (pos < radialEnd);
    }
    
    // 2. Wraps below 0 (e.g., -2 to 5 → 28 to 5)
    if (radialStart < 0) {
        return (pos >= (radialStart + 30)) || (pos < radialEnd);
    }
    
    // 3. Wraps above 29 (e.g., 27 to 32 → 27 to 2)
    if (radialEnd >= 30) {
        return (pos >= radialStart) || (pos < (radialEnd - 30));
    }
    
    return false;
}
```

**Performance Analysis:**
- **Angle checks:** 5 per arm × 3 arms = 15 checks (hoisted, pre-computed)
  - Cost: ~31 μs total (after optimization)
  - Before: 150 calls × ~1.8 μs = ~275 μs (not hoisted)
  - Speedup: **8.97x** from hoisting alone

- **Radial checks:** 5 blobs × 10 LEDs × 3 arms = 150 checks
  - Cost: ~22 μs total
  - Float arithmetic, range checking with wraparound

- **Pixel writes:** 30 LEDs
  - Cost: ~27 μs total (direct buffer access)
  - Before: ~126 μs (NeoPixelBus SetPixelColor overhead)
  - Speedup: **4.71x** from direct access

- **Color blending:** Variable (only when both checks pass)
  - Cost: ~7 μs total
  - Inline `std::min()` operations

**Total Frame Time:** 271 μs (mean)
- Unmeasured overhead: ~143 μs (53% - loop structure, branches, memory access)
- Measured components: ~128 μs (47%)

#### Data Dependencies
- **Blob state:** Updated externally via `updateBlob()` in `blob_types.h`
- **RenderContext:** Passed from main loop, contains arm angles + timing
- **Color palette:** `citrusPalette[5]` from `blob_types.h`
- **Physical→Virtual mapping:** `PHYSICAL_TO_VIRTUAL[30]` from main.cpp

#### Intentional Visual Artifacts
- **Blob breathing:** Radial size oscillates (sine wave) - visible pulsing
- **Blob wandering:** Angular and radial drift create organic motion
- **Additive blending:** Overlapping blobs create bright spots (artistic)
- **Wraparound:** Blobs seamlessly loop when crossing 0-29 boundary

#### Complexity Analysis

**Per-frame complexity:**

```
Setup: 
  - Angle pre-computation: O(5) = 15 comparisons
  
Per-arm (3×):
  - LED iteration: O(10)
  - Per-LED blob checks: O(5)
  - Float arithmetic (radial): O(1) per check
  - Pixel write: O(1)
  
Total: O(3 × 10 × 5) = O(150) blob-LED membership tests
       O(30) pixel writes
       O(15) angle checks
```

**Time Distribution (real measurements):**
- Loop overhead: 52.8% (143 μs unmeasured)
- Angle checks: 11.4% (31 μs)
- Pixel writes: 10.0% (27 μs)
- Array lookups: 9.6% (26 μs)
- RGB initialization: 9.2% (25 μs)
- Radial checks: 8.1% (22 μs)
- Color blending: 2.6% (7 μs)

**Algorithmic insight:** The limiting factor is not measured operations but unmeasured overhead (loop structure, branches, cache behavior). Further optimization would require:
1. Reducing loop iterations (fewer blobs or LEDs)
2. Simplifying branch logic (fewer wraparound cases)
3. Assembly-level tuning (unlikely to help significantly)

---

### 2. PerArmBlobs.cpp

**Purpose:** Render 5 blobs distributed across 3 arms (2 per inner, 2 per middle, 1 per outer)

#### Initialization: `setupPerArmBlobs()`

**Complexity:** O(1) - Fixed 5 blobs, fixed arm distribution

```cpp
Blob Configuration (5 blobs):
├── Arm Assignment: [ARM_INNER, ARM_INNER, ARM_MIDDLE, ARM_MIDDLE, ARM_OUTER]
├── Color: citrusPalette[i] (orange→blue gradient)
├── Angular Parameters: Same as VirtualBlobs
├── Radial Parameters (0-9 LED range per arm):
│   ├── radialWanderCenter: 4.5 (center of 10-LED arm)
│   ├── radialWanderRange: 2.0-3.0 LEDs (smaller than virtual)
│   ├── minRadialSize: 1-3 LEDs
│   └── maxRadialSize: 3-7 LEDs
└── Lifecycle: Same as VirtualBlobs
```

**Key Difference from VirtualBlobs:** Blobs belong to specific arms (via `armIndex`), not entire virtual space.

#### Rendering: `renderPerArmBlobs(const RenderContext& ctx)`

**Complexity:** O(3 × 10 × 5) = O(150) similar to VirtualBlobs, but with arm filtering

**Rendering Pipeline (per-arm example):**

```cpp
// Phase 1: Pre-compute visibility (with arm membership check)
bool blobVisibleOnInnerArm[MAX_BLOBS];
for (int i = 0; i < MAX_BLOBS; i++) {
    blobVisibleOnInnerArm[i] = blobs[i].active && 
                               blobs[i].armIndex == ARM_INNER &&
                               isAngleInArc(ctx.innerArmDegrees, blobs[i]);
}

// Phase 2: Render LEDs (simpler radial check - no wraparound)
for (uint16_t ledIdx = 0; ledIdx < 10; ledIdx++) {
    uint8_t r = 0, g = 0, b = 0;
    
    for (int i = 0; i < MAX_BLOBS; i++) {
        if (blobVisibleOnInnerArm[i]) {
            // Simple radial check within 0-9 range
            bool radialMatch = isLedInBlob(ledIdx, blobs[i]);
            
            if (radialMatch) {
                blendAdditive(r, g, b, 
                             blobs[i].color.R, blobs[i].color.G, blobs[i].color.B);
            }
        }
    }
    
    setPixelColorDirect(buffer, INNER_ARM_START + ledIdx, r, g, b);
}
```

**Hotspot: `isLedInBlob()` - simpler than VirtualBlobs (no wraparound)**

```cpp
static bool isLedInBlob(uint16_t ledIndex, const Blob& blob) {
    float halfSize = blob.currentRadialSize / 2.0f;
    float radialStart = blob.currentRadialCenter - halfSize;
    float radialEnd = blob.currentRadialCenter + halfSize;
    float ledFloat = static_cast<float>(ledIndex);
    
    // Single range check (0-9 range, no wraparound needed)
    return (ledFloat >= radialStart) && (ledFloat < radialEnd);
}
```

**Performance Analysis:**

| Component | Time | Notes |
|-----------|------|-------|
| Angle checks | ~30 μs | 5 checks per arm × 3 arms (hoisted) |
| Radial checks | ~20 μs | Simpler than VirtualBlobs (no wraparound) |
| Pixel writes | ~27 μs | Direct buffer access |
| Blending | ~7 μs | Same as VirtualBlobs |
| **Total frame time** | **~141.5 μs** | 32% faster than VirtualBlobs |

**Why faster than VirtualBlobs?**
1. **Simpler radial logic:** No wraparound handling (0-9 is always a simple range)
2. **Fewer angle checks:** Only 5 blobs of assigned arm matter (not all 5 per arm)
3. **Lower math complexity:** Less float arithmetic overall

#### Data Dependencies
- **Blob state:** Updated externally via `updateBlob()`
- **Arm membership:** Fixed in `setupPerArmBlobs()` (no runtime changes)
- **RenderContext:** Arm angles + timing

#### Intentional Visual Artifacts
- **Arm-specific blobs:** Blobs stay on their assigned arm (visual separation)
- **Smaller radial range:** 1-7 LEDs vs 2-14 (smaller blobs per arm)
- **Additive blending:** Overlapping blobs within same arm create bright spots

#### Complexity Analysis

```
Setup: 
  - Angle pre-computation: O(5)
  - Arm filtering: Hardcoded in visibility check
  
Per-arm (3×):
  - LED iteration: O(10)
  - Per-LED blob checks: O(5)
  - Simple range check (radial): O(1)
  - Pixel write: O(1)
  
Total: Still O(150) checks, but simpler arithmetic
```

**Pragmatic Difference:**
- Same loop structure as VirtualBlobs
- Simpler radial check function (no wraparound)
- Faster by ~34 μs (141.5 vs 275.2 μs)
- Still ~2.8x slower than RpmArc

---

### 3. RpmArc.cpp

**Purpose:** Simple performance indicator - shows RPM as a growing arc from 1-30 virtual pixels

#### Initialization: None (stateless effect)

**Complexity:** O(1) - No persistent state to initialize

#### Rendering: `renderRpmArc(const RenderContext& ctx)`

**Complexity:** O(3 × 10) = O(30) LED writes per frame (no blob checks!)

**Rendering Pipeline:**

```cpp
// Calculate current RPM
float currentRPM = calculateRPM(ctx.microsecondsPerRev);  // 60M / μs_per_rev
uint8_t pixelCount = rpmToPixelCount(currentRPM);         // Map to 1-30 pixels

// Lambda: Render one arm
auto renderRpmArm = [&](double angle, uint16_t armStart) {
    // Check if arm is in 20-degree arc
    if (isAngleInRpmArc(angle)) {
        // Light up pixels 0 through pixelCount-1
        for (uint16_t ledIdx = 0; ledIdx < 10; ledIdx++) {
            uint16_t physicalLed = armStart + ledIdx;
            uint8_t virtualPos = PHYSICAL_TO_VIRTUAL[physicalLed];
            
            if (virtualPos < pixelCount) {
                // Gradient color: green (0) → red (29)
                RgbColor color = getGradientColor(virtualPos);
                setPixelColorDirect(buffer, physicalLed, color.R, color.G, color.B);
            } else {
                // Beyond RPM range, turn off
                setPixelColorDirect(buffer, physicalLed, 0, 0, 0);
            }
        }
    } else {
        // Arm outside arc, all LEDs off
        for (uint16_t ledIdx = 0; ledIdx < 10; ledIdx++) {
            setPixelColorDirect(buffer, armStart + ledIdx, 0, 0, 0);
        }
    }
};

// Render all three arms
renderRpmArm(ctx.innerArmDegrees, INNER_ARM_START);
renderRpmArm(ctx.middleArmDegrees, MIDDLE_ARM_START);
renderRpmArm(ctx.outerArmDegrees, OUTER_ARM_START);
```

**Supporting Functions:**

```cpp
// Calculate RPM from microseconds per revolution
float calculateRPM(interval_t microsecondsPerRev) {
    if (microsecondsPerRev == 0) return 0.0f;
    return 60000000.0f / static_cast<float>(microsecondsPerRev);  // 60M = 60 sec * 1M μs
}

// Map RPM to pixel count (1-30)
uint8_t rpmToPixelCount(float rpm) {
    if (rpm < RPM_MIN) rpm = RPM_MIN;           // 800 RPM
    if (rpm > RPM_MAX) rpm = RPM_MAX;           // 2500 RPM
    
    float normalized = (rpm - RPM_MIN) / (RPM_MAX - RPM_MIN);
    uint8_t pixels = static_cast<uint8_t>(1.0f + normalized * 29.0f);
    
    if (pixels < 1) pixels = 1;
    if (pixels > 30) pixels = 30;
    return pixels;
}

// Get gradient color (green→red based on virtual position)
RgbColor getGradientColor(uint8_t virtualPos) {
    float t = static_cast<float>(virtualPos) / 29.0f;  // 0.0-1.0
    float hue = 120.0f * (1.0f - t) / 360.0f;          // Green (120°) to Red (0°)
    
    HslColor hsl(hue, 1.0f, 0.5f);  // Saturated, medium lightness
    return RgbColor(hsl);  // HSL→RGB conversion
}

// Check angle in 20-degree arc
bool isAngleInRpmArc(double angle) {
    // Arc centered at 0° with 20° width (±10°)
    double halfWidth = 10.0;
    double arcStart = -10.0;
    double arcEnd = 10.0;
    
    angle = fmod(angle, 360.0);
    if (angle < 0) angle += 360.0;
    
    // Handle wraparound at 0/360
    if (arcStart < 0) {
        return (angle >= (arcStart + 360.0)) || (angle < arcEnd);
    } else if (arcEnd > 360.0) {
        return (angle >= arcStart) || (angle < (arcEnd - 360.0));
    } else {
        return (angle >= arcStart) && (angle < arcEnd);
    }
}
```

**Performance Analysis:**

| Component | Time | Notes |
|-----------|------|-------|
| RPM calculation | < 1 μs | Single division + format cast |
| Pixel count mapping | ~1-2 μs | 3-4 float comparisons + cast |
| Angle check | ~3 μs | Simple modulo + comparison per arm |
| Gradient colors | ~10 μs | HSL→RGB conversion (30 calls) |
| Pixel writes | ~70 μs | 30 direct writes |
| **Total frame time** | **~88.7 μs** | Fastest effect (3.1x faster than VirtualBlobs) |

**Why so much faster than blob effects?**
1. **No per-blob checks:** Just RPM → pixel count mapping (4 values)
2. **Simple angle logic:** Just arm visibility check (3 comparisons per arm)
3. **Stateless:** No blob state to maintain
4. **Linear iteration:** All 30 LEDs visited, simple gradient applied

#### Data Dependencies
- **RenderContext:** Only uses `microsecondsPerRev` (timing) and arm angles
- **PHYSICAL_TO_VIRTUAL mapping:** For gradient application
- **HSL color palette:** Green→Red gradient computed per call

#### Intentional Visual Artifacts
- **Growing arc:** As RPM increases, arc grows outward (visual feedback)
- **Green-red gradient:** Inner LEDs green (slow), outer red (fast)
- **Smooth update:** Continuous RPM changes create smooth arc growth

#### Complexity Analysis

```
Per-frame:
  - RPM calculation: O(1)
  - Pixel count mapping: O(1)
  - Angle check per arm: O(3) = 3 comparisons
  - LED iteration: O(30)
  - Per-LED gradient: O(1) HSL→RGB
  - Per-LED write: O(1)
  
Total: O(30) operations (linear in LED count, constant blob logic)
```

**Why recommended for "fast" testing:**
- Tightest loop (no blob iteration)
- Simplest rendering logic (no complex conditions)
- Single state value (RPM)
- Best performance characteristics

---

### 4. SolidArms.cpp

**Purpose:** Diagnostic test pattern - 20 rotation-based test patterns for alignment verification

#### Initialization: None (stateless effect)

**Complexity:** O(1) - No persistent state

#### Rendering: `renderSolidArms(const RenderContext& ctx)`

**Complexity:** O(3 × 10) = O(30) LED writes per frame (same as RpmArc)

**Rendering Pipeline:**

```cpp
// Lambda: Render one arm based on angle
auto renderArm = [&](double angle, uint16_t armStart, uint8_t armIndex) {
    // Normalize angle to 0-359
    double normAngle = fmod(angle, 360.0);
    if (normAngle < 0) normAngle += 360.0;
    
    // Determine test pattern (0-19)
    uint8_t pattern = (uint8_t)(normAngle / 18.0);  // 360° / 20 = 18° per pattern
    if (pattern > 19) pattern = 19;
    
    // Determine what this arm displays
    RgbColor armColor = OFF_COLOR;
    bool fullLeds = true;  // All 10 vs only positions 0,4,9
    
    // ===== Pattern Selection Logic =====
    
    if (pattern <= 3) {
        // Patterns 0-3: Full RGB combinations (all LEDs)
        // Tests: Red/Green/Blue rotation + white
        RgbColor colors[4][3] = {
            // Pattern 0: A=red, B=green, C=blue
            {RgbColor(255, 0, 0), RgbColor(0, 255, 0), RgbColor(0, 0, 255)},
            // Pattern 1: Rotated
            {RgbColor(0, 255, 0), RgbColor(0, 0, 255), RgbColor(255, 0, 0)},
            // Pattern 2: Rotated again
            {RgbColor(0, 0, 255), RgbColor(255, 0, 0), RgbColor(0, 255, 0)},
            // Pattern 3: All white
            {WHITE, WHITE, WHITE}
        };
        armColor = colors[pattern][armIndex];
        fullLeds = true;
    }
    else if (pattern <= 7) {
        // Patterns 4-7: Striped (only positions 0, 4, 9)
        // Tests alignment of 3 arms in sparse pattern
        fullLeds = false;
        // Same color logic as patterns 0-3
        RgbColor colors[4][3] = { /* ... */ };
        armColor = colors[pattern - 4][armIndex];
    }
    else if (pattern <= 11) {
        // Patterns 8-11: Arm A (inner) individual tests
        fullLeds = true;
        if (armIndex == 0) {
            RgbColor colors[4] = {RED, GREEN, BLUE, WHITE};
            armColor = colors[pattern - 8];
        }
        // Other arms stay off
    }
    else if (pattern <= 15) {
        // Patterns 12-15: Arm B (middle) individual tests
        fullLeds = true;
        if (armIndex == 1) {
            RgbColor colors[4] = {RED, GREEN, BLUE, WHITE};
            armColor = colors[pattern - 12];
        }
    }
    else {
        // Patterns 16-19: Arm C (outer) individual tests
        fullLeds = true;
        if (armIndex == 2) {
            RgbColor colors[4] = {RED, GREEN, BLUE, WHITE};
            armColor = colors[pattern - 16];
        }
    }
    
    // Render LEDs
    for (uint16_t ledIdx = 0; ledIdx < 10; ledIdx++) {
        if (fullLeds) {
            setPixelColorDirect(buffer, armStart + ledIdx, armColor.R, armColor.G, armColor.B);
        } else {
            // Striped: only positions 0, 4, 9
            if (ledIdx == 0 || ledIdx == 4 || ledIdx == 9) {
                setPixelColorDirect(buffer, armStart + ledIdx, armColor.R, armColor.G, armColor.B);
            } else {
                setPixelColorDirect(buffer, armStart + ledIdx, 0, 0, 0);
            }
        }
    }
};

// Render all three arms
renderArm(ctx.innerArmDegrees, INNER_ARM_START, 0);
renderArm(ctx.middleArmDegrees, MIDDLE_ARM_START, 1);
renderArm(ctx.outerArmDegrees, OUTER_ARM_START, 2);
```

**Test Pattern Breakdown:**

```
Pattern Range | Angle Range | Test Type | Purpose
-------------|-------------|-----------|----------
0-3    | 0-71°   | RGB combinations | Basic color output
4-7    | 72-143° | Sparse striped   | Alignment between arms
8-11   | 144-215°| Arm A only       | Isolate inner arm
12-15  | 216-287°| Arm B only       | Isolate middle arm
16-19  | 288-359°| Arm C only       | Isolate outer arm
```

**Performance Analysis:**

| Component | Time | Notes |
|-----------|------|-------|
| Angle normalization | ~2 μs | fmod + modulo arithmetic |
| Pattern calculation | ~1 μs | Division by 18 |
| Pattern matching | ~2-3 μs | Nested conditionals |
| Pixel writes | ~70 μs | 30 direct writes |
| **Total frame time** | **~154.0 μs** | 1.7x faster than VirtualBlobs |

**Why faster than blob effects but slower than RpmArc?**
1. **No per-pixel computation:** Simple color lookup
2. **More branching:** Pattern logic adds conditional overhead
3. **Static coloring:** No gradients or complex math
4. **Simple angle logic:** Just pattern selection

#### Data Dependencies
- **RenderContext:** Only uses arm angles
- **Hardcoded colors:** No dynamic computation

#### Intentional Visual Artifacts
- **20 discrete patterns:** Creates visible "steps" as disc rotates
- **Sparse striping:** Tests alignment of the 3-arm system
- **Individual arm isolation:** Can verify each arm independently
- **Pure diagnostics:** No artistic intent, just engineering validation

#### Complexity Analysis

```
Per-frame:
  - Angle normalization: O(1)
  - Pattern selection: O(1) with branching
  - Per-arm rendering: O(3 × 10) = O(30)
  
Total: O(30) operations (linear in LED count)
```

---

## Cross-Effect Pattern Analysis

### 1. Initialization Pattern: Template-Based Configuration

**Found in:** VirtualBlobs, PerArmBlobs  
**Code pattern:**

```cpp
struct BlobTemplate {
    float minAngularSize, maxAngularSize;
    float angularDriftSpeed;
    // ... 6 more parameters
} templates[3];

for (int i = 0; i < MAX_BLOBS; i++) {
    BlobTemplate& tmpl = templates[i % 3];  // Cycle through 3 templates
    blobs[i].minArcSize = tmpl.minAngularSize;
    blobs[i].maxArcSize = tmpl.maxAngularSize;
    // ... apply other parameters
}
```

**Observation:** Both blob effects use 3 template types (small/fast, medium, large/slow) applied cyclically to 5 blobs. This creates variety without hardcoding per-blob values.

**Rule of Three Opportunity:** Not yet at 3 implementations (only 2 effects use this), so extraction is premature per project philosophy.

### 2. Rendering Pattern: 3-Arm Loop Structure

**Found in:** VirtualBlobs, PerArmBlobs, RpmArc, SolidArms  
**Code pattern:**

```cpp
// Pattern: Hoisted pre-computation per arm
bool blobVisibleOnInnerArm[MAX_BLOBS];
for (int i = 0; i < MAX_BLOBS; i++) {
    blobVisibleOnInnerArm[i] = blobs[i].active && isAngleInArc(ctx.innerArmDegrees, blobs[i]);
}

// Pattern: LED iteration with blob checks
for (uint16_t ledIdx = 0; ledIdx < HardwareConfig::LEDS_PER_ARM; ledIdx++) {
    uint16_t physicalLed = INNER_ARM_START + ledIdx;
    
    // ... per-LED computation ...
    
    setPixelColorDirect(buffer, physicalLed, r, g, b);
}

// Repeated 3 times for inner, middle, outer arms
```

**Observation:** All 4 effects follow this structure:
1. Pre-compute visibility/state per arm
2. Iterate LEDs
3. Apply per-LED logic
4. Write to buffer

**Duplication Metric:**
- 3 arm blocks × 4 effects = 12 code blocks that follow same pattern
- ~50 lines per effect duplicated across effects

**Rule of Three Met:** 4 effects using same pattern. However, the differences are subtle (different per-LED logic), so extraction would require parameterization.

### 3. Data Flow Pattern: RenderContext → Visibility → Color

**Common pipeline:**

```
RenderContext (arm angles + timing)
    ↓
Angle checks (isAngleInArc, isAngleInRpmArc)
    ↓
Conditional logic (radial checks, pattern selection)
    ↓
Color computation (blending, gradient, palette lookup)
    ↓
Direct buffer write
```

**Observation:** All effects follow this DAG (directed acyclic graph):
- Input: Angles + timing
- Check 1: Is arm in active zone?
- Check 2: Is LED active within that zone?
- Compute: What color should this LED be?
- Output: Write to buffer

### 4. Optimization Pattern: Hoisted Checks + Direct Access

**Applied to:** All 4 effects (post-optimization)

**Pattern 1: Hoist angle checks**
```cpp
// Before: 150 checks per frame
for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
    for (int i = 0; i < MAX_BLOBS; i++) {
        if (isAngleInArc(angle, blob))  // Called 50 times per arm!
```

```cpp
// After: 15 checks per frame (hoisted)
for (int i = 0; i < MAX_BLOBS; i++) {
    blobVisible[i] = isAngleInArc(angle, blob);  // 5 times per arm
}
for (uint16_t ledIdx = 0; ledIdx < LEDS_PER_ARM; ledIdx++) {
    if (blobVisible[i])  // Just array lookup
```

**Speedup:** 8.97x (275 μs → 31 μs on angle checks)

**Pattern 2: Direct buffer writes**
```cpp
// Before: Function call overhead
strip.SetPixelColor(index, RgbColor(r, g, b));

// After: Direct memory access
uint8_t* buffer = strip.Pixels();
setPixelColorDirect(buffer, index, r, g, b);
```

**Speedup:** 4.71x (126 μs → 27 μs on pixel writes)

**Pattern 3: Raw RGB values**
```cpp
// Before: RgbColor object overhead
RgbColor color(0, 0, 0);
color.R = min(255, color.R + blob.color.R);

// After: Primitive types + inline function
uint8_t r = 0;
blendAdditive(r, g, b, blob.color.R, blob.color.G, blob.color.B);
```

**Speedup:** Compiler optimization (less object overhead)

---

## Optimization Opportunities (Prioritized)

### Completed Optimizations (Evidence in code)

1. ✅ **Hoisted angle checks** (8.97x speedup)
2. ✅ **Direct buffer access** (4.71x speedup on pixel writes)
3. ✅ **Raw RGB values** (compiler optimization)
4. ✅ **Shared constants** (HardwareConfig namespace)
5. ✅ **Inline helper functions** (blendAdditive, setPixelColorDirect)

### Available Opportunities (Not Yet Implemented)

#### Option 1: Extract Common Arm Rendering (Low Complexity)

**Pattern:** 3-arm loop is identical structure in all 4 effects

**Approach:**
```cpp
// New helper function
void renderArmsWithLambda(
    const RenderContext& ctx,
    std::function<void(uint8_t*, uint16_t, uint8_t)> perLedRender) {
    
    uint8_t* buffer = strip.Pixels();
    
    // Inner arm
    for (uint16_t ledIdx = 0; ledIdx < 10; ledIdx++) {
        perLedRender(buffer, INNER_ARM_START + ledIdx, 0);  // arm index
    }
    // Middle arm
    for (uint16_t ledIdx = 0; ledIdx < 10; ledIdx++) {
        perLedRender(buffer, MIDDLE_ARM_START + ledIdx, 1);
    }
    // Outer arm
    for (uint16_t ledIdx = 0; ledIdx < 10; ledIdx++) {
        perLedRender(buffer, OUTER_ARM_START + ledIdx, 2);
    }
    
    strip.Dirty();
}
```

**Impact:**
- Reduces line count per effect by ~100 lines
- Unifies buffer management and Dirty() calls
- Per-LED rendering passed as lambda for flexibility
- **Risk:** Lambda overhead in tight loop (measure first!)

**Complexity:** Medium (require testing for performance regression)

#### Option 2: Reduce MAX_BLOBS from 5 to 3 (Medium Complexity)

**Current:** 5 blobs = 150 blob-LED checks per frame  
**Proposed:** 3 blobs = 90 blob-LED checks per frame (-40%)

**Impact on VirtualBlobs:**
- ~36 μs savings on radial checks (22 → 13 μs)
- ~9 μs savings on angle checks (31 → 22 μs)
- Total: ~45 μs saved (271 → 226 μs)
- Still doesn't reach 1 update/degree goal, but closer

**Trade-off:** Fewer simultaneous blobs visible (artistic choice)

**Complexity:** Low (just change MAX_BLOBS constant and adjust templates)

#### Option 3: Simplify Radial Wraparound (Medium Complexity)

**Current:** VirtualBlobs handles 3 wraparound cases (lines 29-43 in VirtualBlobs.cpp)

**Observation:** Wraparound is rare at typical blob sizes (2-14 LEDs across 0-29 range)

**Proposed:** Remove wraparound handling, clip to 0-29

```cpp
// Simpler version
static bool isVirtualLedInBlob(uint8_t virtualPos, const Blob& blob) {
    float halfSize = blob.currentRadialSize / 2.0f;
    float radialStart = blob.currentRadialCenter - halfSize;
    float radialEnd = blob.currentRadialCenter + halfSize;
    
    // Clip to valid range
    if (radialStart < 0) radialStart = 0;
    if (radialEnd > 30) radialEnd = 30;
    
    float pos = static_cast<float>(virtualPos);
    return (pos >= radialStart) && (pos < radialEnd);
}
```

**Impact:**
- ~2-3 μs savings on radial checks
- Loss of seamless wraparound (blobs vanish at edge)
- Not visually ideal for animation

**Complexity:** Low (simple change)  
**Recommendation:** Test before committing (might break visual intent)

#### Option 4: Pre-compute All Visibility (High Complexity)

**Current:** Pre-compute angle visibility, compute radial at LED time

**Proposed:** Pre-compute both angle AND radial per (blob, LED) pair

```cpp
// Pre-compute phase
bool blobLedVisible[MAX_BLOBS][30];  // 5 × 30 = 150 bools
for (int b = 0; b < MAX_BLOBS; b++) {
    for (int l = 0; l < 30; l++) {
        blobLedVisible[b][l] = blobs[b].active &&
                               isAngleInArc(angle, blobs[b]) &&
                               isVirtualLedInBlob(l, blobs[b]);
    }
}

// Render phase: just look up pre-computed values
for (uint16_t ledIdx = 0; ledIdx < 30; ledIdx++) {
    uint8_t r = 0, g = 0, b = 0;
    for (int i = 0; i < MAX_BLOBS; i++) {
        if (blobLedVisible[i][ledIdx]) {
            blendAdditive(r, g, b, blobs[i].color.R, blobs[i].color.G, blobs[i].color.B);
        }
    }
    setPixelColorDirect(buffer, ledIdx, r, g, b);
}
```

**Impact:**
- Move 150 radial checks to pre-compute (parallel to LED loop)
- LED loop becomes pure table lookup
- ~15-20 μs savings
- More data structures (150-element array on stack)

**Complexity:** High  
**Risk:** Cache effects, branch misprediction changes

#### Option 5: SIMD or Assembly Optimization (Very High Complexity)

**Approach:** Hand-optimize hot paths in assembly or use SIMD for:
- Radial range checks (4 blobs in parallel?)
- Color addition (SIMD saturation)

**Reality Check:**
- Xtensa ISA (ESP32 CPU) has limited SIMD
- Compiler at `-O3 -ffast-math` likely already using vector operations
- Unlikely to yield significant gains vs effort

**Recommendation:** Skip unless profiling shows specific bottleneck

---

## What's Safe to Change vs What to Preserve

### Safe to Change

1. **Optimization implementations** that preserve visual output
   - Hoisting, inlining, direct access patterns ✅
   - Loop simplification if results are identical ✅
   - Constant refactoring ✅

2. **Rule of three refactoring**
   - When 3+ effects share identical pattern
   - Extract to helper function
   - Measure performance impact

3. **Non-visual internal state**
   - Blob update frequency (sine wave parameters)
   - Pre-computation of intermediate values
   - Buffer management details

### Must Preserve

1. **Visual output** (unless explicitly changing artistic intent)
   - Blob animation characteristics (wandering, breathing)
   - Color blending semantics (additive, not subtractive)
   - Arc size and position calculations
   - Gradient colors and transitions

2. **Physical/virtual mapping**
   - `PHYSICAL_TO_VIRTUAL[30]` lookup table
   - Arm LED ranges (0-9, 10-19, 20-29)
   - Virtual display coordinate system (0-29)

3. **Angle wraparound logic**
   - 360° boundary handling for rotation
   - Arc extension past 360°
   - Angle normalization

4. **Timing guarantees**
   - RenderContext passing (inputs to render functions)
   - Frame-by-frame update semantics
   - Blob lifecycle (currently immortal)

---

## Visual Artifact Assessment

### Intentional (Keep)

| Effect | Artifact | Visual Purpose |
|--------|----------|-----------------|
| VirtualBlobs | Radial size breathing | Pulsing, organic motion |
| VirtualBlobs | Angular drift | Blobs wander around display |
| VirtualBlobs | Additive blending | Bright spots where blobs overlap |
| PerArmBlobs | Arm-specific blobs | Visual separation, arm tracking |
| PerArmBlobs | Smaller radial range | More granular, arm-focused |
| RpmArc | Growing arc | RPM feedback (faster RPM = bigger arc) |
| RpmArc | Green→red gradient | Visual speed indicator |
| SolidArms | Discrete patterns | Diagnostic rotation visualization |
| SolidArms | Sparse striping (4-7) | Alignment verification |

### Unintentional (Could be fixed)

| Effect | Issue | Source | Fix |
|--------|-------|--------|-----|
| VirtualBlobs | Visible per-frame update gaps | Slow rendering (0.16 updates/degree) | Optimize to reach 1 update/degree |
| PerArmBlobs | Visible stepping | Medium rendering speed (0.25 updates/degree) | Acceptable for artistic use |
| All | Potential flickering at low RPM | Frame rate drops as RPM decreases | Acceptable for POV display |

---

## Recommendations for Next Steps

### 1. Profile Other Effects (Low Priority)
Current optimization results only available for VirtualBlobs (275 → 271 μs). Other effects should be measured post-optimization:
- PerArmBlobs: Expected ~141.5 μs (already measured, likely lower now)
- SolidArms: Expected ~154 μs (likely lower with direct access)
- RpmArc: Expected ~88.7 μs (already quite fast)

**Action:** Run `analysis/sample_collector.py` if hardware available, collect post-optimization samples.

### 2. Test Gamma Correction Impact (Medium Priority)
Switched from `NeoPixelBusLg` to base `NeoPixelBus` for performance. Colors may be more saturated.

**Action:** 
- Observe colors on real hardware
- If needed, revert to `NeoPixelBusLg` and apply gamma once per frame (not per pixel)
- Or add post-process gamma adjustment to direct buffer writes

### 3. Consider MAX_BLOBS Reduction (Medium Priority)
Reducing from 5 to 3 blobs could save ~45 μs per frame (16.5% overall).

**Action:**
- Measure visual impact (fewer simultaneous blobs)
- Run on hardware at various RPMs
- Decide if trade-off is acceptable

### 4. Implement Arm Rendering Helper (Low Priority)
3-arm loop structure is replicated in all 4 effects. Could extract to reduce duplication.

**Action:**
- Implement `renderArmsWithLambda()` helper
- Migrate effects one at a time
- Measure for lambda overhead (likely negligible)
- Reduce total effect code by ~50 lines

### 5. Hardware Validation (Critical)
All optimization measurements are theoretical or from old hardware runs.

**Action:**
- Deploy optimized firmware to actual POV display
- Test at 700, 1200, 1940, 2800 RPM
- Observe visual output for artifacts/gaps
- Verify gamma looks acceptable
- Compare performance to predictions

---

## Summary Tables

### Effect Performance Comparison (Current)

| Effect | Frame Time | Updates/degree | Ranking | Status |
|--------|-----------|-----------------|---------|--------|
| RpmArc | 88.7 μs | 0.34 | 1st | ✅ Acceptable |
| PerArmBlobs | 141.5 μs | 0.25 | 2nd | ⚠️ Marginal |
| SolidArms | 154.0 μs | 0.23 | 3rd | ⚠️ Marginal |
| VirtualBlobs | 275.2 μs | 0.16 | 4th | ❌ Slow |

### Hot Paths (Measurement Data)

| Function | Calls/frame | Time/call | Total | % of frame |
|----------|------------|----------|-------|-----------|
| `isAngleInArc()` | 15 | ~2 μs | 31 μs | 11.4% |
| `setPixelColorDirect()` | 30 | ~0.9 μs | 27 μs | 10.0% |
| `PHYSICAL_TO_VIRTUAL[]` lookup | 30 | ~0.9 μs | 26 μs | 9.6% |
| `isVirtualLedInBlob()` | 150 | ~0.15 μs | 22 μs | 8.1% |
| `blendAdditive()` | ~50 | ~0.14 μs | 7 μs | 2.6% |
| Loop/branch overhead | N/A | N/A | 143 μs | 52.8% |

### Code Metrics

| Metric | VirtualBlobs | PerArmBlobs | RpmArc | SolidArms |
|--------|------|-----|-------|-----|
| Lines of code | 382 | 162 | 134 | 143 |
| Blob checks/frame | 150 | 150 | 0 | 0 |
| Radial operations | Complex (wrap) | Simple (0-9) | N/A | N/A |
| Angle checks (before) | 150 | 150 | 3 | 3 |
| Angle checks (after) | 15 | 15 | 3 | 3 |

---

## Conclusion

All 4 effects follow recognizable patterns and have been optimized with hoisted angle checks and direct buffer access. VirtualBlobs remains the slowest (275.2 μs, 2.58x speedup from optimization baseline), while RpmArc is the fastest (88.7 μs). The 52.8% unmeasured overhead suggests further optimization would require algorithmic changes (fewer blobs, simpler logic) rather than micro-optimization.

The rendering pipeline is clean, well-structured, and shows clear separation of concerns (angle checks → radial checks → color composition → pixel writes). Code duplication is present but has not yet reached the "rule of three" threshold for extraction into shared helpers.

Performance is limited by the complexity of blob simulation (5 independent animated entities, each with 6 parameters driving position and size). Simplification to 3 blobs or removal of wraparound handling could yield additional gains if real hardware testing shows current performance is insufficient.

