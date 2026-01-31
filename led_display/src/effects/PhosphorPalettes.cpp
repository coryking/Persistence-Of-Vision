#include "effects/PhosphorPalettes.h"
#include <cmath>

/**
 * Phosphor Decay Physics Implementation
 *
 * Based on docs/effects/math-for-phosphor.md
 * Colors from docs/effects/authentic-ppi-radar-display-physics.md
 *
 * Two decay types:
 * 1. Exponential: I(t) = I₀ × exp(-t/τ)
 *    - Used by P1 (fast), P12 (medium)
 *
 * 2. Inverse power law: I(t) = I₀ / (1 + t/τ)^n
 *    - Used by P7, P19 (long persistence)
 *    - Fast initial drop, then holds dim glow much longer
 *
 * Authentic phosphor colors (CHSV hue values):
 *   P7  = Cascade: blue-white → yellow-green (uses CRGB blend)
 *   P12 = Orange (590nm), hue 32
 *   P19 = Orange (595nm), hue 30
 *   P1  = Green (525nm), hue 96
 */

namespace {

// Brightness scaling
constexpr uint8_t BLIP_BRIGHTNESS = 255;   // Full brightness for blips
constexpr uint8_t SWEEP_BRIGHTNESS = 90;   // ~35% for sweep trail

// Map palette index (0-255) to normalized time (0.0-1.0)
inline float indexToTime(int index) {
    return static_cast<float>(index) / 255.0f;
}

// Exponential decay: I(t) = I₀ × exp(-t/τ)
// tau is in normalized time units (0-1 range)
inline float exponentialDecay(float t, float tau) {
    return expf(-t / tau);
}

// Inverse power law: I(t) = I₀ / (1 + t/τ)^n
// tau is time constant, n is power exponent
inline float inversePowerDecay(float t, float tau, float n) {
    return 1.0f / powf(1.0f + t / tau, n);
}

/**
 * P7 (WWII Radar) - Dual layer cascade phosphor
 *
 * Physics: Blue-white flash layer excites yellow-green persistent layer
 * Decay: Inverse power law, I(t) = I₀ / (1 + t/τ)^n where n ≈ 1
 *
 * From authentic-ppi-radar-display-physics.md:
 *   Start: RGB(200, 200, 255) blue-white
 *   Mid:   RGB(150, 200, 100) transitioning
 *   Glow:  RGB(100, 180, 50)  yellow-green
 *   Dim:   RGB(50, 100, 25)   fading yellow-green
 */
void generateP7(CRGBPalette256& blip, CRGBPalette256& sweep) {
    // P7 decay parameters (inverse power law)
    const float tau = 0.15f;  // Time constant
    const float n = 1.0f;     // Power exponent

    // P7 cascade colors (gamma-corrected for LED display)
    // Blue-white flash → yellow-green afterglow
    const CRGB flashColor = CRGB(200, 200, 255);   // Blue-white initial flash
    const CRGB glowColor = CRGB(100, 180, 50);     // Yellow-green afterglow

    for (int i = 0; i < 256; i++) {
        float t = indexToTime(i);
        float intensity = inversePowerDecay(t, tau, n);

        // Color transition: flash→glow happens in first 10% of decay
        // After that, it's pure yellow-green fading out
        float colorBlend = fminf(t * 10.0f, 1.0f);  // 0→1 over first 10%

        // Blend from flash to glow color
        CRGB baseColor = blend(flashColor, glowColor, static_cast<uint8_t>(colorBlend * 255));

        // Apply intensity decay
        uint8_t blipValue = static_cast<uint8_t>(intensity * BLIP_BRIGHTNESS);
        uint8_t sweepValue = static_cast<uint8_t>(intensity * SWEEP_BRIGHTNESS);

        blip[i] = baseColor;
        blip[i].nscale8(blipValue);
        sweep[i] = baseColor;
        sweep[i].nscale8(sweepValue);
    }
}

/**
 * P1 (Green Oscilloscope) - Fast exponential decay
 *
 * Physics: Single layer, pure green phosphor (525nm)
 * Decay: Fast exponential, τ ≈ 10ms equivalent
 */
void generateP1(CRGBPalette256& blip, CRGBPalette256& sweep) {
    const uint8_t HUE = 96;  // Green (525nm)

    // P1 decay: fast exponential
    const float tau = 0.08f;  // Very fast decay

    for (int i = 0; i < 256; i++) {
        float t = indexToTime(i);
        float intensity = exponentialDecay(t, tau);
        uint8_t blipValue = static_cast<uint8_t>(intensity * BLIP_BRIGHTNESS);
        uint8_t sweepValue = static_cast<uint8_t>(intensity * SWEEP_BRIGHTNESS);

        blip[i] = CHSV(HUE, 255, blipValue);
        sweep[i] = CHSV(HUE, 255, sweepValue);
    }
}

/**
 * P12 (Orange Medium) - Medium persistence exponential
 *
 * Physics: Orange phosphor (590nm) with 2-5 second persistence
 * Decay: Exponential, τ ≈ 1-2s equivalent
 */
void generateP12(CRGBPalette256& blip, CRGBPalette256& sweep) {
    const uint8_t HUE = 32;  // Orange (590nm)

    // P12 decay: medium exponential
    const float tau = 0.25f;  // Medium decay

    for (int i = 0; i < 256; i++) {
        float t = indexToTime(i);
        float intensity = exponentialDecay(t, tau);
        uint8_t blipValue = static_cast<uint8_t>(intensity * BLIP_BRIGHTNESS);
        uint8_t sweepValue = static_cast<uint8_t>(intensity * SWEEP_BRIGHTNESS);

        blip[i] = CHSV(HUE, 255, blipValue);
        sweep[i] = CHSV(HUE, 255, sweepValue);
    }
}

/**
 * P19 (Orange Long) - Very long persistence
 *
 * Physics: Orange phosphor (595nm) with very long persistence (>1 second)
 * Decay: Inverse power law (slower than P7)
 */
void generateP19(CRGBPalette256& blip, CRGBPalette256& sweep) {
    const uint8_t HUE = 30;  // Orange (595nm) - slightly redder than P12

    // P19 decay: slow inverse power law
    const float tau = 0.25f;  // Slower time constant
    const float n = 0.8f;     // Lower exponent = even slower decay

    for (int i = 0; i < 256; i++) {
        float t = indexToTime(i);
        float intensity = inversePowerDecay(t, tau, n);
        uint8_t blipValue = static_cast<uint8_t>(intensity * BLIP_BRIGHTNESS);
        uint8_t sweepValue = static_cast<uint8_t>(intensity * SWEEP_BRIGHTNESS);

        blip[i] = CHSV(HUE, 255, blipValue);
        sweep[i] = CHSV(HUE, 255, sweepValue);
    }
}

} // anonymous namespace

namespace PhosphorPalettes {

void generateAll(CRGBPalette256 blipPalettes[4], CRGBPalette256 sweepPalettes[4]) {
    // Index 0: P7 - Blue-white → yellow-green cascade (WWII radar)
    generateP7(blipPalettes[0], sweepPalettes[0]);

    // Index 1: P12 - Orange (590nm), medium persistence
    generateP12(blipPalettes[1], sweepPalettes[1]);

    // Index 2: P19 - Orange (595nm), very long persistence
    generateP19(blipPalettes[2], sweepPalettes[2]);

    // Index 3: P1 - Green (525nm), fast decay (oscilloscope)
    generateP1(blipPalettes[3], sweepPalettes[3]);
}

} // namespace PhosphorPalettes
