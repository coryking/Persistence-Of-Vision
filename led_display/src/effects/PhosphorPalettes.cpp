#include "effects/PhosphorPalettes.h"
#include <cmath>

/**
 * Phosphor Decay Physics Implementation
 *
 * Based on docs/effects/math-for-phosphor.md
 *
 * Two decay types:
 * 1. Exponential: I(t) = I₀ × exp(-t/τ)
 *    - Used by P1 (fast), P12 (medium)
 *    - Decays quickly then fades
 *
 * 2. Inverse power law: I(t) = I₀ / (1 + t/τ)^n
 *    - Used by P7, P19 (long persistence)
 *    - Fast initial drop, then holds dim glow much longer
 *    - Creates the "trails that never quite disappear" look
 *
 * DEBUG MODE: Using distinct hues to verify decay physics:
 *   P7  = Cyan (128)    - authentic: blue→yellow cascade
 *   P12 = Magenta (192) - authentic: orange
 *   P19 = Lime (96)     - authentic: orange
 *   P1  = Red (0)       - authentic: green
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
 * DEBUG: Cyan (hue=128) to distinguish from other phosphors
 * AUTHENTIC: Blue-white (160, sat=128) at t=0, yellow-green (64) thereafter
 */
void generateP7(CRGBPalette256& blip, CRGBPalette256& sweep) {
    // DEBUG hue - cyan for easy identification
    const uint8_t DEBUG_HUE = 128;  // Authentic: 160→64 (blue→yellow cascade)

    // P7 decay parameters (inverse power law)
    const float tau = 0.15f;  // Time constant
    const float n = 1.0f;     // Power exponent

    for (int i = 0; i < 256; i++) {
        float t = indexToTime(i);
        float intensity = inversePowerDecay(t, tau, n);
        uint8_t blipValue = static_cast<uint8_t>(intensity * BLIP_BRIGHTNESS);
        uint8_t sweepValue = static_cast<uint8_t>(intensity * SWEEP_BRIGHTNESS);

        blip[i] = CHSV(DEBUG_HUE, 255, blipValue);
        sweep[i] = CHSV(DEBUG_HUE, 255, sweepValue);
    }
}

/**
 * P1 (Green Oscilloscope) - Fast exponential decay
 *
 * Physics: Single layer, pure green phosphor
 * Decay: Fast exponential, τ ≈ 10ms equivalent
 *
 * DEBUG: Red (hue=0) to distinguish from other phosphors
 * AUTHENTIC: Green (hue=96)
 */
void generateP1(CRGBPalette256& blip, CRGBPalette256& sweep) {
    // DEBUG hue - red for easy identification
    const uint8_t DEBUG_HUE = 0;  // Authentic: 96 (green)

    // P1 decay: fast exponential
    const float tau = 0.08f;  // Very fast decay

    for (int i = 0; i < 256; i++) {
        float t = indexToTime(i);
        float intensity = exponentialDecay(t, tau);
        uint8_t blipValue = static_cast<uint8_t>(intensity * BLIP_BRIGHTNESS);
        uint8_t sweepValue = static_cast<uint8_t>(intensity * SWEEP_BRIGHTNESS);

        blip[i] = CHSV(DEBUG_HUE, 255, blipValue);
        sweep[i] = CHSV(DEBUG_HUE, 255, sweepValue);
    }
}

/**
 * P12 (Orange Medium) - Medium persistence exponential
 *
 * Physics: Orange phosphor with 1-2 second persistence
 * Decay: Exponential, τ ≈ 1-2s equivalent
 *
 * DEBUG: Magenta (hue=192) to distinguish from other phosphors
 * AUTHENTIC: Orange (hue=32)
 */
void generateP12(CRGBPalette256& blip, CRGBPalette256& sweep) {
    // DEBUG hue - magenta for easy identification
    const uint8_t DEBUG_HUE = 192;  // Authentic: 32 (orange)

    // P12 decay: medium exponential
    const float tau = 0.25f;  // Medium decay

    for (int i = 0; i < 256; i++) {
        float t = indexToTime(i);
        float intensity = exponentialDecay(t, tau);
        uint8_t blipValue = static_cast<uint8_t>(intensity * BLIP_BRIGHTNESS);
        uint8_t sweepValue = static_cast<uint8_t>(intensity * SWEEP_BRIGHTNESS);

        blip[i] = CHSV(DEBUG_HUE, 255, blipValue);
        sweep[i] = CHSV(DEBUG_HUE, 255, sweepValue);
    }
}

/**
 * P19 (Orange Long) - Very long persistence
 *
 * Physics: Orange phosphor with very long persistence
 * Decay: Inverse power law (slower than P7)
 *
 * DEBUG: Lime/Yellow-green (hue=96) to distinguish from other phosphors
 * AUTHENTIC: Orange (hue=32)
 */
void generateP19(CRGBPalette256& blip, CRGBPalette256& sweep) {
    // DEBUG hue - lime/yellow-green for easy identification
    const uint8_t DEBUG_HUE = 96;  // Authentic: 32 (orange)

    // P19 decay: slow inverse power law
    const float tau = 0.25f;  // Slower time constant
    const float n = 0.8f;     // Lower exponent = even slower decay

    for (int i = 0; i < 256; i++) {
        float t = indexToTime(i);
        float intensity = inversePowerDecay(t, tau, n);
        uint8_t blipValue = static_cast<uint8_t>(intensity * BLIP_BRIGHTNESS);
        uint8_t sweepValue = static_cast<uint8_t>(intensity * SWEEP_BRIGHTNESS);

        blip[i] = CHSV(DEBUG_HUE, 255, blipValue);
        sweep[i] = CHSV(DEBUG_HUE, 255, sweepValue);
    }
}

} // anonymous namespace

namespace PhosphorPalettes {

void generateAll(CRGBPalette256 blipPalettes[4], CRGBPalette256 sweepPalettes[4]) {
    // Index 0: P7_BLUE_YELLOW (DEBUG: Cyan)
    generateP7(blipPalettes[0], sweepPalettes[0]);

    // Index 1: P12_ORANGE (DEBUG: Magenta)
    generateP12(blipPalettes[1], sweepPalettes[1]);

    // Index 2: P19_ORANGE_LONG (DEBUG: Lime)
    generateP19(blipPalettes[2], sweepPalettes[2]);

    // Index 3: P1_GREEN (DEBUG: Red)
    generateP1(blipPalettes[3], sweepPalettes[3]);
}

} // namespace PhosphorPalettes
