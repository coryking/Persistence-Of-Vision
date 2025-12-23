#ifndef NOISE_FIELD_H
#define NOISE_FIELD_H

#include "Effect.h"

// =============================================================================
// DARK-CENTER PALETTES for Perlin noise
// =============================================================================
// Primary color is dark/black (entries 6-9), accent colors POP at edges.
// Perlin noise clusters around middle values (~65%), so dark center = mostly
// black with occasional bright flashes. Drive at higher brightness!
//
// To add a new palette:
//   1. Define it here as const CRGBPalette16
//   2. Add it to NOISE_PALETTES array below
// =============================================================================

// Ember: Mostly dark with fiery orange/red pops
const CRGBPalette16 EmberDark_p(
    CRGB(255, 100, 0),     // 0: Bright orange (rare pop!)
    CRGB(255, 60, 0),      // 1: Orange-red (rare pop!)
    CRGB(200, 30, 0),      // 2: Red
    CRGB(120, 15, 0),      // 3:
    CRGB(60, 5, 0),        // 4:
    CRGB(30, 2, 0),        // 5:
    CRGB(10, 0, 0),        // 6: PRIMARY - near black
    CRGB(5, 0, 0),         // 7: PRIMARY - almost black
    CRGB(8, 0, 0),         // 8: PRIMARY - almost black
    CRGB(15, 2, 0),        // 9: PRIMARY - dark ember
    CRGB(40, 5, 0),        // 10:
    CRGB(80, 15, 0),       // 11:
    CRGB(150, 40, 0),      // 12:
    CRGB(220, 80, 0),      // 13: Orange (rare pop!)
    CRGB(255, 150, 50),    // 14: Bright orange (rare pop!)
    CRGB(255, 200, 100)    // 15: Hot yellow (rare pop!)
);

// Abyss: Mostly black with cyan/white pops
const CRGBPalette16 AbyssDark_p(
    CRGB(0, 200, 255),     // 0: Bright cyan (rare pop!)
    CRGB(0, 150, 220),     // 1: Cyan (rare pop!)
    CRGB(0, 80, 150),      // 2: Blue
    CRGB(0, 40, 80),       // 3:
    CRGB(0, 15, 40),       // 4:
    CRGB(0, 5, 15),        // 5:
    CRGB(0, 0, 5),         // 6: PRIMARY - near black
    CRGB(0, 0, 2),         // 7: PRIMARY - almost black
    CRGB(0, 2, 5),         // 8: PRIMARY - almost black
    CRGB(0, 5, 15),        // 9: PRIMARY - dark blue
    CRGB(0, 20, 50),       // 10:
    CRGB(0, 50, 100),      // 11:
    CRGB(0, 100, 180),     // 12:
    CRGB(50, 180, 255),    // 13: Bright cyan (rare pop!)
    CRGB(150, 220, 255),   // 14: Cyan-white (rare pop!)
    CRGB(255, 255, 255)    // 15: Pure white (rare pop!)
);

// Void: Mostly black with purple/magenta pops
const CRGBPalette16 VoidDark_p(
    CRGB(255, 0, 200),     // 0: Bright magenta (rare pop!)
    CRGB(200, 0, 255),     // 1: Purple (rare pop!)
    CRGB(120, 0, 180),     // 2: Dark purple
    CRGB(60, 0, 100),      // 3:
    CRGB(30, 0, 50),       // 4:
    CRGB(10, 0, 20),       // 5:
    CRGB(3, 0, 5),         // 6: PRIMARY - near black
    CRGB(1, 0, 2),         // 7: PRIMARY - almost black
    CRGB(2, 0, 4),         // 8: PRIMARY - almost black
    CRGB(8, 0, 15),        // 9: PRIMARY - dark purple
    CRGB(30, 0, 60),       // 10:
    CRGB(80, 0, 140),      // 11:
    CRGB(150, 0, 220),     // 12:
    CRGB(220, 50, 255),    // 13: Bright purple (rare pop!)
    CRGB(255, 150, 255),   // 14: Pink-white (rare pop!)
    CRGB(255, 255, 255)    // 15: Pure white (rare pop!)
);

// Firefly: Mostly black with green/yellow pops
const CRGBPalette16 FireflyDark_p(
    CRGB(200, 255, 0),     // 0: Bright yellow-green (rare pop!)
    CRGB(100, 255, 0),     // 1: Green (rare pop!)
    CRGB(50, 180, 0),      // 2: Dark green
    CRGB(20, 100, 0),      // 3:
    CRGB(10, 50, 0),       // 4:
    CRGB(3, 20, 0),        // 5:
    CRGB(0, 5, 0),         // 6: PRIMARY - near black
    CRGB(0, 2, 0),         // 7: PRIMARY - almost black
    CRGB(1, 4, 0),         // 8: PRIMARY - almost black
    CRGB(5, 15, 0),        // 9: PRIMARY - dark green
    CRGB(15, 50, 0),       // 10:
    CRGB(40, 120, 0),      // 11:
    CRGB(100, 200, 0),     // 12:
    CRGB(180, 255, 0),     // 13: Bright green (rare pop!)
    CRGB(230, 255, 100),   // 14: Yellow-green (rare pop!)
    CRGB(255, 255, 200)    // 15: Pale yellow (rare pop!)
);

// =============================================================================

// Palette collection - add new palettes here!
const CRGBPalette16* const NOISE_PALETTES[] = {
    &EmberDark_p,
    &AbyssDark_p,
    &VoidDark_p,
    &FireflyDark_p,
};
const uint8_t NOISE_PALETTE_COUNT = sizeof(NOISE_PALETTES) / sizeof(NOISE_PALETTES[0]);

/**
 * Organic flowing texture using Perlin noise
 *
 * Creates lava-like patterns by mapping 2D noise to polar coordinates.
 * Each arm samples noise at its actual angle, creating natural flow.
 */
class NoiseField : public Effect {
public:
    void render(RenderContext &ctx) override;
    void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) override;
    void nextMode() override;
    void prevMode() override;
    void paramUp() override;    // Next palette
    void paramDown() override;  // Previous palette

    timestamp_t noiseTimeOffsetMs = 0;
    float radius = 1.5f;

    // Palette (manual control via paramUp/paramDown)
    uint8_t paletteIndex = 0;
    CRGBPalette16 palette = *NOISE_PALETTES[0];

    // Contrast modes: 0=Normal, 1=S-curve, 2=Turbulence
    uint8_t contrastMode = 0;
    static constexpr uint8_t CONTRAST_MODE_COUNT = 3;

    static constexpr float ANIMATION_SPEED = 10.0f;
    static constexpr float RADIUS_PERIOD_SECONDS = 15.0f;
    static constexpr float RADIUS_PERIOD_US = SECONDS_TO_MICROS(RADIUS_PERIOD_SECONDS);
    static constexpr float RADIUS_MIN = 0.75f;
    static constexpr float RADIUS_MAX = 1.75f;
};
#endif // NOISE_FIELD_H
