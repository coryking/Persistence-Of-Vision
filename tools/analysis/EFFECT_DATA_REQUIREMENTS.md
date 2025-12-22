# Effect Data Requirements Analysis

## Overview

This document analyzes what data each effect actually **needs** to render correctly vs what it's **given** by the RenderContext. It focuses on precision requirements, boundary sensitivity, and potential sources of visual glitches.

**Context Provided:**
- `ctx.timeUs` - Current timestamp (microseconds)
- `ctx.microsPerRev` - Microseconds per revolution
- `ctx.arms[3].angle` - **Each arm's current angle (0-360°)**
- `ctx.arms[3].pixels[10]` - 10 LEDs per arm to write to
- Convenience methods: `ctx.rpm()`, `ctx.degreesPerRender()`

---

## 1. SolidArms.cpp - Diagnostic Pattern Detector

**Purpose:** Display 20 discrete angular zones (18° each) as a diagnostic tool. Shows pattern boundaries clearly.

### Data Contract

**What it receives:**
```
ctx.arms[a].angle - Float angle in 0-360° range
```

**What it actually uses:**
1. `normalizeAngle(arm.angle)` → Maps to 0-360°
2. Division: `pattern = angle / 18.0f` → Maps 0-360° to 0-19 pattern index
3. Reference marker: checks if `angle < 3.0f || angle > 357.0f` for 0° line

**Precision required:**
- **Pattern determination:** Integer degrees sufficient
  - Each pattern is 18° wide
  - Off-by-1° in angle = within same pattern (invisible)
  - Off-by-9° in angle = might cross pattern boundary
- **Reference marker:** Sub-degree precision needed
  - Currently flags 3° window around 0° (generous)
  - Could work with ±2° without visual issue

### Boundary Sensitivity Analysis

**Pattern boundaries occur at:** 0°, 18°, 36°, 54°, 72°, 90°, 108°, 126°, 144°, 162°, 180°, 198°, 216°, 234°, 252°, 270°, 288°, 306°, 324°, 342°

**What happens at boundaries:**
- If angle jitter causes flickering between patterns N and N+1:
  - Visual: Arm color flickers between two colors on same render
  - Symptom: Arm appears to strobe rather than being solid
  - Sensitivity: **Jitter >0.5° becomes visible**

**Problematic boundary (documented in code):**
- 288° boundary (patterns 15→16) is explicitly logged
- Code has `BOUNDARY_EPSILON = 0.5f` for detecting near-boundary conditions
- If angle bounces near 288°, expect pattern flickering logs

**If angle is off by:**
- ±1° → Same pattern, invisible
- ±5° → Still same pattern, invisible
- ±9° → Boundary: flicker between patterns
- ±18° → Different pattern (off by one full zone)

### Jitter Sensitivity Zones

This effect is **MOST SENSITIVE** around pattern boundaries. At 18° boundaries:
- Jitter >0.5° visible as flickering
- At 288° specifically: logged as `FLICKER@288` events

### Implementation Notes

- Uses `float` angle but pattern is determined by integer division
- Has detailed timing diagnostics enabled by `ENABLE_DETAILED_TIMING` flag
- Tracks pattern changes per arm to detect flickering
- Reference marker uses both white (0-3°) and orange (357-360°) to test boundary wraparound

---

## 2. PerArmBlobs.cpp - Per-Arm Lava Lamp

**Purpose:** Create 5 blobs (colored wedges) distributed across 3 arms. Each blob belongs to one arm.

### Data Contract

**What it receives:**
```
ctx.arms[a].angle     - Angle of arm A at this moment
ctx.clear()           - Clears all pixels (called each render)
```

**What it actually uses:**
1. **Angular:** `isAngleInArc(arm.angle, blob.currentStartAngle, blob.currentArcSize)`
   - Checks if THIS arm's angle falls within the blob's angular wedge
   - Uses `angularDistanceAbs()` internally (handles 360° wraparound)
2. **Radial:** Range checks on pixel indices (0-9 per arm)
   - Checks if `pixelPos >= radialStart && pixelPos <= radialEnd`
   - Pure integer comparison after float center/extent calculation

### Precision Required

**Angular precision:**
- Blob has `currentStartAngle` and `currentArcSize` (both float, degrees)
- Blob's arc can be 5-90° wide depending on initialization
- Typical: "is arm at 45°?" with blob arc 10-60° wide
- **Sub-degree precision NOT needed** - a 10° wide blob with 1° angle error is still invisible
- **BUT:** For narrow blobs (5° arc), error >2-3° becomes noticeable

**Radial precision:**
- Radial position is `float` (0-9 LED range)
- Rendered as range: `pixelStart <= pixelPos <= pixelEnd`
- **Integer precision sufficient** - LEDs are discrete
- Fractional positions blur naturally (pixel at 3.5 lights both 3 and 4)

### Boundary Sensitivity

**Angular boundaries:**
- Blob boundaries determined by `arcIntensity()` → linear fade
- No hard edge, so no sharp flickering
- Boundary crossing = smooth fade in/out as arm rotates through arc
- **Not sensitive** to small angle jitter because additive blending smooths it

**Radial boundaries:**
- Hard edges: `pixelPos >= radialStart && pixelPos <= radialEnd`
- If blob center drifts across LED boundary (3.9 → 4.1), pixel 3 suddenly disappears
- **Sensitive** if radial position has jitter >0.5 LEDs (about 5%)

### Jitter Impact

**If angle has ±1° jitter:**
- For 30° arc blob: invisible (3% of arc width)
- For 5° arc blob: noticeable shimmer during entry/exit
- **Typical blobs (10-60°): invisible**

**If angle has ±5° jitter:**
- For 30° arc: subtle shimmer (17% of arc)
- For 5° arc: blob appears to pulse in/out
- **Problematic for narrow blobs**

**If radial position has jitter ±2 LEDs:**
- Blob appears to vibrate radially
- Visible as LED flicker if at boundary

### Animation Details

- Blobs update via `onRevolution()` (once per revolution, ~47 Hz at 2800 RPM)
- Uses time-based sine waves for smooth drift/breathing
- Animation state stored in blob, angle error only affects which pixels render

### Implementation Notes

- Shape-first rendering: iterates blobs, then checks if each arm is in arc
- Uses `isAngleInArc()` helper with wraparound handling
- Additive blending (`+=`) provides visual smoothing at overlaps
- 5 blobs distributed: 2 inner (arm 0), 2 middle (arm 1), 1 outer (arm 2)

---

## 3. VirtualBlobs.cpp - Virtual Space Blobs

**Purpose:** Create blobs in unified 30-row virtual space. Blobs can appear on multiple arms simultaneously.

### Data Contract

**What it receives:**
```
ctx.arms[a].angle     - Angle of arm A
ctx.clear()           - Clears all pixels
```

**What it actually uses:**
1. **Angular:** `isAngleInArc(arm.angle, blob.currentStartAngle, blob.currentArcSize)`
   - Checks if any arm is in blob's angular region
   - Creates "virtual column" effect: all three arms lit when rotating through arc
2. **Radial:** Virtual pixel mapping (0-29)
   - `virtualPos = a + p * 3` (maps arm/LED to virtual pixel)
   - Range check: `vPos >= radialStart && vPos <= radialEnd`

### Precision Required

**Angular precision:**
- Same as PerArmBlobs
- Blob arcs: 5-90° wide
- **Sub-degree NOT needed** for typical (10-60°) blobs
- Narrow (5°) blobs: ±1-2° error becomes noticeable

**Radial precision:**
- Virtual position floating point (0-29)
- More forgiving than per-arm because range is 3× wider
- Example: pixel 15 is center; 1 LED error = 3.3% of virtual space vs 10% of per-arm space
- **Less sensitive than PerArmBlobs**

### Boundary Sensitivity

**Visibility difference from PerArmBlobs:**
- **Wider radial range** (0-29 vs 0-9) means same absolute angle error is smaller fraction
- **Multiple arms** see blob simultaneously, so flickering would appear on all 3 arms at once
- Hard radial boundaries exist, but blob typically 6-14 LEDs wide (larger than per-arm)

**Angular boundaries:**
- Same smooth fade behavior (no sharp edges)
- All three arms see blob simultaneously = unified effect

**If angle has ±1° jitter:**
- For 30° arc: invisible (3% of arc)
- Multiplied by 3 arms = no shimmer
- **Very robust**

**If angle has ±5° jitter:**
- For 30° arc: subtle shimmer (17% of arc)
- **Still less noticeable than per-arm** because all 3 arms shimmer together

### Key Difference from PerArmBlobs

Virtual blobs can illuminate 3 arms simultaneously. This means:
- If angle jitter causes the arc to oscillate, **all three arms blink in sync**
- This is actually less visually jarring than one arm flickering alone
- Creates a unified "pulse" rather than arm-specific strobing

### Implementation Notes

- Each blob checks all 3 arms (line 26: `for (int a = 0; a < 3; a++)`)
- Virtual pixel mapping: `virtualPos = a + p * 3`
- Larger radial extents: 2-6 / 4-10 / 6-14 LEDs vs per-arm 1-3 / 2-5 / 3-7
- Same additive blending for smooth overlaps
- Immortal blobs (no lifecycle management)

---

## 4. RpmArc.cpp - RPM Indicator Arc

**Purpose:** Display growing arc at fixed angular position (0°). Arc width = function of RPM.

### Data Contract

**What it receives:**
```
ctx.arms[a].angle           - Angle of arm A
ctx.rpm()                   - RPM calculated from ctx.microsPerRev
ctx.degreesPerRender()      - Degrees per render (for reference)
```

**What it actually uses:**
1. **Angular:** `arcIntensity(arm.angle, ARC_CENTER=0.0f, arcWidth)`
   - Checks how centered each arm is on 0° position
   - Returns intensity: 0.0 (outside) to 1.0 (at center)
   - Linear fade at edges
2. **RPM timing:** `rpmToPixelCount(rpm)`
   - Converts RPM to LED count (1-30)
   - Used to determine which radial pixels light up
   - Linear interpolation: `(rpm - RPM_MIN) / (RPM_MAX - RPM_MIN)`

### Precision Required

**Angular precision:**
- Arc centered at 0° with width 20° (adjustable)
- `arcIntensity()` uses `angularDistanceAbs()` for comparison
- Fade zone from 10° on each side of 0° (linear fade)
- **Sub-degree precision beneficial** for smooth fade
- ±1° error = 5% change in intensity (subtle but visible)
- ±5° error = 25% intensity change (noticeable)

**RPM precision:**
- RPM calculated from `microsPerRev` (microseconds per revolution)
- Conversion: `rpm = 60,000,000 / microsPerRev`
- RPM used as lookup in range [800, 2500]
- **Very sensitive** to `microsPerRev` accuracy
- Example at 2000 RPM (30,000 µs/rev):
  - Error ±1000 µs = ±67 RPM error
  - This changes LED count by ~2 pixels (4% of range)
  - Visible as growth/shrink jitter

### Boundary Sensitivity

**Angular boundary at 0° with ±10° width:**
- Hard numerical boundary at 10° / -10° from 0°
- Intensity fade is continuous (linear)
- **NOT sharp** - smooth intensity curve
- But the presence/absence of arc at the boundary IS sharp

**Time-based sensitivity:**
- Arc position is FIXED (0°)
- But arms rotate continuously through it
- If microsPerRev has timing jitter, RPM calculation fluctuates
- RPM jitter causes LED count to dance (visible growth/shrink)

**If angle has ±1° jitter at 0° boundary:**
- Arm at -8° with jitter to -7°:
  - Intensity at -8°: `1.0 - (8/10) = 0.2`
  - Intensity at -7°: `1.0 - (7/10) = 0.3`
  - 50% brightness change **VISIBLE**
- This is the most angle-sensitive effect

**If RPM timing has ±100µs jitter (at 30,000 µs/rev):**
- RPM fluctuates ±200 RPM
- LED count change: ~1.5 pixels
- Arc grows/shrinks noticeably

### Critical Sensitivity Analysis

**This effect is MOST SENSITIVE to:**
1. **`microsPerRev` timing accuracy** - RPM directly controls arc size
2. **Angle accuracy AT the 0° arc center** - Fade zone is narrow (±10°)

Specific vulnerability:
```
At 0° - 10°: intensity = 0.0
At 0° - 9°: intensity = 0.1
At 0° - 5°: intensity = 0.5
At 0°:      intensity = 1.0
```

If angle has ±2° jitter and arm is near 0°, intensity swings 20% → very visible

### Implementation Notes

- `ARC_CENTER = 0.0f` is fixed (hall sensor position)
- `BASE_ARC_WIDTH = 20.0f` (future: can be animated)
- `RPM_MIN = 800`, `RPM_MAX = 2500`
- Gradient pre-computed: green (inner) → red (outer)
- Uses additive color scale: `color.nscale8(intensity * 255)`
- **No per-arm-specific math** - all arms see same arc center

---

## 5. NoiseField.cpp - Perlin Noise Texture

**Purpose:** Create organic flowing lava-like patterns using 3D Perlin noise.

### Data Contract

**What it receives:**
```
ctx.arms[a].angle     - Angle of arm A (CRITICAL - directly used for noise X)
timeOffset            - Animation time (incremented each render)
```

**What it actually uses:**
1. **Angle for noise X coordinate:**
   ```cpp
   uint16_t noiseX = static_cast<uint16_t>(arm.angle * 182.0f);
   ```
   - Conversion factor: 182 = 65536 / 360
   - Maps 0-360° → 0-65535 (full noise space)
   - **Direct linear mapping**
2. **Radial for noise Y coordinate:**
   ```cpp
   uint16_t noiseY = virtualPos * 2184;
   ```
   - Conversion factor: 2184 = 65536 / 30
   - Maps 0-29 virtual pixels → 0-65535 (full noise space)
3. **Time for noise Z:**
   - `timeOffset` incremented by `ANIMATION_SPEED = 10` each render
   - Creates temporal dimension of noise field

### Precision Required

**Angular precision - CRITICAL:**
- Angle directly multiplied by 182.0 for noise lookup
- Precision: **Sub-degree matters**
- Noise sampling resolution: 182 units/degree
  - ±0.5° error = ±91 noise units (out of 65535 = ±0.14% error)
  - ±1.0° error = ±182 noise units (±0.28% error)
  - ±2.0° error = ±364 noise units (±0.56% error)

**Perlin noise characteristics:**
- Smooth interpolation between sample points
- **Angle error of ±1° causes subtle hue shift** (not sharp boundary)
- Noise oscillates continuously, so error is averaged out over render

**If angle has ±1° jitter:**
- Noise color shifts subtly per frame
- Creates "shimmer" effect rather than flickering
- At 2800 RPM with 50µs render: ~5 renders per degree at edge
- Jitter causes color to oscillate smoothly (might look intentional)

**If angle has ±5° jitter:**
- Noise jumps noticeably (5% of full radial range)
- Lava pattern appears to shimmer/pulse
- **Visible as color instability**

### Boundary Sensitivity

**360° wraparound:**
- Angle normalization: handled by RenderContext
- Noise wraps correctly: 0° and 360° both map to similar noise values
- **No sharp boundary issues**

**Arm stagger (120° apart):**
- Arm 0 at 0°, arm 1 at 120°, arm 2 at 240°
- Each arm samples different part of noise field
- This is intentional and creates natural variation
- **Not sensitive** to small arm angle differences

### Visual Behavior Under Jitter

The effect is **LEAST SENSITIVE** to angle jitter because:
1. Perlin noise naturally interpolates smoothly
2. Lava-like patterns are already random-looking
3. Angle jitter creates color shimmer, which is organic
4. No hard boundaries or sharp features

**However:**
- Regular jitter pattern could create beat frequencies
- If jitter occurs at fixed frequency and noise sample rate aligns, could create visible banding
- Unlikely in practice due to noise complexity

### Implementation Notes

- Uses FastLED's `inoise16()` for performance (fixed-point 16-bit math)
- Conversion factors optimized: `182.0f = 65536/360`, `2184 = 65536/30`
- Each arm's noise X is independent (respects arm position)
- Virtual pixel mapping ensures radial continuity (0=hub, 29=tip)
- Time animation decoupled from rotation (purely elapsed time)
- Color mapping: `ColorFromPalette(LavaColors_p, brightness)`

---

## Summary: Sensitivity Ranking

**MOST SENSITIVE (sub-degree precision critical):**
1. **RpmArc** - Arc intensity changes 10% per degree near center
   - Impact: Arc fade zone flickers if angle jittery near 0°
2. **SolidArms** - Pattern flickering at 18° boundaries
   - Impact: Color strobing if jitter >0.5° at boundaries

**MODERATELY SENSITIVE (precision beneficial but not critical):**
3. **PerArmBlobs** - Especially narrow blobs (5° arc)
   - Impact: Blob edges shimmer if angle jittery
4. **VirtualBlobs** - Similar to per-arm but more robust (3× arm overlap)
   - Impact: Less noticeable because shimmer is unified across arms

**LEAST SENSITIVE (robust to jitter):**
5. **NoiseField** - Smooth interpolation, organic appearance
   - Impact: Slight color shimmer might look intentional

---

## Timing Context

From `RenderContext`:
```cpp
float degreesPerRender(uint32_t renderTimeUs) const {
    // At 50µs render, 2800 RPM: ~0.84° per render
    // At 50µs render, 700 RPM:  ~0.21° per render
}
```

**If angle has jitter matching render frequency:**
- Could cause per-render angle oscillation
- SolidArms/RpmArc would show maximum sensitivity
- PerArmBlobs/VirtualBlobs smoothed by additive blending
- NoiseField would create organic shimmer

---

## Recommendations for Validation

**Test jitter impact:**
1. Inject known angle error patterns and observe visual artifacts
2. Use SolidArms as diagnostic (pattern flickering is obvious)
3. Watch RpmArc arc intensity at 0° boundary (most sensitive spot)
4. Check PerArmBlobs narrow blob edges for strobing

**Measure what you actually need:**
- Use ENABLE_DETAILED_TIMING in SolidArms to catch boundary crossings
- Log pattern change frequency and angle values
- Correlate with hall sensor interrupt timing

**Rule of thumb:**
- Angle jitter should be <1° for imperceptible effects
- Angle jitter 1-3° visible on sensitive effects (SolidArms, RpmArc)
- Angle jitter >5° becomes obvious on all effects

