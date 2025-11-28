# Effect System Design

## Philosophy: Embrace Your Constraints

This POV display is **not** a rectangular framebuffer. It's three physical arms spinning through space at 2800 RPM. At any instant:

- **Arm 0** (inner) is at angle θ + 120°
- **Arm 1** (middle, hall trigger) is at angle θ
- **Arm 2** (outer) is at angle θ + 240°

Each arm has 10 radial LEDs. The "virtual 30-pixel radial line" is a useful fiction for some effects, but those 30 pixels are physically at three different angular positions right now.

**Previous approaches that failed:**
- POV_Top used triple-buffering and treated the display as X×Y pixels. This abstracted away the physical reality and never worked reliably.
- Per-pixel callbacks (`renderAllArms`) hid the buffer structure and encouraged per-pixel computation, leading to optimization hacks like `blob_cache`.

**This design:**
- Exposes the physical reality (3 arms, each with its own angle)
- Provides helpers for "virtual column" thinking when that's more natural
- Gives effects direct buffer access for maximum flexibility
- Makes timing/RPM information first-class

---

## Core Types

### RenderContext

The context passed to every render call. Owns the pixel buffers.

```cpp
struct RenderContext {
    // === Timing ===
    uint32_t timeUs;              // Current timestamp (microseconds)
    interval_t microsPerRev;      // Microseconds per revolution

    // === Convenience Methods ===
    float rpm() const {
        return 60000000.0f / static_cast<float>(microsPerRev);
    }

    // Angular resolution: how many degrees does one render cover?
    // At 2800 RPM with 50µs renders: ~0.84° per render
    // At 700 RPM with 50µs renders: ~0.21° per render
    float degreesPerRender(uint32_t renderTimeUs) const {
        float revsPerMicro = 1.0f / static_cast<float>(microsPerRev);
        return renderTimeUs * revsPerMicro * 360.0f;
    }

    // === The Three Arms (physical reality) ===
    struct Arm {
        float angle;              // THIS arm's current angle (0-360°)
        CRGB pixels[10];          // THIS arm's LEDs: [0]=hub, [9]=tip
    } arms[3];                    // [0]=inner(+120°), [1]=middle(0°), [2]=outer(+240°)

    // === Virtual Pixel Access ===
    // Virtual pixels 0-29 map to physical LEDs in radial order:
    //   virt 0 = arm0:led0 (innermost)
    //   virt 1 = arm1:led0
    //   virt 2 = arm2:led0
    //   virt 3 = arm0:led1
    //   ...
    //   virt 29 = arm2:led9 (outermost)

    CRGB& virt(uint8_t v) {
        return arms[v % 3].pixels[v / 3];
    }

    const CRGB& virt(uint8_t v) const {
        return arms[v % 3].pixels[v / 3];
    }

    // Fill virtual pixel range with solid color
    void fillVirtual(uint8_t start, uint8_t end, CRGB color) {
        for (uint8_t v = start; v < end && v < 30; v++) {
            virt(v) = color;
        }
    }

    // Fill virtual pixel range with gradient
    void fillVirtualGradient(uint8_t start, uint8_t end,
                             const CRGBPalette16& palette,
                             uint8_t paletteStart = 0,
                             uint8_t paletteEnd = 255) {
        if (end <= start) return;
        for (uint8_t v = start; v < end && v < 30; v++) {
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
    // Use for one-time initialization, loading palettes, etc.
    virtual void begin() {}

    // Called once when effect is deactivated (switching to another effect)
    virtual void end() {}

    // THE MAIN WORK: Called for each render cycle
    // Write to ctx.arms[].pixels[] or use ctx.virt()
    virtual void render(RenderContext& ctx) = 0;

    // Called once per revolution (at hall sensor trigger)
    // Natural place for per-revolution state updates
    // rpm: current revolutions per minute
    virtual void onRevolution(float rpm) {}
};
```

---

## Polar Coordinate Helpers

These helpers make common angular/radial operations easy. They handle wraparound and edge cases.

### Angular Helpers

```cpp
// Normalize angle to 0-360 range
inline float normalizeAngle(float angle) {
    angle = fmod(angle, 360.0f);
    return angle < 0 ? angle + 360.0f : angle;
}

// Signed angular distance from 'from' to 'to' (-180 to +180)
inline float angularDistance(float from, float to) {
    float diff = normalizeAngle(to - from);
    return diff > 180.0f ? diff - 360.0f : diff;
}

// Is angle within arc centered at 'center' with given 'width'?
// Handles 360° wraparound correctly
inline bool isAngleInArc(float angle, float center, float width) {
    float halfWidth = width / 2.0f;
    float dist = fabsf(angularDistance(center, angle));
    return dist <= halfWidth;
}

// How far into the arc is this angle? (0.0 = edge, 1.0 = center)
// Useful for soft-edge effects
inline float arcIntensity(float angle, float center, float width) {
    float halfWidth = width / 2.0f;
    float dist = fabsf(angularDistance(center, angle));
    if (dist > halfWidth) return 0.0f;
    return 1.0f - (dist / halfWidth);
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

For effects that want to think in "30-pixel virtual columns":

```cpp
// Check if ALL arms are within the target arc
// Use when you want the virtual column to appear as a unit
inline bool isVirtualColumnInArc(const RenderContext& ctx,
                                  float arcCenter,
                                  float arcWidth) {
    for (int a = 0; a < 3; a++) {
        if (!isAngleInArc(ctx.arms[a].angle, arcCenter, arcWidth)) {
            return false;
        }
    }
    return true;
}

// Check if ANY arm is within the target arc
// Use when partial visibility is okay
inline bool isAnyArmInArc(const RenderContext& ctx,
                          float arcCenter,
                          float arcWidth) {
    for (int a = 0; a < 3; a++) {
        if (isAngleInArc(ctx.arms[a].angle, arcCenter, arcWidth)) {
            return true;
        }
    }
    return false;
}

// Get intensity for each arm based on arc position
// Returns array of 3 intensities (0.0-1.0)
inline void getArmIntensities(const RenderContext& ctx,
                               float arcCenter,
                               float arcWidth,
                               float intensities[3]) {
    for (int a = 0; a < 3; a++) {
        intensities[a] = arcIntensity(ctx.arms[a].angle, arcCenter, arcWidth);
    }
}
```

---

## Example Effects

### RpmArc (Shape-based, RPM-aware)

```cpp
class RpmArc : public Effect {
private:
    static constexpr float RPM_MIN = 800.0f;
    static constexpr float RPM_MAX = 2500.0f;
    static constexpr float BASE_ARC_WIDTH = 20.0f;

    CRGBPalette16 palette = LavaColors_p;
    float arcWidth = BASE_ARC_WIDTH;

    uint8_t rpmToRadius(float rpm) const {
        float clamped = constrain(rpm, RPM_MIN, RPM_MAX);
        float normalized = (clamped - RPM_MIN) / (RPM_MAX - RPM_MIN);
        return 1 + static_cast<uint8_t>(normalized * 29.0f);
    }

public:
    void render(RenderContext& ctx) override {
        ctx.clear();

        // Animate arc width based on RPM (wider at higher RPM)
        arcWidth = BASE_ARC_WIDTH + 10.0f * (ctx.rpm() / 2800.0f);
        uint8_t radiusLimit = rpmToRadius(ctx.rpm());

        // Process each arm independently
        for (int a = 0; a < 3; a++) {
            auto& arm = ctx.arms[a];

            // Check if THIS arm is in the arc
            float intensity = arcIntensity(arm.angle, 0.0f, arcWidth);
            if (intensity == 0.0f) continue;  // Skip this arm entirely

            // Draw gradient up to RPM-based radius
            for (int p = 0; p < 10; p++) {
                uint8_t virtualPos = a + p * 3;
                if (virtualPos < radiusLimit) {
                    CRGB color = ColorFromPalette(palette, virtualPos * 8);
                    // Optional: fade at arc edges
                    color.nscale8(static_cast<uint8_t>(intensity * 255));
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

            // Use THIS arm's actual angle for noise sampling
            uint16_t noiseX = static_cast<uint16_t>(arm.angle * 182.0f);

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
        uint32_t now = esp_timer_get_time();
        for (auto& blob : blobs) {
            updateBlobAnimation(blob, now);
        }
    }

    void render(RenderContext& ctx) override {
        ctx.clear();

        // Shape-first: iterate blobs, not pixels
        for (const auto& blob : blobs) {
            if (!blob.active) continue;

            // Check each arm against this blob's arc
            for (int a = 0; a < 3; a++) {
                auto& arm = ctx.arms[a];

                if (!isAngleInArc(arm.angle, blob.centerAngle, blob.arcWidth)) {
                    continue;  // This arm isn't in the blob
                }

                // Draw blob's radial extent on this arm
                for (int p = 0; p < 10; p++) {
                    uint8_t virtualPos = a + p * 3;
                    if (isRadiusInRange(virtualPos, blob.radialStart, blob.radialEnd)) {
                        arm.pixels[p] += blob.color;  // Additive blend
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

            // Determine pattern based on THIS arm's angle
            float normAngle = normalizeAngle(arm.angle);
            uint8_t pattern = static_cast<uint8_t>(normAngle / 18.0f);
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
