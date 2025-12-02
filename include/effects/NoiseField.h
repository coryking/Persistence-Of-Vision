#ifndef NOISE_FIELD_H
#define NOISE_FIELD_H

#include "Effect.h"

// =============================================================================
// Center-biased palettes for Perlin noise
// =============================================================================
// Perlin noise clusters around middle values, so:
//   - Entries 6-9: Primary colors (seen most often)
//   - Entries 0-2: Low accent (rare)
//   - Entries 13-15: High accent (rare)
//
// To add a new palette:
//   1. Define it here as const CRGBPalette16
//   2. Add it to NOISE_PALETTES array below
//   3. That's it! It will automatically be included in the rotation.
// =============================================================================

// Lava: Orange/yellow center, DARK red and BRIGHT white tips
const CRGBPalette16 LavaCenterBias_p(
    CRGB(20, 0, 0),        // 0: Nearly black red (rare)
    CRGB(40, 0, 0),        // 1: Very dark red (rare)
    CRGB(80, 5, 0),        // 2: Dark red
    CRGB(140, 20, 0),      // 3:
    CRGB(200, 60, 0),      // 4:
    CRGB(240, 100, 0),     // 5:
    CRGB(255, 140, 0),     // 6: PRIMARY - orange
    CRGB(255, 180, 0),     // 7: PRIMARY - orange-yellow
    CRGB(255, 200, 20),    // 8: PRIMARY - yellow
    CRGB(255, 180, 40),    // 9: PRIMARY - golden
    CRGB(255, 200, 80),    // 10:
    CRGB(255, 220, 140),   // 11:
    CRGB(255, 240, 200),   // 12:
    CRGB(255, 250, 240),   // 13: Hot white (rare)
    CRGB(255, 255, 250),   // 14: Bright white (rare)
    CRGB(255, 255, 255)    // 15: Pure white (very rare)
);

// Ocean: Blue/cyan center, DARK deep and BRIGHT white accents
const CRGBPalette16 OceanCenterBias_p(
    CRGB(0, 0, 10),        // 0: Nearly black (rare)
    CRGB(0, 5, 30),        // 1: Very dark blue (rare)
    CRGB(0, 15, 60),       // 2: Dark blue
    CRGB(0, 40, 100),      // 3:
    CRGB(0, 70, 130),      // 4:
    CRGB(0, 100, 160),     // 5:
    CRGB(0, 140, 180),     // 6: PRIMARY - ocean blue
    CRGB(20, 170, 200),    // 7: PRIMARY - cyan
    CRGB(60, 190, 210),    // 8: PRIMARY - bright cyan
    CRGB(40, 160, 190),    // 9: PRIMARY - teal
    CRGB(100, 200, 220),   // 10:
    CRGB(160, 230, 245),   // 11:
    CRGB(200, 245, 255),   // 12:
    CRGB(230, 255, 255),   // 13: Bright cyan (rare)
    CRGB(250, 255, 255),   // 14: Near white (rare)
    CRGB(255, 255, 255)    // 15: Pure white (very rare)
);

// Aurora: Green/cyan center, DARK purple and BRIGHT white tips
const CRGBPalette16 AuroraCenterBias_p(
    CRGB(10, 0, 20),       // 0: Nearly black purple (rare)
    CRGB(25, 0, 50),       // 1: Very dark purple (rare)
    CRGB(40, 0, 80),       // 2: Dark purple
    CRGB(30, 40, 100),     // 3:
    CRGB(0, 80, 100),      // 4:
    CRGB(0, 120, 80),      // 5:
    CRGB(0, 160, 60),      // 6: PRIMARY - teal-green
    CRGB(20, 200, 80),     // 7: PRIMARY - green
    CRGB(50, 230, 100),    // 8: PRIMARY - bright green
    CRGB(30, 190, 90),     // 9: PRIMARY - green
    CRGB(80, 220, 150),    // 10:
    CRGB(140, 240, 200),   // 11:
    CRGB(200, 250, 240),   // 12:
    CRGB(230, 255, 255),   // 13: Bright cyan (rare)
    CRGB(250, 255, 255),   // 14: Near white (rare)
    CRGB(255, 255, 255)    // 15: Pure white (very rare)
);

// =============================================================================
// DARK-CENTER PALETTES: Primary is dark/black, accents POP
// Drive at higher brightness - the rare accent colors will really stand out
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

// Diagnostic: Tests center-bias hypothesis
// If center-biased: You'll see mostly GREEN with rare RED/MAGENTA flashes
// If uniform: You'll see roughly equal red, green, and magenta
const CRGBPalette16 DiagnosticCenterBias_p(
    CRGB(255, 0, 0),       // 0: RED (should be rare)
    CRGB(255, 0, 0),       // 1: RED (should be rare)
    CRGB(255, 0, 0),       // 2: RED (should be rare)
    CRGB(200, 50, 0),      // 3: transition
    CRGB(150, 100, 0),     // 4: transition
    CRGB(100, 150, 0),     // 5: transition
    CRGB(0, 255, 0),       // 6: GREEN (should dominate)
    CRGB(0, 255, 0),       // 7: GREEN (should dominate)
    CRGB(0, 255, 0),       // 8: GREEN (should dominate)
    CRGB(0, 255, 0),       // 9: GREEN (should dominate)
    CRGB(50, 150, 100),    // 10: transition
    CRGB(100, 100, 150),   // 11: transition
    CRGB(150, 50, 200),    // 12: transition
    CRGB(255, 0, 255),     // 13: MAGENTA (should be rare)
    CRGB(255, 0, 255),     // 14: MAGENTA (should be rare)
    CRGB(255, 0, 255)      // 15: MAGENTA (should be rare)
);

// Palette collection - add new palettes here!
const CRGBPalette16* const NOISE_PALETTES[] = {
    // Dark-center palettes (crank brightness, colors POP)
    &EmberDark_p,
    &AbyssDark_p,
    &VoidDark_p,
    &FireflyDark_p,
    // Bright-center palettes
    &LavaCenterBias_p,
    &OceanCenterBias_p,
    &AuroraCenterBias_p,
    // Diagnostic (uncomment to test)
    // &DiagnosticCenterBias_p,
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

    timestamp_t noiseTimeOffsetMs = 0;
    float radius = 1.5f;

    // Palette rotation
    uint8_t paletteIndex = 0;
    CRGBPalette16 palette = *NOISE_PALETTES[0];
    static constexpr uint32_t PALETTE_SWITCH_SECONDS = 10;

    static constexpr float ANIMATION_SPEED = 10.0f;
    static constexpr float RADIUS_PERIOD_SECONDS = 15.0f;
    static constexpr float RADIUS_PERIOD_US = SECONDS_TO_MICROS(RADIUS_PERIOD_SECONDS);
    static constexpr float RADIUS_MIN = 0.75f;
    static constexpr float RADIUS_MAX = 1.75f;
};
#endif // NOISE_FIELD_H
