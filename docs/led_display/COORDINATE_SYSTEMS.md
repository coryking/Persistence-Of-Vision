# POV Display Coordinate Systems and Globe Projection

This document captures the coordinate system design, projection math, and implementation insights developed for rendering globe/planet textures on the POV display.

## Table of Contents

1. [Display Hardware Geometry](#display-hardware-geometry)
2. [The Fundamental Challenge](#the-fundamental-challenge)
3. [Coordinate System Design](#coordinate-system-design)
4. [Angular Resolution](#angular-resolution)
5. [Polar-Native Textures](#polar-native-textures)
6. [Projection Math](#projection-math)
7. [What We Built](#what-we-built)
8. [Key Insights and Learnings](#key-insights-and-learnings)
9. [Current State](#current-state)
10. [Next Steps](#next-steps)

---

## Display Hardware Geometry

> **See also:** [HARDWARE.md](HARDWARE.md) for LED calibration details and physical measurements.

### Physical Layout

The POV display consists of 3 arms with LEDs, spinning around a central axis:

| Parameter | Value | Source |
|-----------|-------|--------|
| Total logical LEDs | 40 "rings" | `geometry.h` |
| Arms | 3 (interlaced) | ARM1: 13 LEDs, ARM2: 13 LEDs, ARM3: 14 LEDs |
| Innermost LED center | 10.0mm from axis | `ARM3_INNER_RADIUS_MM` |
| Outermost LED center | 101.0mm from axis | Calculated |
| LED pitch | 7.0mm center-to-center | `LED_PITCH_MM` |
| LED chip size | 5.0mm | `LED_CHIP_SIZE_MM` |
| Inner display edge | 7.5mm | `INNER_DISPLAY_RADIUS_MM` |
| Outer display edge | 103.5mm | `OUTER_DISPLAY_RADIUS_MM` |

### The "Donut Hole"

The display has a blind spot at the center — no LEDs exist at radii less than ~10mm, and the inner display edge is 7.5mm. This creates a "donut" topology:

```
        ┌─────────────────────────┐
        │                         │
        │    ┌───────────────┐    │
        │    │               │    │
        │    │   HOLE        │    │   Ring 0 starts here (~10mm)
        │    │   (r < 7.5mm) │    │
        │    │               │    │
        │    └───────────────┘    │
        │                         │
        └─────────────────────────┘
                Ring 39 (~101mm)
```

### Ring Radii (Non-Uniform Spacing)

Ring radii are NOT evenly spaced. **Ideally** they would be ~2.33mm apart (7mm LED pitch ÷ 3 arms), but in practice:

1. **Human manufacturing tolerance** — The LED strips were positioned by sighting them by hand, not precision machining
2. **3-arm interlacing** — Each arm has a different starting offset from the axis

```
Ring 0 (ARM3): 10.00mm
Ring 1 (ARM2): 13.10mm   (+3.10mm)
Ring 2 (ARM1): 15.10mm   (+2.00mm)
Ring 3 (ARM3): 17.00mm   (+1.90mm)
Ring 4 (ARM2): 20.10mm   (+3.10mm)
Ring 5 (ARM1): 22.10mm   (+2.00mm)
... pattern repeats ...
Ring 39 (ARM3): 101.00mm
```

The spacing alternates ~3.1mm, ~2.0mm, ~1.9mm. These values were calibrated by visual inspection (see [HARDWARE.md](HARDWARE.md) for calibration procedure).

---

## The Fundamental Challenge

### Cartesian vs Polar Mismatch

Source textures (equirectangular planet maps) are defined in **Cartesian** coordinates:
- X axis = longitude (0° to 360°)
- Y axis = latitude (-90° to +90°)

The POV display operates in **polar** coordinates:
- Radius (r) = distance from center (determined by which ring/LED)
- Angle (θ) = rotational position (determined by disc angle when LED fires)

**The challenge:** How do we map (latitude, longitude) on a sphere to (ring, angle) on the display?

### The Naive Approach (What We Tried First)

The CartesianGrid effect attempted to work in Cartesian space:

1. For each LED at (ring, angle), compute Cartesian (x, y):
   ```cpp
   x = radius_mm * cos(angle)
   y = radius_mm * sin(angle)
   ```
2. Use (x, y) to look up a texture

**Problems discovered:**
- Aliasing at 45° angles (the "X" pattern)
- Lines running "with the grain" (tangent to rings) need anti-aliasing
- Lines running "against the grain" (crossing rings) look clean
- The Cartesian intermediate fights the hardware's polar nature

---

## Coordinate System Design

### The "Grid Paper" Mental Model

We developed a conceptual Cartesian grid for understanding the display:

**Key decisions:**
- Origin (0, 0) at the true center of the display (even though it's in the hole)
- Pixel-centered approach (odd dimensions, center pixel exists)
- Grid cell size ≈ LED spacing (~2.33mm ideally)

**Dimensions:**
- ~44 "pixels" from center to edge (40 real LEDs + ~4 virtual "hole" pixels)
- Full grid: ~88 × 88 cells (from -44 to +44 in both axes)
- The hole occupies the center ~4 pixel radius

```
     -44        -4   0   +4        +44
      ├──────────┼───┼───┼──────────┤
      │          │ H │ H │          │
      │  LEDs    │ O │ O │   LEDs   │
      │  exist   │ L │ L │   exist  │
      │  here    │ E │ E │   here   │
      ├──────────┼───┼───┼──────────┤

     Ring 39    Ring 0  Ring 0    Ring 39
     (outer)    (inner) (inner)   (outer)
```

### Why Pixel-Centered?

Two options for the coordinate system:

1. **Pixel-centered (odd dimensions):** (0,0) is the center of the middle pixel
2. **Edge-aligned (even dimensions):** (0,0) is at the boundary between pixels

We chose **pixel-centered** because:
- The math `y = r × sin(θ)` naturally gives y=0 at the center
- If a center LED is added later (no hole), (0,0) becomes displayable without changing the coordinate system
- The hole is just "pixels we can't light up" — the coordinate system doesn't need to know

### Physical Measurement Validation

Testing the CartesianGrid effect:
- Grid spacing was set to 10 "pixels"
- Expected physical size: 10 × 2.3mm = 23mm
- Measured on spinning display: ~20-22mm ✓

The coordinate system mapping was validated.

---

## Angular Resolution

### Adaptive Resolution System

The display uses **adaptive angular resolution** based on render performance:

| Resolution | Degrees/Slot | Slots/Revolution |
|------------|--------------|------------------|
| 0.5° | Fine | 720 |
| 1.0° | | 360 |
| 1.5° | | 240 |
| 2.0° | | 180 |
| 3.0° | Default | 120 |
| 5.0° | | 72 |
| 6.0° | | 60 |
| ... | Coarse | ... |

The resolution is determined by: `max(render_time, output_time) / microseconds_per_degree`

### Impact on Cartesian Mapping

**Radial resolution is fixed:** 40 rings, always.

**Angular resolution varies:** 72 to 720 slots depending on code speed and RPM.

This creates asymmetric sampling:
- Near the outer edge (r=101mm), arc length between slots can be 8.8mm (at 72 slots) or 0.87mm (at 720 slots)
- Near the center (r=10mm), arc length is 10× smaller

**Key insight:** You cannot have "square pixels" everywhere. Near the center, angular pixels are compressed. Near the edge, they're stretched.

### The Lucky Coincidence

All the valid angular resolutions (720, 360, 240, 180, 144, 120, 90, 80, 72, 60...) divide evenly into 720!

This means:
- Store textures at 720 angular slots (maximum resolution)
- At lower angular resolution, we simply sample every Nth slot
- No explicit downsampling code needed — it's implicit

---

## Polar-Native Textures

### The Key Insight

Instead of storing textures in Cartesian format and converting at render time, **pre-compute textures in polar format** that matches the hardware:

**Polar texture format:**
- Width (720): Angular positions (0° to 359.5°)
- Height (44): Radial positions (center to edge)
- Row 0 = outer edge, Row 43 = center (or vice versa)

**Render loop becomes trivial:**
```cpp
int textureCol = arm.angleUnits / 5;  // 3600 units → 720 columns
int textureRow = ring_to_row(ring);
color = polar_texture[textureRow][textureCol];
```

No Cartesian math at render time. Just array lookup.

### Rotation is Trivial

To rotate the globe (e.g., Earth spinning):
```cpp
int rotatedCol = (textureCol + rotationOffset) % 720;
color = polar_texture[textureRow][rotatedCol];
```

Just an index offset — essentially free.

### Memory Budget

| Format | Size |
|--------|------|
| CRGB (3 bytes/pixel) | 720 × 44 × 3 = 95 KB per texture |
| Palette-indexed (1 byte/pixel) | 720 × 44 × 1 = 32 KB per texture |

Current implementation: 11 textures × 95 KB = ~1.05 MB (fits in flash)

---

## Projection Math

### North/South Pole View (Simple)

Looking straight down at a pole, the math is trivial:

```python
# North pole at center
latitude = 90.0 * row / (height - 1)        # 0° at edge, 90° at center
longitude = col * 360.0 / width              # 0° to 360°
```

- Center of display = pole (lat ±90°)
- Edge of display = equator (lat 0°)
- Disc angle = longitude (direct mapping!)

**Why this was our first implementation:** No spherical trigonometry required.

### Arbitrary Viewpoint (The Hard Stuff)

To center the view on any point (not just poles), we need the **azimuthal equidistant projection**:

```cpp
// Given: (ring, angle_slot) on display
// Given: (centerLat, centerLon) = point we're "looking at"
// Output: (lat, lon) on the sphere

// Ring → angular distance from center (0° at center, 90° at edge)
float angularDist = (ring / 39.0f) * (M_PI / 2.0f);

// Angle slot → azimuthal direction
float azimuth = angleUnitsToRadians(angleSlot);

// Spherical trigonometry
float lat = asin(
    sin(centerLat) * cos(angularDist) +
    cos(centerLat) * sin(angularDist) * cos(azimuth)
);

float lon = centerLon + atan2(
    sin(azimuth) * sin(angularDist),
    cos(centerLat) * cos(angularDist) - sin(centerLat) * sin(angularDist) * cos(azimuth)
);
```

This allows viewing Jupiter with bands horizontal (equator-centered) or any other orientation.

### Pre-compute vs Runtime

**Option 1: Pre-compute in Python**
- Generate polar textures for specific viewpoints
- ESP32 just does array lookup (fast)
- One texture per viewpoint (memory cost)

**Option 2: Compute at runtime**
- Full azimuthal projection in render()
- Can change viewpoint dynamically
- Heavier computation per frame

Current recommendation: Pre-compute for testing, runtime for final implementation if needed.

---

## What We Built

### 1. ProjectionTest Effect

> **Source:** [`led_display/src/effects/ProjectionTest.cpp`](../../led_display/src/effects/ProjectionTest.cpp)

**Purpose:** Validate that straight horizontal lines through the center look correct.

**Key learnings:**
- The "donut hole" is real and visible
- Anti-aliasing needed for lines tangent to rings ("with the grain")
- Lines perpendicular to rings ("against the grain") look clean
- Established the physical radius → Cartesian Y mapping

### 2. CartesianGrid Effect

> **Source:** [`led_display/src/effects/CartesianGrid.cpp`](../../led_display/src/effects/CartesianGrid.cpp)

**Purpose:** Validate the Cartesian coordinate system with a grid pattern.

**Key learnings:**
- The "X" pattern at 45° angles = aliasing in the transition zone
- Squinting/blurring makes it look correct (the concept works)
- Cartesian intermediate is fighting the polar hardware
- Led to the insight about polar-native textures

### 3. PolarGlobe Effect

> **Source:** [`led_display/src/effects/PolarGlobe.cpp`](../../led_display/src/effects/PolarGlobe.cpp)

**Purpose:** Display pre-computed polar textures of planets.

**Features:**
- 11 textures: Earth (day/night/clouds), Mars, Jupiter, Saturn, Neptune, Sun, Moon, Mercury, Makemake
- Up/down arrows cycle textures
- Rotation animation (longitude offset)
- Radial brightness compensation (25% center to 100% edge)
- Saturation/contrast boost for dull planets

**Files:**
- [`led_display/src/effects/PolarGlobe.cpp`](../../led_display/src/effects/PolarGlobe.cpp)
- [`led_display/include/effects/PolarGlobe.h`](../../led_display/include/effects/PolarGlobe.h)
- `led_display/src/textures/polar_*.h` (generated)
- [`tools/convert_to_polar.py`](../../tools/convert_to_polar.py)

---

## Key Insights and Learnings

### 1. The Display is Polar — Work With It

Fighting the polar nature with Cartesian intermediates creates problems (aliasing, coordinate conversion overhead). Pre-computing polar-native textures aligns with the hardware.

### 2. The "Grid Paper" is an Abstraction

The Cartesian grid (88×88) helps us reason about the display, but the actual rendering should use polar coordinates. The grid defines WHAT we want to show; the polar texture defines HOW we sample it.

### 3. Angular Resolution Varies — Design for Max

Store textures at 720 angular slots. Lower angular resolutions just sample fewer slots. The "downsampling" is implicit and free.

### 4. A Sphere is a Sphere

There's nothing special about "north pole" vs "equator" on a sphere. The projection math is the same for any center point — we just skipped the hard trig by starting with pole views.

### 5. The Hole is a Visible Feature

The center hole (~4 pixel radius in our grid, ~7.5mm physical) is a **visible feature of the display**, not an invisible point. Whatever you're projecting, the center portion cannot be displayed. For globe projections, this means the exact center of your viewpoint (and a small area around it) falls into the hole. This is a design constraint to work around, not ignore.

### 6. Rotation = Longitude Offset (Sometimes)

For pole views, rotation is trivially cheap (just shift the column index). For equator views, it's more complex — the visible hemisphere changes, not just the longitude.

---

## Current State

### What Works

- Pole view textures display correctly
- Earth is recognizable (continents, oceans visible from above)
- Rotation animation is smooth
- Multiple planets accessible via arrow keys
- Flash usage: 66% (1.1MB headroom remaining)

### What's Underwhelming

- **Pole views are the worst angle for most planets**
  - Jupiter's bands are iconic — but from above a pole, you just see concentric rings
  - Mars's features (Olympus Mons, Valles Marineris) are equatorial
  - The "recognizable" parts of planets are at their least visible

- **Most planets lack colorful contrast**
  - Mars is mostly orange/brown monochrome
  - Jupiter and Saturn are subtle tan/cream bands
  - The Moon and Mercury are grey
  - Only Earth has strong contrasting colors (blue oceans, green/brown land, white ice caps, tan deserts)

- **Earth is the exception** — pole view shows continents reasonably well AND has the color contrast to make features pop

### Open Questions

Is the underwhelming result because:
1. Pole view is wrong for these planets? (Fixable with equator view)
2. The display resolution (40 rings) is too low for recognizable features? (Fundamental limit)
3. Procedural generation would look better than real textures? (Different approach)
4. **Planets just aren't as good a subject as expected?** Earth works because of unique color contrast — maybe focus there instead of trying to make all planets work

---

## Next Steps

### Option A: Equator-Centered View (Test the Projection)

Modify `convert_to_polar.py` to support arbitrary center points:

```bash
python convert_to_polar.py jupiter.jpg polar_jupiter \
    --center-lat 0 --center-lon 0  # Equator view
```

This requires implementing the full azimuthal equidistant projection in Python.

If Jupiter with horizontal bands STILL doesn't look good, we know the limitation is fundamental (resolution/contrast) rather than projection.

### Option B: Focus on Earth (Play to Our Strengths)

Earth is the only planet with strong color contrast. Instead of trying to make all planets work, **double down on Earth**:

1. **Rotation** — Smooth Earth rotation is already working
2. **Day/night cycle** — Blend between day and night textures based on rotation phase, or darken half the globe
3. **Equator view** — Show Earth from the equator for a more "classic globe" look
4. **Cloud layer** — Overlay or blend the cloud texture
5. **Seasons** — Different lighting angles or ice cap sizes
6. **City lights** — Night texture shows population centers

This approach embraces what works rather than fighting what doesn't.

### Future Considerations

1. **Runtime projection** — Compute azimuthal projection on ESP32 for dynamic viewpoint changes

2. **Procedural effects** — Gas giant bands, storm patterns, aurora, etc. might look better than real textures at this resolution

3. **Palette-indexed storage** — Reduce texture memory by 3× if keeping multiple textures

---

## File Reference

| File | Purpose |
|------|---------|
| `led_display/include/geometry.h` | Physical dimensions, ring radii calculations |
| `led_display/src/effects/PolarGlobe.cpp` | Globe rendering effect |
| `led_display/src/effects/CartesianGrid.cpp` | Grid validation effect |
| `led_display/src/effects/ProjectionTest.cpp` | Band projection test |
| `led_display/src/textures/polar_*.h` | Pre-computed polar textures |
| `tools/convert_to_polar.py` | Equirectangular → polar converter |
| `docs/led_display/HARDWARE.md` | LED geometry and calibration |
| `docs/led_display/TIMING_ANALYSIS.md` | Angular resolution system |
