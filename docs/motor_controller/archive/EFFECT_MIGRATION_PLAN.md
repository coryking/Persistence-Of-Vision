# Effect System Migration Plan

## Current State

The new effect system infrastructure exists but isn't wired in:

| File | Status | Notes |
|------|--------|-------|
| `include/Effect.h` | Ready | Base class with lifecycle methods |
| `include/RenderContext.h` | OLD | Still uses flat angles + `CRGB* leds` |
| `include/RenderContext.h.new` | Ready | New design with `arms[]` struct |
| `include/polar_helpers.h` | Ready | Angular/radial helpers |
| `include/EffectRegistry.h` | Missing | Need to create |
| `src/effects/*.cpp` | OLD | All use old callback pattern |

---

## Phase 1: Swap RenderContext

**Task:** Replace old RenderContext with new one.

```bash
# Backup old version
mv include/RenderContext.h include/RenderContext.h.old

# Activate new version
mv include/RenderContext.h.new include/RenderContext.h
```

**Impact:** This will break all existing effects. That's expected - we'll fix them in Phase 2.

---

## Phase 2: Create EffectRegistry

Create `include/EffectRegistry.h`:

```cpp
#ifndef EFFECT_REGISTRY_H
#define EFFECT_REGISTRY_H

#include "Effect.h"

// Forward declarations - include actual effect headers in .cpp
class Effect;

/**
 * Manages effect lifecycle and switching
 *
 * Effects are registered at compile time. The registry handles
 * begin()/end() calls when switching between effects.
 */
class EffectRegistry {
public:
    static constexpr uint8_t MAX_EFFECTS = 8;

private:
    Effect* effects[MAX_EFFECTS] = {};
    uint8_t effectCount = 0;
    uint8_t currentIndex = 0;

public:
    /**
     * Register an effect (call during setup)
     * @return Index of registered effect, or 255 if full
     */
    uint8_t registerEffect(Effect* effect) {
        if (effectCount >= MAX_EFFECTS) return 255;
        effects[effectCount] = effect;
        return effectCount++;
    }

    /**
     * Initialize the registry and start first effect
     */
    void begin() {
        if (effectCount > 0 && effects[currentIndex]) {
            effects[currentIndex]->begin();
        }
    }

    /**
     * Get currently active effect
     */
    Effect* current() {
        return (currentIndex < effectCount) ? effects[currentIndex] : nullptr;
    }

    /**
     * Switch to next effect (wraps around)
     */
    void next() {
        if (effectCount == 0) return;

        if (effects[currentIndex]) {
            effects[currentIndex]->end();
        }

        currentIndex = (currentIndex + 1) % effectCount;

        if (effects[currentIndex]) {
            effects[currentIndex]->begin();
        }
    }

    /**
     * Switch to specific effect by index
     */
    void setEffect(uint8_t index) {
        if (index >= effectCount) return;
        if (index == currentIndex) return;

        if (effects[currentIndex]) {
            effects[currentIndex]->end();
        }

        currentIndex = index;

        if (effects[currentIndex]) {
            effects[currentIndex]->begin();
        }
    }

    /**
     * Get current effect index
     */
    uint8_t getCurrentIndex() const { return currentIndex; }

    /**
     * Get total number of registered effects
     */
    uint8_t getEffectCount() const { return effectCount; }

    /**
     * Notify current effect of revolution (call from hall sensor handler)
     */
    void onRevolution(float rpm) {
        if (effects[currentIndex]) {
            effects[currentIndex]->onRevolution(rpm);
        }
    }
};

#endif // EFFECT_REGISTRY_H
```

---

## Phase 3: Migrate Each Effect

### Migration Template

For each effect in `src/effects/`, convert from:

```cpp
// OLD PATTERN
void renderSomeEffect(RenderContext& ctx) {
    renderAllArms(ctx, [&](uint16_t physicalLed, uint16_t ledIdx, const ArmInfo& arm) {
        // per-pixel logic using arm.angle, ctx.leds[physicalLed]
    });
}
```

To:

```cpp
// NEW PATTERN
class SomeEffect : public Effect {
public:
    void begin() override {
        // one-time init (if needed)
    }

    void render(RenderContext& ctx) override {
        ctx.clear();  // or don't, if you want to layer

        for (int a = 0; a < 3; a++) {
            auto& arm = ctx.arms[a];
            // per-arm logic using arm.angle, arm.pixels[]
        }
    }

    void onRevolution(float rpm) override {
        // per-revolution updates (if needed)
    }
};
```

### Effect-by-Effect Migration

#### 1. SolidArms (`src/effects/SolidArms.cpp`)

**Old:** Uses angle-to-pattern lookup per pixel
**New:** Same logic, but iterate arms directly

```cpp
// include/effects/SolidArms.h
#ifndef SOLID_ARMS_H
#define SOLID_ARMS_H

#include "Effect.h"

class SolidArms : public Effect {
public:
    void render(RenderContext& ctx) override;

private:
    CRGB getPatternColor(uint8_t pattern, uint8_t armIndex);
};

#endif

// src/effects/SolidArms.cpp
#include "effects/SolidArms.h"
#include "polar_helpers.h"

void SolidArms::render(RenderContext& ctx) {
    for (int a = 0; a < 3; a++) {
        auto& arm = ctx.arms[a];

        // Pattern based on THIS arm's angle (0-19 patterns, 18° each)
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

CRGB SolidArms::getPatternColor(uint8_t pattern, uint8_t armIndex) {
    // ... copy existing pattern color logic ...
}
```

#### 2. RpmArc (`src/effects/RpmArc.cpp`)

**Old:** Per-pixel arc check + virtual position check
**New:** Per-arm arc check, then fill radial range

```cpp
// include/effects/RpmArc.h
#ifndef RPM_ARC_H
#define RPM_ARC_H

#include "Effect.h"

class RpmArc : public Effect {
public:
    void begin() override;
    void render(RenderContext& ctx) override;

private:
    static constexpr float RPM_MIN = 800.0f;
    static constexpr float RPM_MAX = 2500.0f;

    float arcCenter = 0.0f;
    float arcWidth = 20.0f;
    CRGBPalette16 palette;

    uint8_t rpmToRadius(float rpm) const;
};

#endif

// src/effects/RpmArc.cpp
#include "effects/RpmArc.h"
#include "polar_helpers.h"

void RpmArc::begin() {
    palette = LavaColors_p;  // Or load from config
}

void RpmArc::render(RenderContext& ctx) {
    ctx.clear();

    // Animate arc width based on RPM
    arcWidth = 15.0f + 15.0f * (ctx.rpm() / 2800.0f);
    uint8_t radiusLimit = rpmToRadius(ctx.rpm());

    for (int a = 0; a < 3; a++) {
        auto& arm = ctx.arms[a];

        float intensity = arcIntensity(arm.angle, arcCenter, arcWidth);
        if (intensity == 0.0f) continue;  // Skip entire arm

        for (int p = 0; p < 10; p++) {
            uint8_t virtualPos = a + p * 3;
            if (virtualPos < radiusLimit) {
                CRGB color = ColorFromPalette(palette, virtualPos * 8);
                color.nscale8(static_cast<uint8_t>(intensity * 255));
                arm.pixels[p] = color;
            }
        }
    }
}

uint8_t RpmArc::rpmToRadius(float rpm) const {
    float clamped = constrain(rpm, RPM_MIN, RPM_MAX);
    float normalized = (clamped - RPM_MIN) / (RPM_MAX - RPM_MIN);
    return 1 + static_cast<uint8_t>(normalized * 29.0f);
}
```

#### 3. NoiseField (`src/effects/NoiseField.cpp`)

**Old:** Per-pixel noise sampling
**New:** Same, but explicit arm iteration

```cpp
// include/effects/NoiseField.h
#ifndef NOISE_FIELD_H
#define NOISE_FIELD_H

#include "Effect.h"

class NoiseField : public Effect {
public:
    void render(RenderContext& ctx) override;

private:
    uint32_t timeOffset = 0;
    CRGBPalette16 palette = LavaColors_p;
};

#endif

// src/effects/NoiseField.cpp
#include "effects/NoiseField.h"

void NoiseField::render(RenderContext& ctx) {
    timeOffset += 10;

    for (int a = 0; a < 3; a++) {
        auto& arm = ctx.arms[a];

        // Use THIS arm's actual angle
        uint16_t noiseX = static_cast<uint16_t>(arm.angle * 182.0f);

        for (int p = 0; p < 10; p++) {
            uint8_t virtualPos = a + p * 3;
            uint16_t noiseY = virtualPos * 2184;

            uint8_t brightness = inoise16(noiseX, noiseY, timeOffset) >> 8;
            arm.pixels[p] = ColorFromPalette(palette, brightness);
        }
    }
}
```

#### 4. VirtualBlobs (`src/effects/VirtualBlobs.cpp`)

**Old:** Per-pixel blob hit test with blob_cache
**New:** Shape-first iteration, NO cache needed

```cpp
// include/effects/VirtualBlobs.h
#ifndef VIRTUAL_BLOBS_H
#define VIRTUAL_BLOBS_H

#include "Effect.h"
#include "blob_types.h"

class VirtualBlobs : public Effect {
public:
    void begin() override;
    void render(RenderContext& ctx) override;
    void onRevolution(float rpm) override;

private:
    static constexpr int MAX_BLOBS = 5;
    Blob blobs[MAX_BLOBS];
};

#endif

// src/effects/VirtualBlobs.cpp
#include "effects/VirtualBlobs.h"
#include "polar_helpers.h"

void VirtualBlobs::begin() {
    // Initialize blobs (copy from setupVirtualBlobs())
    timestamp_t now = esp_timer_get_time();
    for (int i = 0; i < MAX_BLOBS; i++) {
        blobs[i].active = true;
        // ... rest of initialization ...
    }
}

void VirtualBlobs::onRevolution(float rpm) {
    // Update blob animations once per revolution
    timestamp_t now = esp_timer_get_time();
    for (auto& blob : blobs) {
        updateBlob(blob, now);
    }
}

void VirtualBlobs::render(RenderContext& ctx) {
    ctx.clear();

    // Shape-first: iterate blobs, not pixels
    for (const auto& blob : blobs) {
        if (!blob.active) continue;

        for (int a = 0; a < 3; a++) {
            auto& arm = ctx.arms[a];

            // Check if THIS arm is in blob's arc
            if (!isAngleInArc(arm.angle, blob.currentStartAngle,
                             blob.currentArcSize)) {
                continue;
            }

            // Draw blob's radial extent on this arm
            for (int p = 0; p < 10; p++) {
                uint8_t virtualPos = a + p * 3;
                float radialCenter = blob.currentRadialCenter;
                float radialSize = blob.currentRadialSize;

                if (virtualPos >= radialCenter - radialSize/2 &&
                    virtualPos <= radialCenter + radialSize/2) {
                    arm.pixels[p] += blob.color;
                }
            }
        }
    }
}
```

#### 5. PerArmBlobs (`src/effects/PerArmBlobs.cpp`)

Similar to VirtualBlobs but each blob is tied to an arm. Migration follows same pattern.

---

## Phase 4: Update main.cpp

### Remove Old Includes

```cpp
// REMOVE these
#include "arm_renderer.h"
#include "blob_cache.h"
#include "effects.h"

// ADD these
#include "Effect.h"
#include "RenderContext.h"
#include "EffectRegistry.h"
#include "effects/SolidArms.h"
#include "effects/RpmArc.h"
#include "effects/NoiseField.h"
#include "effects/VirtualBlobs.h"
#include "effects/PerArmBlobs.h"
```

### Create Effect Instances and Registry

```cpp
// Global instances
SolidArms solidArmsEffect;
RpmArc rpmArcEffect;
NoiseField noiseFieldEffect;
VirtualBlobs virtualBlobsEffect;
PerArmBlobs perArmBlobsEffect;

EffectRegistry effectRegistry;
RenderContext renderCtx;

void setup() {
    // ... existing setup ...

    // Register effects
    effectRegistry.registerEffect(&solidArmsEffect);
    effectRegistry.registerEffect(&rpmArcEffect);
    effectRegistry.registerEffect(&noiseFieldEffect);
    effectRegistry.registerEffect(&virtualBlobsEffect);
    effectRegistry.registerEffect(&perArmBlobsEffect);

    effectRegistry.begin();
}
```

### Update Loop

```cpp
void loop() {
    // ... existing timing/hall sensor code ...

    if (isWarmupComplete && isRotating) {
        // Populate context
        renderCtx.timeUs = now;
        renderCtx.microsPerRev = microsecondsPerRev;

        // Set arm angles (these already exist)
        renderCtx.arms[0].angle = angleInner;
        renderCtx.arms[1].angle = angleMiddle;
        renderCtx.arms[2].angle = angleOuter;

        // === NEW: Single effect render call ===
        effectRegistry.current()->render(renderCtx);

        // === REMOVED: blob updates, blob_cache, switch statement ===

        // Copy to strip
        // Arm index to physical start: [0]=10, [1]=0, [2]=20
        static const uint16_t armStart[3] = {10, 0, 20};

        for (int a = 0; a < 3; a++) {
            for (int p = 0; p < 10; p++) {
                CRGB color = renderCtx.arms[a].pixels[p];
                color.nscale8(128);  // Power budget
                strip.SetPixelColor(armStart[a] + p,
                    RgbColor(color.r, color.g, color.b));
            }
        }

        strip.Show();
    }
    // ... rest of loop ...
}
```

### Update Hall Processing Task

```cpp
void hallProcessingTask(void* pvParameters) {
    HallEffectEvent event;
    QueueHandle_t queue = hallDriver.getEventQueue();

    while (1) {
        if (xQueueReceive(queue, &event, portMAX_DELAY) == pdPASS) {
            revTimer.addTimestamp(event.triggerTimestamp);

            // === NEW: Notify effect of revolution ===
            float rpm = revTimer.getRPM();  // Or calculate from microsPerRev
            effectRegistry.onRevolution(rpm);

            // ... rest of handler ...
        }
    }
}
```

---

## Phase 5: Cleanup

### Files to Delete

After migration is complete and tested:

```bash
rm include/arm_renderer.h
rm include/blob_cache.h
rm include/effects.h
rm include/RenderContext.h.old

rm src/arm_renderer.cpp      # If exists
rm src/blob_cache.cpp        # If exists
```

### Files to Reorganize

Move effect headers to `include/effects/`:

```
include/effects/
  SolidArms.h
  RpmArc.h
  NoiseField.h
  VirtualBlobs.h
  PerArmBlobs.h
```

---

## Verification Checklist

After each phase:

- [ ] **Phase 1:** Code compiles (will have errors, that's expected)
- [ ] **Phase 2:** EffectRegistry.h exists, compiles
- [ ] **Phase 3:** Each effect class compiles individually
- [ ] **Phase 4:** Full build succeeds
- [ ] **Phase 5:** Run on hardware, verify visual output

### Per-Effect Verification

For each migrated effect:

- [ ] Effect compiles
- [ ] `begin()` called once at startup
- [ ] `render()` produces expected visual output
- [ ] `onRevolution()` called per rotation (if used)
- [ ] No use of old `renderAllArms`, `blob_cache`, or `ctx.leds[]`

---

## Rollback Plan

If migration fails mid-way:

```bash
# Restore old RenderContext
mv include/RenderContext.h include/RenderContext.h.new
mv include/RenderContext.h.old include/RenderContext.h

# Old effects still in src/effects/ - they'll work again
```

---

## Notes for LLM Agents

When migrating an effect:

1. **Read the old effect first** - Understand what it does
2. **Create header in `include/effects/`** - Declare the class
3. **Create implementation in `src/effects/`** - Convert logic
4. **Key pattern changes:**
   - `renderAllArms(ctx, lambda)` → `for (auto& arm : ctx.arms)`
   - `ctx.leds[physicalLed]` → `arm.pixels[p]`
   - `arm.angle` (from callback) → `ctx.arms[a].angle`
   - External `blobs[]` → class member `blobs[]`
   - `updateBlobCache()` → DELETE (not needed)
5. **Don't build at the end** - Let parent agent do consolidated build
