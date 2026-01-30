# Effect System Design

## Philosophy: Embrace Your Constraints

This POV display is **not** a rectangular framebuffer. It's three physical arms spinning through space at 2800 RPM. At any instant:

- **Arm 0** (inner) is at angle θ + 120°
- **Arm 1** (middle, hall trigger) is at angle θ
- **Arm 2** (outer) is at angle θ + 240°

Each arm has 11 radial LEDs. The "virtual 33-pixel radial line" is a useful fiction for some effects, but those 33 pixels are physically at three different angular positions right now.

**Previous approaches that failed:**
- POV_Top used triple-buffering and treated the display as X×Y pixels. This abstracted away the physical reality and never worked reliably.
- Per-pixel callbacks (`renderAllArms`) hid the buffer structure and encouraged per-pixel computation, leading to optimization hacks like `blob_cache`.

**This design:**
- Exposes the physical reality (3 arms, each with its own angle)
- Provides helpers for "virtual column" thinking when that's more natural
- Gives effects direct buffer access for maximum flexibility
- Makes timing/RPM information first-class

---

## The Spinning Brightness Floor (PWM + Rotation)

PWM dimming on a spinning display creates a unique visual constraint that affects all effects.

**How PWM works:** The LED isn't actually "dim" - it's fully bright (100%) for a shorter duration. A 10% brightness value means the LED is ON at full intensity for 10% of each PWM cycle, OFF for the remaining 90%.

**The problem with spinning:** On a stationary display, your eye integrates these rapid on/off pulses into perceived dimness. But on a spinning POV display, each "on" pulse happens at a slightly different angular position. At low brightness values:

- The "on" pulses are sparse enough that they don't blend together
- Instead of smooth dim color, you see discrete bright dots scattered across the arc
- The effect looks like bubbles, sparkles, or fireflies floating in the dark regions

**Key implications:**

1. **No smooth dark gradients** - You can't smoothly fade to black. At some threshold, the image breaks up from "solid color" into "sparkly dots"

2. **Even RGB(1,1,1) is visible** - It's not invisibly dark; it's noticeably bright but spotty. The LED is at full brightness during its brief "on" time.

3. **Dark-centered palettes work well** - When most noise values map to "dark," you get a field of subtle sparkles with occasional bright pops. This aesthetic works *with* the constraint rather than fighting it.

4. **High contrast is inherent** - The display naturally clips the dark end into discrete dots, so effects tend toward high contrast regardless of intent.

**Exploiting this effect:**

This isn't just a limitation - it's an artistic opportunity:

- **Firefly/bioluminescence effects** - Deliberately use low values to create floating point-sources
- **Starfield backgrounds** - The sparkle texture reads as distant stars or particles
- **Ember/spark effects** - Brief bright flashes against darkness
- **Noise-based textures** - Dark-centered palettes create organic, living textures

**Palette design guidance:**

See `docs/led_display/POV_Perlin_Noise_Color_Theory.md` for palette design strategies (dark-centered vs bright-centered) and noise distribution considerations.

---

## Core Types

### RenderContext

The context passed to every render call. Owns the pixel buffers.

```cpp
struct RenderContext {
    // === Timing ===
    uint32_t timeUs;              // Current timestamp (microseconds)
    interval_t microsPerRev;      // Microseconds per revolution (use directly, don't convert to RPM!)

    // === Speed Scaling ===
    // Use speedFactor8(microsPerRev) from polar_helpers.h for speed-based effects
    // Returns 0-255 (faster = higher). NO float division needed.

    // === The Three Arms (physical reality) ===
    struct Arm {
        angle_t angleUnits;       // THIS arm's angle in units (3600 = 360°)
        CRGB pixels[LEDS_PER_ARM]; // THIS arm's LEDs: [0]=hub, [10]=tip
    } arms[3];                    // [0]=inner(+120°), [1]=middle(0°), [2]=outer(+240°)

    // === Virtual Pixel Access ===
    // Virtual pixels 0-32 map to physical LEDs in radial order:
    //   virt 0 = arm0:led0 (innermost)
    //   virt 1 = arm1:led0
    //   virt 2 = arm2:led0
    //   virt 3 = arm0:led1
    //   ...
    //   virt 32 = arm2:led10 (outermost)

    CRGB& virt(uint8_t v) {
        return arms[v % 3].pixels[v / 3];
    }

    const CRGB& virt(uint8_t v) const {
        return arms[v % 3].pixels[v / 3];
    }

    // Fill virtual pixel range with solid color
    void fillVirtual(uint8_t start, uint8_t end, CRGB color) {
        for (uint8_t v = start; v < end && v < TOTAL_LEDS; v++) {
            virt(v) = color;
        }
    }

    // Fill virtual pixel range with gradient
    void fillVirtualGradient(uint8_t start, uint8_t end,
                             const CRGBPalette16& palette,
                             uint8_t paletteStart = 0,
                             uint8_t paletteEnd = 255) {
        if (end <= start) return;
        for (uint8_t v = start; v < end && v < TOTAL_LEDS; v++) {
            uint8_t palIdx = map(v - start, 0, end - start - 1, paletteStart, paletteEnd);
            virt(v) = ColorFromPalette(palette, palIdx);
        }
    }

    // Clear all pixels
    void clear() {
        for (auto& arm : arms) {
            memset(arm.pixels, 0, sizeof(arm.pixels));
        }
    }
};
```

### Effect Base Class

```cpp
class Effect {
public:
    virtual ~Effect() = default;

    // Called once when effect is activated
    virtual void begin() {}

    // Called once when effect is deactivated
    virtual void end() {}

    // Speed range this effect works well at (override for non-default)
    virtual SpeedRange getSpeedRange() const { return {0, 0}; }

    // THE MAIN WORK: Called for each render cycle
    virtual void render(RenderContext& ctx) = 0;

    // Called once per revolution (at hall sensor trigger)
    virtual void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {}
};
```

### Speed Range System

Effects declare their valid speed range via `getSpeedRange()`. The system automatically shuffles between effects valid for the current speed.

```cpp
struct SpeedRange {
    uint16_t minRPM = 0;    // 0 = works at any slow speed
    uint16_t maxRPM = 0;    // 0 = works at any fast speed

    bool contains(interval_t microsPerRev) const {
        if (minRPM > 0 && microsPerRev > RPM_TO_MICROS(minRPM)) return false;  // Too slow
        if (maxRPM > 0 && microsPerRev < RPM_TO_MICROS(maxRPM)) return false;  // Too fast
        return true;
    }
};
```

**Speed Ranges by Effect:**

| Effect | Min RPM | Max RPM | Notes |
|--------|---------|---------|-------|
| MomentumFlywheel | 10 | 200 | Hand-spin energy decay |
| PulseChaser | 10 | 200 | Hand-spin pulse trails |
| NoiseFieldRGB | 10 | 200 | Hand-spin noise |
| SolidArms | 10 | 3000 | Diagnostic, any speed |
| ArmAlignment | 10 | 3000 | Diagnostic, any speed |
| RpmArc | 200 | 3000 | Needs motor variation |
| PerArmBlobs | 200 | 3000 | Motor speed |
| VirtualBlobs | 200 | 3000 | Motor speed |
| NoiseField | 200 | 3000 | Motor speed |

**Behavior:**
- Threshold: 200 RPM (~300,000 µs/rev) separates "slow" from "motor" mode
- Shuffles every 20 seconds to a random effect valid for current speed
- When speed crosses threshold, switches to valid effect if current isn't

**RPM Conversion Macro:**
```cpp
#define RPM_TO_MICROS(rpm) (60000000ULL / (rpm))
```

**Adaptive Smoothing:**
Rolling average window scales linearly with speed:
- 2800 RPM → 20 samples (stability)
- 50 RPM → 2 samples (responsiveness)

---

## Polar Coordinate Helpers

These helpers make common angular/radial operations easy. They handle wraparound and edge cases.

### Angular Helpers

All angle helpers use **integer units** (3600 = 360°) for precision and speed.

```cpp
// Normalize angle units to 0-3599 range
inline angle_t normalizeAngleUnits(int32_t units) {
    int32_t normalized = units % ANGLE_FULL_CIRCLE;
    return normalized < 0 ? normalized + ANGLE_FULL_CIRCLE : normalized;
}

// Signed angular distance in units (-1800 to +1800)
inline int16_t angularDistanceUnits(angle_t from, angle_t to) {
    int32_t diff = static_cast<int32_t>(to) - static_cast<int32_t>(from);
    diff = ((diff % ANGLE_FULL_CIRCLE) + ANGLE_FULL_CIRCLE) % ANGLE_FULL_CIRCLE;
    return diff > ANGLE_HALF_CIRCLE ? diff - ANGLE_FULL_CIRCLE : diff;
}

// Is angle within arc? (all values in units)
inline bool isAngleInArcUnits(angle_t angle, angle_t center, angle_t width) {
    angle_t halfWidth = width / 2;
    angle_t dist = angularDistanceAbsUnits(center, angle);
    return dist <= halfWidth;
}

// Arc intensity: returns 0-255 for FastLED scale8 (edge=0, center=255)
inline uint8_t arcIntensityUnits(angle_t angle, angle_t center, angle_t width) {
    angle_t halfWidth = width / 2;
    angle_t dist = angularDistanceAbsUnits(center, angle);
    if (dist > halfWidth) return 0;
    return static_cast<uint8_t>(255 - (dist * 255UL / halfWidth));
}

// Speed factor: maps microsPerRev to 0-255 (faster = higher)
inline uint8_t speedFactor8(interval_t microsPerRev) {
    if (microsPerRev >= MICROS_PER_REV_MAX) return 0;
    if (microsPerRev <= MICROS_PER_REV_MIN) return 255;
    return (MICROS_PER_REV_MAX - microsPerRev) * 255 / (MICROS_PER_REV_MAX - MICROS_PER_REV_MIN);
}
```

### Radial Helpers

```cpp
// Is virtual pixel position within radial range?
inline bool isRadiusInRange(uint8_t virtualPos, uint8_t start, uint8_t end) {
    return virtualPos >= start && virtualPos < end;
}

// Normalized radius (0.0 = hub, 1.0 = tip)
inline float normalizedRadius(uint8_t virtualPos) {
    return static_cast<float>(virtualPos) / 29.0f;
}

// Virtual position from normalized radius
inline uint8_t virtualFromNormalized(float normalized) {
    return static_cast<uint8_t>(constrain(normalized * 29.0f, 0.0f, 29.0f));
}
```

### Virtual Column Helpers

For effects that want to think in "30-pixel virtual columns", iterate arms directly:

```cpp
// Check if ALL arms are within the target arc (units version)
bool allArmsInArc = true;
for (int a = 0; a < 3; a++) {
    if (!isAngleInArcUnits(ctx.arms[a].angleUnits, arcCenterUnits, arcWidthUnits)) {
        allArmsInArc = false;
        break;
    }
}

// Check if ANY arm is within the target arc
bool anyArmInArc = false;
for (int a = 0; a < 3; a++) {
    if (isAngleInArcUnits(ctx.arms[a].angleUnits, arcCenterUnits, arcWidthUnits)) {
        anyArmInArc = true;
        break;
    }
}

// Get intensity for each arm (0-255 for FastLED scale8)
uint8_t intensities[3];
for (int a = 0; a < 3; a++) {
    intensities[a] = arcIntensityUnits(ctx.arms[a].angleUnits, arcCenterUnits, arcWidthUnits);
}
```

---

## Example Effects

### RpmArc (Shape-based, Speed-aware)

```cpp
class RpmArc : public Effect {
private:
    // Arc width in angle units (3600 = 360°)
    static constexpr angle_t BASE_ARC_WIDTH_UNITS = 200;   // 20°
    static constexpr angle_t MAX_EXTRA_WIDTH_UNITS = 100;  // +10° at max speed

    CRGBPalette16 palette = LavaColors_p;

public:
    void render(RenderContext& ctx) override {
        ctx.clear();

        // Speed-based arc width: wider at higher speeds (lower microsPerRev)
        uint8_t speed = speedFactor8(ctx.microsPerRev);  // 0-255, faster=higher
        angle_t arcWidthUnits = BASE_ARC_WIDTH_UNITS + scale8(MAX_EXTRA_WIDTH_UNITS, speed);

        // Speed-based radial extent
        uint8_t radiusLimit = 1 + scale8(29, speed);

        // Process each arm independently
        for (int a = 0; a < 3; a++) {
            auto& arm = ctx.arms[a];

            // Check if THIS arm is in the arc (integer units)
            uint8_t intensity = arcIntensityUnits(arm.angleUnits, 0, arcWidthUnits);
            if (intensity == 0) continue;  // Skip this arm entirely

            // Draw gradient up to speed-based radius
            for (int p = 0; p < 10; p++) {
                uint8_t virtualPos = a + p * 3;
                if (virtualPos < radiusLimit) {
                    CRGB color = ColorFromPalette(palette, virtualPos * 8);
                    color.nscale8(intensity);  // Fade at arc edges
                    arm.pixels[p] = color;
                }
            }
        }
    }
};
```

### NoiseField (Per-pixel, uses arm angles)

```cpp
class NoiseField : public Effect {
private:
    uint32_t timeOffset = 0;
    CRGBPalette16 palette = LavaColors_p;

public:
    void render(RenderContext& ctx) override {
        timeOffset += 10;  // Animation speed

        for (int a = 0; a < 3; a++) {
            auto& arm = ctx.arms[a];

            // Use THIS arm's angle units for noise sampling
            // angleUnits is 0-3599, scale to 0-65535 for noise: *18
            uint16_t noiseX = arm.angleUnits * 18;

            for (int p = 0; p < 10; p++) {
                uint8_t virtualPos = a + p * 3;
                uint16_t noiseY = virtualPos * 2184;

                uint8_t brightness = inoise16(noiseX, noiseY, timeOffset) >> 8;
                arm.pixels[p] = ColorFromPalette(palette, brightness);
            }
        }
    }
};
```

### VirtualBlobs (Shape-first iteration)

```cpp
class VirtualBlobs : public Effect {
private:
    static constexpr int MAX_BLOBS = 5;
    Blob blobs[MAX_BLOBS];

public:
    void begin() override {
        initializeBlobs(blobs, MAX_BLOBS);
    }

    void onRevolution(float rpm) override {
        // Update blob animations once per revolution
        timestamp_t now = esp_timer_get_time();
        for (auto& blob : blobs) {
            updateBlobAnimation(blob, now);
        }
    }

    void render(RenderContext& ctx) override {
        ctx.clear();

        // Shape-first: iterate blobs, not pixels
        for (const auto& blob : blobs) {
            if (!blob.active) continue;

            // Check each arm against this blob's arc (integer units)
            for (int a = 0; a < 3; a++) {
                auto& arm = ctx.arms[a];

                if (!isAngleInArcUnits(arm.angleUnits,
                                        blob.currentStartAngleUnits,
                                        blob.currentArcSizeUnits)) {
                    continue;  // This arm isn't in the blob
                }

                // Draw blob's radial extent on this arm
                for (int p = 0; p < 10; p++) {
                    uint8_t virtualPos = a + p * 3;
                    if (isRadiusInRange(virtualPos, blob.radialStart, blob.radialEnd)) {
                        arm.pixels[p] += blob.color;  // Additive blend with qadd8
                    }
                }
            }
        }
    }
};
```

### SolidArms (Diagnostic, per-arm logic)

```cpp
class SolidArms : public Effect {
public:
    void render(RenderContext& ctx) override {
        for (int a = 0; a < 3; a++) {
            auto& arm = ctx.arms[a];

            // Determine pattern based on THIS arm's angle (integer units)
            // Each pattern is 18° = 180 units, so divide by ANGLE_PER_PATTERN
            uint8_t pattern = arm.angleUnits / ANGLE_PER_PATTERN;  // 0-19
            if (pattern > 19) pattern = 19;

            CRGB color = getPatternColor(pattern, a);
            bool striped = (pattern >= 4 && pattern <= 7);

            for (int p = 0; p < 10; p++) {
                if (striped && !(p == 0 || p == 4 || p == 9)) {
                    arm.pixels[p] = CRGB::Black;
                } else {
                    arm.pixels[p] = color;
                }
            }
        }
    }

private:
    CRGB getPatternColor(uint8_t pattern, uint8_t armIndex) {
        // ... pattern logic from original SolidArms.cpp ...
    }
};
```

---

## Wiring Into Main Loop

### 1. Create Effect Manager

```cpp
// EffectRegistry.h
#include "Effect.h"
#include "effects/RpmArc.h"
#include "effects/NoiseField.h"
#include "effects/VirtualBlobs.h"
#include "effects/SolidArms.h"

class EffectRegistry {
private:
    Effect* effects[4] = {
        new RpmArc(),
        new NoiseField(),
        new VirtualBlobs(),
        new SolidArms()
    };
    uint8_t currentIndex = 0;

public:
    Effect* current() { return effects[currentIndex]; }

    void next() {
        effects[currentIndex]->end();
        currentIndex = (currentIndex + 1) % 4;
        effects[currentIndex]->begin();
    }

    void begin() {
        effects[currentIndex]->begin();
    }
};
```

### 2. Update Main Loop

```cpp
// In main.cpp

EffectRegistry effects;
RenderContext ctx;

void setup() {
    // ... existing setup ...
    effects.begin();
}

void loop() {
    // ... existing timing/hall sensor code ...

    if (isWarmupComplete && isRotating) {
        // Populate context
        ctx.timeUs = now;
        ctx.microsPerRev = microsecondsPerRev;

        // Set arm angles (existing calculation)
        ctx.arms[0].angle = angleInner;   // Inner arm
        ctx.arms[1].angle = angleMiddle;  // Middle arm (hall trigger)
        ctx.arms[2].angle = angleOuter;   // Outer arm

        // Render current effect
        effects.current()->render(ctx);

        // Copy to strip (flatten arm buffers to physical layout)
        for (int a = 0; a < 3; a++) {
            uint16_t start = armStartIndex[a];  // 10, 0, 20 respectively
            for (int p = 0; p < 10; p++) {
                CRGB color = ctx.arms[a].pixels[p];
                color.nscale8(128);  // Power budget
                strip.SetPixelColor(start + p, RgbColor(color.r, color.g, color.b));
            }
        }

        strip.Show();
    }
}

// In hall processing task, notify effect of revolution
void hallProcessingTask(void* pvParameters) {
    while (1) {
        if (xQueueReceive(queue, &event, portMAX_DELAY) == pdPASS) {
            revTimer.addTimestamp(event.triggerTimestamp);

            // Notify current effect
            float rpm = 60000000.0f / revTimer.getMicrosecondsPerRevolution();
            effects.current()->onRevolution(rpm);
        }
    }
}
```

### 3. File Structure

```
include/
  Effect.h              # Effect base class
  RenderContext.h       # RenderContext struct
  polar_helpers.h       # Angular/radial helper functions
  EffectRegistry.h      # Effect management

src/effects/
  RpmArc.cpp           # Each effect in its own file
  NoiseField.cpp
  VirtualBlobs.cpp
  SolidArms.cpp
  PerArmBlobs.cpp
```

---

## Migration Checklist

For each existing effect:

1. [ ] Create class inheriting from `Effect`
2. [ ] Move initialization to `begin()`
3. [ ] Convert render function to `render(RenderContext& ctx)`
4. [ ] Replace `renderAllArms` callback with direct arm iteration
5. [ ] Use `ctx.arms[a].angle` instead of `arm.angle` from callback
6. [ ] Use `ctx.arms[a].pixels[p]` instead of `ctx.leds[physicalLed]`
7. [ ] Move per-revolution updates to `onRevolution()` if applicable
8. [ ] Remove blob_cache usage (no longer needed with shape-first iteration)

---

## Key Differences from Old System

| Old Pattern | New Pattern |
|-------------|-------------|
| `renderAllArms(ctx, lambda)` | Direct iteration: `for (auto& arm : ctx.arms)` |
| `ctx.leds[physicalLed]` | `ctx.arms[a].pixels[p]` or `ctx.virt(v)` |
| `arm.angle` from callback | `ctx.arms[a].angle` |
| `blob_cache` for performance | Shape-first iteration (no cache needed) |
| Free functions | Effect classes with lifecycle |
| Hidden buffer | Explicit `ctx.arms[].pixels[]` ownership |

---

## Future Considerations

### Timing Synchronization

The current system renders "as fast as loop() runs." A future enhancement could:
- Fire renders at specific angular intervals (e.g., every 1°)
- Align render timing to hall sensor trigger
- Provide consistent angular resolution across RPM ranges

This is orthogonal to the Effect API - effects don't need to change.

### Additional Helpers

As patterns emerge, consider adding:
- Spiral drawing helpers
- Radial gradient utilities
- Animation easing functions
- Color palette cycling utilities

### Performance Profiling

With the new system, profile:
- `render()` duration per effect
- Compare against old per-pixel callback overhead
- Validate blob_cache removal doesn't regress performance
