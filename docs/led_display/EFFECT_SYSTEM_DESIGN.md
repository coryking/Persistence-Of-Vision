# Effect System Reference

**Read this document before writing or modifying effects.** It contains everything needed to create a working effect without exploring the codebase.

**Source of truth for constants:** `include/hardware_config.h` (LED counts, arm layout), `include/geometry.h` (angles, radial positions, speed ranges), `include/polar_helpers.h` (helper functions).

---

## Quick Start: Adding a New Effect

1. Create header in `include/effects/YourEffect.h` — class declaration, state
2. Create source in `src/effects/YourEffect.cpp` — method implementations
3. `#include "effects/YourEffect.h"` in `src/main.cpp`
4. Create instance: `YourEffect yourEffect;` in `src/main.cpp`
5. Register: `effectManager.registerEffect(&yourEffect);` in `registerEffects()`

Effects are numbered 1-based in registration order. The number maps to IR remote buttons.

---

## 1. Effect Base Class

**Source:** `include/Effect.h`

All effects inherit from `Effect`. Only `render()` is required.

```cpp
class Effect {
public:
    virtual ~Effect() = default;

    // === Required ===
    virtual void render(RenderContext& ctx) = 0;  // THE MAIN WORK — called every render cycle

    // === Optional Lifecycle ===
    virtual void begin() {}   // Called once when effect is activated
    virtual void end() {}     // Called once when effect is deactivated

    // === Optional Event ===
    // Called once per revolution at hall sensor trigger.
    // Natural place for per-revolution state updates.
    virtual void onRevolution(timestamp_t usPerRev, timestamp_t timestamp,
                              uint16_t revolutionCount) {}

    // === Optional Control (IR Remote via ESP-NOW) ===
    virtual void nextMode() {}       // Right arrow — cycle internal mode forward
    virtual void prevMode() {}       // Left arrow  — cycle internal mode backward
    virtual void paramUp() {}        // Up arrow    — secondary parameter increment
    virtual void paramDown() {}      // Down arrow  — secondary parameter decrement

    // === Optional Configuration ===
    virtual bool requiresFullBrightness() const { return false; }
    //   Return true to bypass brightness control (e.g., Radar effect)
};
```

### IR Remote Button Mapping

The IR remote on the motor controller sends commands via ESP-NOW to the display. The full mapping is defined in `motor_controller/src/remote_input.cpp`; the effect-relevant buttons are:

| Remote Button | Effect Method | Convention |
|---------------|---------------|------------|
| **Right arrow** | `nextMode()` | Cycle visual variant forward (e.g., contrast mode, phosphor type) |
| **Left arrow** | `prevMode()` | Cycle visual variant backward |
| **Up arrow** | `paramUp()` | Cycle content/data forward (e.g., next palette, next texture, next preset) |
| **Down arrow** | `paramDown()` | Cycle content/data backward |
| **Number 1-9, 0** | (system) | Select effect directly (1-based registration order; 0 = effect 10) |
| **Vol Up/Down** | (system) | Brightness control (0-10 scale, handled by EffectManager, not per-effect) |
| **Power** | (system) | Display on/off toggle |

**Convention from existing effects:**
- **Left/Right (mode):** Visual rendering variation — how the same content looks. NoiseField uses it for contrast modes (Normal, S-curve, Turbulence...). Radar uses it for phosphor type (P7, P12, P19, P1).
- **Up/Down (param):** Content/data selection — what is being shown. NoiseField uses it to cycle palettes. PolarGlobe uses it to cycle planet textures. Radar uses it to cycle presets (Aircraft, Classic, Marine, Zombie).

**Tip:** Not every effect needs all four. Many effects only implement `paramUp()`/`paramDown()` for palette cycling. The defaults are no-ops, so unimplemented buttons are silently ignored.

**Overriding brightness control:** Vol Up/Down adjusts a global 0-10 brightness scale applied by EffectManager to all effects. If your effect needs to bypass this (e.g., it depends on precise phosphor colors or has its own brightness logic), override `requiresFullBrightness()` to return `true`. The Radar effect does this because dimming would break its phosphor decay color accuracy:

```cpp
bool requiresFullBrightness() const override { return true; }
```

---

## 2. RenderContext

**Source:** `include/RenderContext.h`

Passed to `render()` every cycle. Owns the pixel buffers.

### Timing Fields

```cpp
uint32_t frameCount;        // Frame counter (incremented every render)
uint32_t timeUs;            // Current timestamp in microseconds
interval_t microsPerRev;    // Microseconds per revolution (use directly!)
angle_t slotSizeUnits;      // Angular resolution (angle units per slot)
```

**Performance rule:** Do NOT convert `microsPerRev` to RPM float in the render path. Use `speedFactor8(microsPerRev)` from `polar_helpers.h` to get a 0-255 speed value instead.

### The Three Arms (Physical Reality)

```cpp
struct Arm {
    angle_t angleUnits;                          // THIS arm's angle (3600 = 360°)
    CRGB pixels[HardwareConfig::LEDS_PER_ARM];   // THIS arm's LEDs: [0]=hub, [max]=tip
} arms[3];
```

Arm assignments (see `include/hardware_config.h` for authoritative layout):
- `arms[0]` = outer/ARM3 — phase offset +240° from hall sensor
- `arms[1]` = middle/ARM2 — 0° hall sensor reference
- `arms[2]` = inside/ARM1 — phase offset +120° from hall sensor

**The arms have different LED counts.** `arms[0]` has one more LED than the others. Use `HardwareConfig::ARM_LED_COUNT[a]` for the actual count per arm, or `HardwareConfig::LEDS_PER_ARM` for the maximum (buffer sizing).

### Virtual Pixel Access (40-LED Radial Line)

The three arms create 40 interlaced concentric rings when spinning. Virtual pixels number these 0 (innermost) to 39 (outermost) in radial order.

```cpp
CRGB& virt(uint8_t v);     // Access virtual pixel 0-39
void clear();               // All pixels to black
void fillVirtual(uint8_t start, uint8_t end, CRGB color);
void fillVirtualGradient(uint8_t start, uint8_t end,
                         const CRGBPalette16& palette,
                         uint8_t paletteStart = 0, uint8_t paletteEnd = 255);
```

**Virtual pixel mapping:** Because arm[0] has an extra LED at the hub, the mapping is NOT a simple `v % 3` / `v / 3`. It uses lookup tables internally. See `RenderContext.h` for the exact mapping. The key facts:
- `virt(0)` = arm[0]:led0 (the extra inner LED, no matching LED on other arms)
- `virt(1-3)` = radial row 1 (arm[0]:led1, arm[1]:led0, arm[2]:led0)
- `virt(4-6)` = radial row 2, and so on through row 13
- `virt(37-39)` = outermost radial row (tips)

**When to use virtual pixels vs direct arm access:**
- **Virtual pixels** (`ctx.virt(v)`) — When you want a unified 40-pixel radial line (gradients, sweeps, radial effects). Simpler but ignores that the 3 pixels per row are at different angles.
- **Direct arm access** (`ctx.arms[a].pixels[p]`) — When you need each arm's actual angle (noise fields, arc-based effects, anything angle-dependent). More control, more accurate.

---

## 3. Angle System

**Source:** `include/geometry.h`

Angles use integer units: **3600 units = 360°** (0.1° precision).

```cpp
typedef uint16_t angle_t;   // 0-3599

#define ANGLE_UNITS(deg) ((angle_t)((deg) * 10))
#define UNITS_TO_DEGREES(units) ((float)(units) / 10.0f)

// Constants defined in geometry.h:
// ANGLE_FULL_CIRCLE = 3600
// ANGLE_HALF_CIRCLE = 1800
// ANGLE_PER_PATTERN = 180 (18°)
```

Arm phase offsets (defined in `geometry.h`):
```cpp
ARM_PHASE[0] = ANGLE_UNITS(120);   // outer arm
ARM_PHASE[1] = ANGLE_UNITS(0);     // middle arm (hall reference)
ARM_PHASE[2] = ANGLE_UNITS(240);   // inside arm
```

---

## 4. Polar Helpers

**Source:** `include/polar_helpers.h`

### Angular Helpers (Integer — Preferred)

```cpp
angle_t normalizeAngleUnits(int32_t units);           // Normalize to 0-3599
float angleUnitsToRadians(angle_t units);              // Convert to radians
int16_t angularDistanceUnits(angle_t from, angle_t to); // Signed distance (-1800 to +1800)
angle_t angularDistanceAbsUnits(angle_t a, angle_t b);  // Absolute distance (0 to 1800)
bool isAngleInArcUnits(angle_t angle, angle_t center, angle_t width); // Arc test (handles wraparound)
uint8_t arcIntensityUnits(angle_t angle, angle_t center, angle_t width); // 0-255 (center=255)
```

### Speed Helpers

```cpp
uint8_t speedFactor8(interval_t microsPerRev);         // 0-255, faster=higher (700-2800 RPM)
uint8_t speedFactor8HandSpin(interval_t microsPerRev); // 0-255, faster=higher (5-60 RPM)
```

### Radial Helpers

```cpp
bool isRadiusInRange(uint8_t virtualPos, uint8_t start, uint8_t end);
float normalizedRadius(uint8_t virtualPos);    // 0.0=hub, 1.0=tip
uint8_t virtualFromNormalized(float normalized);
void virtualToArmLed(uint8_t virtualPos, uint8_t& armIndex, uint8_t& ledPos);
uint8_t armLedToVirtual(uint8_t armIndex, uint8_t ledPos);
```

### Noise Helper

```cpp
// Cylindrical Perlin noise → 16-bit palette index (smooth gradients)
uint16_t noiseCylinderPalette16(float angle, float height, uint32_t time, float radius);
```

### Legacy Float Helpers

`normalizeAngle()`, `angularDistance()`, `angularDistanceAbs()`, `isAngleInArc()`, `arcIntensity()`, `arcIntensitySoftEdge()` — still available but slower than integer versions.

---

## 5. Geometry

**Source:** `include/geometry.h` → `RadialGeometry` namespace

The display is a donut: LEDs don't reach the rotation center. See `geometry.h` for all constants (`INNER_DISPLAY_RADIUS_MM`, `OUTER_DISPLAY_RADIUS_MM`, `DISPLAY_SPAN_MM`, etc.) and `docs/led_display/HARDWARE.md` for calibration methodology.

Key function:
```cpp
float RadialGeometry::ringRadiusMM(int ring);  // Physical radius in mm for ring 0-39
```

Speed constants (defined in `geometry.h`):
```cpp
MICROS_PER_REV_MIN   // Fastest motor speed (~2800 RPM)
MICROS_PER_REV_MAX   // Slowest motor speed (~700 RPM)
RPM_TO_MICROS(rpm)   // Compile-time conversion macro
```

---

## 6. EffectManager

**Source:** `include/EffectManager.h`

Manages effect lifecycle, brightness (0-10 scale), and cross-core command processing via FreeRTOS queue.

Effects are registered in `registerEffects()` in `src/main.cpp`. Registration order determines the 1-based effect number (maps to IR remote buttons).

Commands from the IR remote arrive via ESP-NOW on Core 0 and are queued to the render task on Core 1. The command types include: set effect, brightness up/down, mode next/prev, param up/down, display power.

---

## 7. Common Patterns

### Pattern: Per-Arm Angle-Based Rendering

The most common pattern. Each arm has a different angle; process them independently.

```cpp
void render(RenderContext& ctx) override {
    ctx.clear();

    for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
        auto& arm = ctx.arms[a];

        // Test THIS arm's angle against something
        uint8_t intensity = arcIntensityUnits(arm.angleUnits, targetAngle, arcWidth);
        if (intensity == 0) continue;

        for (int p = 0; p < HardwareConfig::ARM_LED_COUNT[a]; p++) {
            CRGB color = ColorFromPalette(palette, p * 8);
            color.nscale8(intensity);
            arm.pixels[p] = color;
        }
    }
}
```

### Pattern: Virtual Pixel Radial Gradient

When you don't care about per-arm angles (e.g., radial-only effects):

```cpp
void render(RenderContext& ctx) override {
    for (uint8_t v = 0; v < HardwareConfig::TOTAL_LOGICAL_LEDS; v++) {
        uint8_t palIdx = map(v, 0, HardwareConfig::TOTAL_LOGICAL_LEDS - 1, 0, 255);
        ctx.virt(v) = ColorFromPalette(palette, palIdx);
    }
}
```

### Pattern: Noise Field (Cylindrical)

Uses each arm's actual angle for seamless cylindrical noise:

```cpp
void render(RenderContext& ctx) override {
    for (int a = 0; a < HardwareConfig::NUM_ARMS; a++) {
        auto& arm = ctx.arms[a];
        float angle = angleUnitsToRadians(arm.angleUnits);

        for (int p = 0; p < HardwareConfig::ARM_LED_COUNT[a]; p++) {
            float height = static_cast<float>(p) / HardwareConfig::ARM_LED_COUNT[a];
            uint16_t noiseVal = noiseCylinderPalette16(angle, height, timeOffset, radius);
            arm.pixels[p] = ColorFromPalette(palette, noiseVal >> 8);
        }
    }
}
```

### Pattern: Speed-Responsive Effect

Scale visual parameters based on rotation speed:

```cpp
void render(RenderContext& ctx) override {
    uint8_t speed = speedFactor8(ctx.microsPerRev);  // 0=slow, 255=fast

    angle_t arcWidth = BASE_WIDTH + scale8(EXTRA_WIDTH, speed);
    uint8_t trailLength = 5 + scale8(30, speed);
    // ...
}
```

### Pattern: Per-Revolution State Updates

Use `onRevolution()` for state that changes once per rotation:

```cpp
void onRevolution(timestamp_t usPerRev, timestamp_t timestamp,
                  uint16_t revolutionCount) override {
    // Advance animation phase
    sweepAngle = normalizeAngleUnits(sweepAngle + sweepStep);

    // Update targets, respawn objects, etc.
    updateTargets(timestamp);
}
```

### Pattern: Mode/Param Control

Let the user cycle through variations via IR remote. Convention: left/right = visual variant, up/down = content selection (see IR Remote Button Mapping in section 1).

```cpp
// Left/Right arrows: cycle how it looks (rendering variation)
void nextMode() override {
    contrastMode = (contrastMode + 1) % MODE_COUNT;
}
void prevMode() override {
    contrastMode = (contrastMode + MODE_COUNT - 1) % MODE_COUNT;
}

// Up/Down arrows: cycle what is shown (content/data)
void paramUp() override {
    paletteIndex = (paletteIndex + 1) % PALETTE_COUNT;
    palette = *PALETTES[paletteIndex];
}
void paramDown() override {
    paletteIndex = (paletteIndex + PALETTE_COUNT - 1) % PALETTE_COUNT;
    palette = *PALETTES[paletteIndex];
}
```

---

## 8. The PWM Brightness Floor

PWM dimming on a spinning display creates a unique visual constraint. The LED isn't "dim" — it's fully bright for a shorter duration within each PWM cycle. At low brightness values on a spinning display, the sparse "on" pulses appear as scattered bright dots rather than smooth dim color.

**Implications:**
- No smooth dark gradients — at some threshold, solid color breaks into sparkly dots
- Even RGB(1,1,1) is visibly bright but spotty
- Dark-centered palettes work well — mostly black with bright pops
- High contrast is inherent

**Artistic opportunities:** Firefly/bioluminescence, starfield backgrounds, ember/spark effects. See `docs/led_display/POV_Perlin_Noise_Color_Theory.md` for palette design guidance.

---

## 9. Performance Rules

1. **Use integer angle math** (`angle_t`, 3600 units) not float degrees in render paths
2. **Use `speedFactor8()` instead of RPM float division** — float division is ~2x slower on ESP32-S3
3. **Use `HardwareConfig::ARM_LED_COUNT[a]`** for actual per-arm LED count in loops
4. **Use `HardwareConfig::LEDS_PER_ARM`** only for buffer sizing (it's the maximum)
5. **Prefer shape-first iteration** — iterate shapes/blobs then check which arms they hit, rather than iterating every pixel and checking every shape
6. **`scale8()` for 0-255 multiplication** — FastLED's optimized byte math
7. **No `delay()` in render path** — tight loop is intentional
8. **Use `SECONDS_TO_MICROS(s)` macro** from `shared/types.h` for time constants

See `docs/led_display/ESP32_REFERENCE.md` for ESP32-S3 FPU performance details and integer math patterns.

---

## 10. Polar Texture System (PolarGlobe)

For pre-computed image-based effects, the project includes a polar texture pipeline:

- **`tools/convert_to_polar.py`** — Converts equirectangular images to polar format for the display
- **Texture format:** Width = angular resolution, Height = radial rings (center to edge)
- **Render loop** is trivial array lookup: `color = texture[row][col]`
- **Rotation** = longitude offset applied to column index

See `include/effects/PolarGlobe.h` and `docs/led_display/COORDINATE_SYSTEMS.md` for details.

---

## 11. Types Reference

**Source:** `shared/types.h`

```cpp
typedef uint64_t timestamp_t;   // Microsecond timestamp
typedef uint64_t interval_t;    // Microsecond duration
typedef uint16_t angle_t;       // Angle in units (3600 = 360°)

#define SECONDS_TO_MICROS(s) ((s) * 1000000ULL)
```

---

## 12. File Structure

```
include/
  Effect.h              # Effect base class
  RenderContext.h        # RenderContext struct and virtual pixel mapping
  EffectManager.h        # Effect registration, switching, brightness, commands
  polar_helpers.h        # Angular, radial, speed, and noise helper functions
  geometry.h             # Angle types, arm phases, speed constants, radial geometry
  hardware_config.h      # LED counts, arm layout, pin assignments
  effects/
    YourEffect.h         # Effect class declaration + state

src/
  main.cpp               # Effect instances, registerEffects(), includes
  effects/
    YourEffect.cpp        # Effect method implementations
```

---

## 13. Checklist for New Effects

- [ ] Class inherits from `Effect`, implements `render(RenderContext& ctx)`
- [ ] Header in `include/effects/`, source in `src/effects/`
- [ ] Uses `HardwareConfig::` constants for all loop limits (NEVER hardcode counts)
- [ ] Uses integer angle math (`angle_t`, helpers from `polar_helpers.h`)
- [ ] Uses `speedFactor8()` instead of float RPM conversion
- [ ] Registered in `src/main.cpp` → `registerEffects()`
- [ ] Calls `ctx.clear()` at start of `render()` (unless intentionally layering)
- [ ] State that updates once per revolution goes in `onRevolution()`
- [ ] No `delay()` or blocking calls in `render()`
