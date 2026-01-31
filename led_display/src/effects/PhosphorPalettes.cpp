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
 */

namespace {

// Brightness scaling
constexpr float BLIP_BRIGHTNESS = 255.0f;   // Full brightness for blips
constexpr float SWEEP_BRIGHTNESS = 90.0f;   // ~35% for sweep trail

// Map palette index (0-15) to normalized time (0.0-1.0)
inline float indexToTime(int index) {
    return static_cast<float>(index) / 15.0f;
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

// Interpolate between two colors based on factor (0.0-1.0)
inline CRGB lerpColor(const CRGB& a, const CRGB& b, float factor) {
    factor = fminf(1.0f, fmaxf(0.0f, factor));
    return CRGB(
        static_cast<uint8_t>(a.r + (b.r - a.r) * factor),
        static_cast<uint8_t>(a.g + (b.g - a.g) * factor),
        static_cast<uint8_t>(a.b + (b.b - a.b) * factor)
    );
}

// Scale color by intensity (0.0-1.0)
inline CRGB scaleColor(const CRGB& c, float intensity, float maxBrightness) {
    float scale = intensity * maxBrightness / 255.0f;
    return CRGB(
        static_cast<uint8_t>(c.r * scale),
        static_cast<uint8_t>(c.g * scale),
        static_cast<uint8_t>(c.b * scale)
    );
}

/**
 * P7 (WWII Radar) - Dual layer cascade phosphor
 *
 * Physics: Blue-white flash layer excites yellow-green persistent layer
 * Decay: Inverse power law, I(t) = I₀ / (1 + t/τ)^n where n ≈ 1
 *
 * Color progression:
 *   t=0.0: Blue-white RGB(200, 200, 255) - initial flash
 *   t=0.15: Yellow-green RGB(100, 180, 50) - cascade transition
 *   t=1.0: Dim green RGB(50, 100, 25) - long tail
 */
void generateP7(CRGBPalette16& blip, CRGBPalette16& sweep) {
    // P7 color keyframes
    const CRGB blueWhite(200, 200, 255);
    const CRGB yellowGreen(100, 180, 50);
    const CRGB dimGreen(50, 100, 25);

    // P7 decay parameters (inverse power law)
    const float tau = 0.15f;  // Time constant
    const float n = 1.0f;     // Power exponent

    for (int i = 0; i < 16; i++) {
        float t = indexToTime(i);
        float intensity = inversePowerDecay(t, tau, n);

        // Color transition: blue-white → yellow-green → dim green
        CRGB baseColor;
        if (t < 0.15f) {
            // Flash phase: blue-white to yellow-green
            baseColor = lerpColor(blueWhite, yellowGreen, t / 0.15f);
        } else {
            // Decay phase: yellow-green to dim green
            float phase = (t - 0.15f) / 0.85f;
            baseColor = lerpColor(yellowGreen, dimGreen, phase);
        }

        blip[i] = scaleColor(baseColor, intensity, BLIP_BRIGHTNESS);
        sweep[i] = scaleColor(baseColor, intensity, SWEEP_BRIGHTNESS);
    }
}

/**
 * P1 (Green Oscilloscope) - Fast exponential decay
 *
 * Physics: Single layer, pure green phosphor
 * Decay: Fast exponential, τ ≈ 10ms equivalent
 *
 * Color: Pure green RGB(100, 255, 100) throughout
 */
void generateP1(CRGBPalette16& blip, CRGBPalette16& sweep) {
    const CRGB pureGreen(100, 255, 100);

    // P1 decay: fast exponential
    const float tau = 0.08f;  // Very fast decay

    for (int i = 0; i < 16; i++) {
        float t = indexToTime(i);
        float intensity = exponentialDecay(t, tau);

        blip[i] = scaleColor(pureGreen, intensity, BLIP_BRIGHTNESS);
        sweep[i] = scaleColor(pureGreen, intensity, SWEEP_BRIGHTNESS);
    }
}

/**
 * P12 (Orange Medium) - Medium persistence exponential
 *
 * Physics: Orange phosphor with 1-2 second persistence
 * Decay: Exponential, τ ≈ 1-2s equivalent
 *
 * Color: Orange RGB(255, 150, 50) throughout
 */
void generateP12(CRGBPalette16& blip, CRGBPalette16& sweep) {
    const CRGB orange(255, 150, 50);

    // P12 decay: medium exponential
    const float tau = 0.25f;  // Medium decay

    for (int i = 0; i < 16; i++) {
        float t = indexToTime(i);
        float intensity = exponentialDecay(t, tau);

        blip[i] = scaleColor(orange, intensity, BLIP_BRIGHTNESS);
        sweep[i] = scaleColor(orange, intensity, SWEEP_BRIGHTNESS);
    }
}

/**
 * P19 (Orange Long) - Very long persistence
 *
 * Physics: Orange phosphor with very long persistence
 * Decay: Inverse power law (slower than P7)
 *
 * Color: Orange RGB(255, 160, 60) throughout
 */
void generateP19(CRGBPalette16& blip, CRGBPalette16& sweep) {
    const CRGB orange(255, 160, 60);

    // P19 decay: slow inverse power law
    const float tau = 0.25f;  // Slower time constant
    const float n = 0.8f;     // Lower exponent = even slower decay

    for (int i = 0; i < 16; i++) {
        float t = indexToTime(i);
        float intensity = inversePowerDecay(t, tau, n);

        blip[i] = scaleColor(orange, intensity, BLIP_BRIGHTNESS);
        sweep[i] = scaleColor(orange, intensity, SWEEP_BRIGHTNESS);
    }
}

} // anonymous namespace

namespace PhosphorPalettes {

void generateAll(CRGBPalette16 blipPalettes[4], CRGBPalette16 sweepPalettes[4]) {
    // Index 0: P7_BLUE_YELLOW
    generateP7(blipPalettes[0], sweepPalettes[0]);

    // Index 1: P12_ORANGE
    generateP12(blipPalettes[1], sweepPalettes[1]);

    // Index 2: P19_ORANGE_LONG
    generateP19(blipPalettes[2], sweepPalettes[2]);

    // Index 3: P1_GREEN
    generateP1(blipPalettes[3], sweepPalettes[3]);
}

} // namespace PhosphorPalettes
