#pragma once

#include <FastLED.h>

// ============================================================================
// Shared Palette Collection
// ============================================================================
// Used by NoiseField, Kaleidoscope, and future effects.
//
// Dark-centered palettes (0-11): Create stained-glass geometry on Kaleidoscope,
// classic noise fields on NoiseField.
//
// Full-spectrum palettes (12-17): Create dominant-color noise on NoiseField,
// vibrant kaleidoscope patterns with no dark veins.
// ============================================================================

namespace SharedPalettes {

// Dark-centered custom palettes (from NoiseField)
// Using inline to avoid multiple definition linker errors
inline DEFINE_GRADIENT_PALETTE(ember_gp) {
    0,     0,   0,   0,
    64,   80,   0,   0,
    128, 255,  40,   0,
    192, 255, 100,   0,
    255, 255, 200,  20
};

inline DEFINE_GRADIENT_PALETTE(abyss_gp) {
    0,     0,   0,   0,
    64,    0,   0,  40,
    128,   0,  20,  80,
    192,   0,  60, 120,
    255,  20, 100, 180
};

inline DEFINE_GRADIENT_PALETTE(void_gp) {
    0,     0,   0,   0,
    64,   20,   0,  20,
    128,  60,   0,  60,
    192, 100,  20, 100,
    255, 180,  60, 180
};

inline DEFINE_GRADIENT_PALETTE(firefly_gp) {
    0,     0,   0,   0,
    64,    0,  40,   0,
    128,  40, 100,   0,
    192, 100, 180,  20,
    255, 180, 255, 100
};

inline DEFINE_GRADIENT_PALETTE(sunset_gp) {
    0,     0,   0,   0,
    64,   40,   0,  40,
    128, 120,  20,  60,
    192, 200,  80,  20,
    255, 255, 180,  40
};

inline DEFINE_GRADIENT_PALETTE(ice_gp) {
    0,     0,   0,   0,
    64,    0,  20,  60,
    128,  20,  80, 140,
    192,  60, 140, 200,
    255, 140, 200, 255
};

inline DEFINE_GRADIENT_PALETTE(gold_gp) {
    0,     0,   0,   0,
    64,   40,  20,   0,
    128, 100,  60,   0,
    192, 180, 120,  20,
    255, 255, 200,  80
};

inline DEFINE_GRADIENT_PALETTE(lava_gp) {
    0,     0,   0,   0,
    64,   80,   0,   0,
    128, 180,  20,   0,
    192, 255,  60,   0,
    255, 255, 140,  40
};

inline DEFINE_GRADIENT_PALETTE(fireice_gp) {
    0,     0,   0,   0,
    64,   80,   0,   0,
    128, 255,  40,   0,
    192,  40, 100, 255,
    255, 100, 180, 255
};

inline DEFINE_GRADIENT_PALETTE(synthwave_gp) {
    0,     0,   0,   0,
    64,   80,   0,  80,
    128, 180,   0, 180,
    192, 255,  20, 200,
    255,   0, 200, 255
};

inline DEFINE_GRADIENT_PALETTE(ember_subtle_gp) {
    0,     0,   0,   0,
    64,   40,   0,   0,
    128, 100,  20,   0,
    192, 160,  60,  10,
    255, 200, 120,  40
};

inline DEFINE_GRADIENT_PALETTE(neon_abyss_gp) {
    0,     0,   0,   0,
    64,    0,  40,  80,
    128,  20, 100, 180,
    192, 100, 200, 255,
    255, 200, 255, 255
};

// Custom full-spectrum palette
inline DEFINE_GRADIENT_PALETTE(acid_gp) {
    0,   255,   0, 255,  // Magenta
    64,    0, 255, 255,  // Cyan
    128, 255, 255,   0,  // Yellow
    192,   0, 255,   0,  // Green
    255, 255,   0, 255   // Magenta (wrap)
};

// Palette array
inline const CRGBPalette16 PALETTES[] PROGMEM = {
    ember_gp,           // 0
    abyss_gp,           // 1
    void_gp,            // 2
    firefly_gp,         // 3
    sunset_gp,          // 4
    ice_gp,             // 5
    gold_gp,            // 6
    lava_gp,            // 7
    fireice_gp,         // 8
    synthwave_gp,       // 9
    ember_subtle_gp,    // 10
    neon_abyss_gp,      // 11
    RainbowColors_p,    // 12
    RainbowStripeColors_p, // 13
    PartyColors_p,      // 14
    LavaColors_p,       // 15 (FastLED built-in, different from custom lava_gp)
    OceanColors_p,      // 16
    acid_gp             // 17
};

inline constexpr uint8_t PALETTE_COUNT = sizeof(PALETTES) / sizeof(PALETTES[0]);

// Palette names for UI feedback
inline const char* const PALETTE_NAMES[] = {
    "Ember",
    "Abyss",
    "Void",
    "Firefly",
    "Sunset",
    "Ice",
    "Gold",
    "Lava",
    "FireIce",
    "Synthwave",
    "EmberSubtle",
    "NeonAbyss",
    "Rainbow",
    "RainbowStripe",
    "Party",
    "LavaFL",       // FastLED's Lava
    "Ocean",
    "Acid"
};

} // namespace SharedPalettes
