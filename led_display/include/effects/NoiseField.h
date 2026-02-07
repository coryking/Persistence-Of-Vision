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
// NEW HUE FAMILIES (dark-centered)
// =============================================================================

// Sunset: Dark purple center with orange/pink pops
const CRGBPalette16 SunsetDark_p(
    CRGB(255, 200, 100),   // 0: Warm yellow (rare pop!)
    CRGB(255, 140, 50),    // 1: Orange (rare pop!)
    CRGB(255, 80, 50),     // 2: Red-orange
    CRGB(180, 40, 60),     // 3:
    CRGB(100, 20, 60),     // 4:
    CRGB(50, 10, 40),      // 5:
    CRGB(15, 2, 20),       // 6: PRIMARY - dark purple
    CRGB(8, 0, 12),        // 7: PRIMARY - almost black
    CRGB(10, 0, 15),       // 8: PRIMARY - almost black
    CRGB(20, 5, 30),       // 9: PRIMARY - dark purple
    CRGB(60, 15, 50),      // 10:
    CRGB(120, 30, 70),     // 11:
    CRGB(200, 60, 100),    // 12:
    CRGB(255, 100, 150),   // 13: Pink (rare pop!)
    CRGB(255, 150, 180),   // 14: Light pink (rare pop!)
    CRGB(255, 200, 220)    // 15: Pale pink (rare pop!)
);

// Ice: Dark blue center with white/pale blue pops
const CRGBPalette16 IceDark_p(
    CRGB(255, 255, 255),   // 0: Pure white (rare pop!)
    CRGB(220, 240, 255),   // 1: Ice white (rare pop!)
    CRGB(180, 210, 255),   // 2: Pale blue
    CRGB(100, 150, 200),   // 3:
    CRGB(50, 80, 140),     // 4:
    CRGB(20, 40, 80),      // 5:
    CRGB(5, 10, 30),       // 6: PRIMARY - dark blue
    CRGB(2, 5, 15),        // 7: PRIMARY - almost black
    CRGB(3, 8, 20),        // 8: PRIMARY - almost black
    CRGB(8, 15, 40),       // 9: PRIMARY - dark blue
    CRGB(30, 50, 100),     // 10:
    CRGB(70, 100, 160),    // 11:
    CRGB(140, 170, 220),   // 12:
    CRGB(200, 220, 255),   // 13: Pale blue (rare pop!)
    CRGB(230, 245, 255),   // 14: Ice (rare pop!)
    CRGB(255, 255, 255)    // 15: Pure white (rare pop!)
);

// Gold: Dark amber center with yellow/gold pops
const CRGBPalette16 GoldDark_p(
    CRGB(255, 230, 100),   // 0: Bright gold (rare pop!)
    CRGB(255, 200, 50),    // 1: Gold (rare pop!)
    CRGB(220, 160, 20),    // 2: Deep gold
    CRGB(160, 100, 10),    // 3:
    CRGB(100, 60, 5),      // 4:
    CRGB(50, 30, 2),       // 5:
    CRGB(15, 8, 0),        // 6: PRIMARY - dark amber
    CRGB(8, 4, 0),         // 7: PRIMARY - almost black
    CRGB(10, 5, 0),        // 8: PRIMARY - almost black
    CRGB(20, 10, 2),       // 9: PRIMARY - dark amber
    CRGB(60, 35, 5),       // 10:
    CRGB(120, 70, 10),     // 11:
    CRGB(180, 120, 20),    // 12:
    CRGB(230, 170, 40),    // 13: Gold (rare pop!)
    CRGB(255, 210, 80),    // 14: Bright gold (rare pop!)
    CRGB(255, 240, 150)    // 15: Pale gold (rare pop!)
);

// Lava: Dark red center with orange/yellow/white pops
const CRGBPalette16 LavaDark_p(
    CRGB(255, 255, 200),   // 0: Hot white (rare pop!)
    CRGB(255, 220, 100),   // 1: Hot yellow (rare pop!)
    CRGB(255, 150, 50),    // 2: Orange
    CRGB(220, 80, 20),     // 3:
    CRGB(150, 40, 10),     // 4:
    CRGB(80, 15, 5),       // 5:
    CRGB(25, 3, 0),        // 6: PRIMARY - dark red
    CRGB(12, 1, 0),        // 7: PRIMARY - almost black
    CRGB(18, 2, 0),        // 8: PRIMARY - almost black
    CRGB(40, 5, 0),        // 9: PRIMARY - dark red
    CRGB(100, 20, 5),      // 10:
    CRGB(180, 50, 10),     // 11:
    CRGB(230, 100, 20),    // 12:
    CRGB(255, 160, 50),    // 13: Orange (rare pop!)
    CRGB(255, 200, 100),   // 14: Yellow-orange (rare pop!)
    CRGB(255, 240, 180)    // 15: Hot white (rare pop!)
);

// =============================================================================
// DUAL-HUE PALETTES (dark-centered, different colors at each extreme)
// =============================================================================

// FireIce: Orange/red at low end, cyan/blue at high end, dark center
const CRGBPalette16 FireIceDark_p(
    CRGB(255, 150, 50),    // 0: Hot orange (rare pop!)
    CRGB(255, 80, 20),     // 1: Red-orange (rare pop!)
    CRGB(180, 40, 10),     // 2: Dark red
    CRGB(100, 15, 5),      // 3:
    CRGB(50, 5, 2),        // 4:
    CRGB(20, 2, 2),        // 5:
    CRGB(5, 0, 2),         // 6: PRIMARY - near black
    CRGB(2, 0, 2),         // 7: PRIMARY - almost black
    CRGB(2, 2, 5),         // 8: PRIMARY - almost black
    CRGB(2, 5, 15),        // 9: PRIMARY - hint of blue
    CRGB(5, 20, 50),       // 10:
    CRGB(10, 50, 100),     // 11:
    CRGB(20, 100, 160),    // 12:
    CRGB(50, 160, 220),    // 13: Cyan (rare pop!)
    CRGB(100, 200, 255),   // 14: Bright cyan (rare pop!)
    CRGB(200, 240, 255)    // 15: Ice blue (rare pop!)
);

// Synthwave: Hot pink at low end, cyan at high end, dark center
const CRGBPalette16 SynthwaveDark_p(
    CRGB(255, 50, 150),    // 0: Hot pink (rare pop!)
    CRGB(255, 20, 100),    // 1: Magenta (rare pop!)
    CRGB(180, 10, 70),     // 2: Dark magenta
    CRGB(100, 5, 40),      // 3:
    CRGB(50, 2, 20),       // 4:
    CRGB(20, 0, 10),       // 5:
    CRGB(5, 0, 5),         // 6: PRIMARY - near black
    CRGB(2, 0, 2),         // 7: PRIMARY - almost black
    CRGB(2, 2, 5),         // 8: PRIMARY - almost black
    CRGB(2, 5, 15),        // 9: PRIMARY - hint of cyan
    CRGB(5, 15, 40),       // 10:
    CRGB(10, 40, 80),      // 11:
    CRGB(20, 80, 140),     // 12:
    CRGB(50, 150, 200),    // 13: Cyan (rare pop!)
    CRGB(100, 220, 255),   // 14: Bright cyan (rare pop!)
    CRGB(200, 255, 255)    // 15: Cyan-white (rare pop!)
);

// =============================================================================
// VARIATION PALETTES
// =============================================================================

// EmberSubtle: Dimmer accents for meditative look
const CRGBPalette16 EmberSubtle_p(
    CRGB(180, 70, 0),      // 0: Muted orange (rare pop!)
    CRGB(150, 45, 0),      // 1: Muted orange-red (rare pop!)
    CRGB(120, 25, 0),      // 2: Dark red
    CRGB(80, 12, 0),       // 3:
    CRGB(45, 4, 0),        // 4:
    CRGB(25, 2, 0),        // 5:
    CRGB(10, 0, 0),        // 6: PRIMARY - near black
    CRGB(5, 0, 0),         // 7: PRIMARY - almost black
    CRGB(8, 0, 0),         // 8: PRIMARY - almost black
    CRGB(15, 2, 0),        // 9: PRIMARY - dark ember
    CRGB(35, 4, 0),        // 10:
    CRGB(60, 10, 0),       // 11:
    CRGB(100, 30, 0),      // 12:
    CRGB(140, 50, 0),      // 13: Muted orange (rare pop!)
    CRGB(160, 80, 20),     // 14: Soft orange (rare pop!)
    CRGB(180, 120, 60)     // 15: Pale amber (rare pop!)
);

// NeonAbyss: Punchier/more saturated cyan
const CRGBPalette16 NeonAbyss_p(
    CRGB(0, 255, 255),     // 0: Pure cyan (rare pop!)
    CRGB(0, 220, 255),     // 1: Bright cyan (rare pop!)
    CRGB(0, 150, 200),     // 2: Cyan
    CRGB(0, 80, 120),      // 3:
    CRGB(0, 40, 60),       // 4:
    CRGB(0, 15, 25),       // 5:
    CRGB(0, 3, 8),         // 6: PRIMARY - near black
    CRGB(0, 1, 4),         // 7: PRIMARY - almost black
    CRGB(0, 2, 6),         // 8: PRIMARY - almost black
    CRGB(0, 8, 20),        // 9: PRIMARY - dark cyan
    CRGB(0, 30, 60),       // 10:
    CRGB(0, 70, 120),      // 11:
    CRGB(0, 140, 200),     // 12:
    CRGB(0, 200, 255),     // 13: Bright cyan (rare pop!)
    CRGB(100, 255, 255),   // 14: Cyan-white (rare pop!)
    CRGB(255, 255, 255)    // 15: Pure white (rare pop!)
);

// =============================================================================

// Palette collection - add new palettes here!
const CRGBPalette16* const NOISE_PALETTES[] = {
    // Original palettes
    &EmberDark_p,
    &AbyssDark_p,
    &VoidDark_p,
    &FireflyDark_p,
    // New hue families
    &SunsetDark_p,
    &IceDark_p,
    &GoldDark_p,
    &LavaDark_p,
    // Dual-hue palettes
    &FireIceDark_p,
    &SynthwaveDark_p,
    // Variations
    &EmberSubtle_p,
    &NeonAbyss_p,
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
    void right() override;
    void left() override;
    void up() override;    // Next palette
    void down() override;  // Previous palette

    timestamp_t noiseTimeOffsetMs = 0;
    float radius = 1.5f;

    // Palette (manual control via paramUp/paramDown)
    uint8_t paletteIndex = 0;
    CRGBPalette16 palette = *NOISE_PALETTES[0];

    // Contrast modes: 0=Normal, 1=S-curve, 2=Turbulence, 3=Quantize, 4=Expanded, 5=Compressed
    uint8_t contrastMode = 0;
    static constexpr uint8_t CONTRAST_MODE_COUNT = 6;

    static constexpr float ANIMATION_SPEED = 10.0f;
    static constexpr float RADIUS_PERIOD_SECONDS = 15.0f;
    static constexpr float RADIUS_PERIOD_US = SECONDS_TO_MICROS(RADIUS_PERIOD_SECONDS);
    static constexpr float RADIUS_MIN = 0.75f;
    static constexpr float RADIUS_MAX = 1.75f;
};
#endif // NOISE_FIELD_H
