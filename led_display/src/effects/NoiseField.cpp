#include "effects/NoiseField.h"
#include <FastLED.h>
#include "fl/noise.h"  // noiseCylinderCRGB (new in master, post-3.10.3)
#include "fl/map_range.h"
#include "polar_helpers.h"

// =============================================================================
// Contrast transformation functions (all integer math)
// =============================================================================

/**
 * S-curve contrast - pushes middle values toward extremes
 * Input/output: 0-65535
 * Uses cubic function for smooth S-shape
 */
static inline uint16_t applySCurve(uint16_t val) {
    // Center around zero: 0-65535 -> -32768 to 32767
    int32_t centered = (int32_t)val - 32768;

    // Normalize to -1.0 to 1.0 range (as fixed point)
    // Then apply x^3 which creates S-curve (keeps sign, amplifies extremes)
    // x^3 where x is small stays small, x near ±1 stays near ±1
    int64_t cubed = ((int64_t)centered * centered * centered) >> 30;

    // Clamp and convert back
    cubed = constrain(cubed, -32768, 32767);
    return (uint16_t)(cubed + 32768);
}

/**
 * Turbulence - fold around center for hard edges
 * Input/output: 0-65535
 * Creates fractured, lightning-like patterns
 */
static inline uint16_t applyTurbulence(uint16_t val) {
    // Distance from center (0 at edges, 32768 at center)
    int32_t centered = (int32_t)val - 32768;
    int32_t folded = abs(centered) * 2;  // Scale back to 0-65535

    return (uint16_t)min(folded, (int32_t)65535);
}

/**
 * Quantize - reduce to discrete steps for stained-glass effect
 * Input/output: 0-65535
 * Creates distinct color "cells" with sharp edges
 */
static inline uint16_t applyQuantize(uint16_t val) {
    // 4 levels: 0, 21845, 43690, 65535
    return (val >> 14) * 21845;
}

/**
 * Expanded center - push values away from center toward edges
 * Input/output: 0-65535
 * More frequent accent colors, less dark
 */
static inline uint16_t applyExpanded(uint16_t val) {
    // Amplify distance from center by 1.5x
    int32_t centered = (int32_t)val - 32768;
    int32_t expanded = (centered * 3) / 2;  // 1.5x
    int32_t result = expanded + 32768;
    return (uint16_t)constrain(result, 0, 65535);
}

/**
 * Compressed center - reduce distance from center
 * Input/output: 0-65535
 * Rarer accent pops, more subtle sparkle texture (meditative)
 */
static inline uint16_t applyCompressed(uint16_t val) {
    // Reduce distance from center by 0.6x
    int32_t centered = (int32_t)val - 32768;
    int32_t compressed = (centered * 3) / 5;  // 0.6x
    return (uint16_t)(compressed + 32768);
}

/**
 * Render NoiseField effect - flowing lava texture
 *
 * Uses FastLED's noiseCylinderCRGB for seamless cylindrical noise.
 * Maps the POV disc as a cylinder where:
 *   - angle (θ) = position around the disc (radians)
 *   - height = radial position (0.0 = hub, 1.0 = tip)
 *
 * The cylinder mapping eliminates the 0°/360° seam problem.
 */
void IRAM_ATTR NoiseField::render(RenderContext& ctx) {

    for (int armIdx = 0; armIdx < 3; armIdx++) {
        auto& arm = ctx.arms[armIdx];
        float angleRadians = angleUnitsToRadians(arm.angleUnits);

        for (int led = 0; led < HardwareConfig::LEDS_PER_ARM; led++) {
#ifdef ENABLE_TIMING_INSTRUMENTATION
            int64_t noiseStart = esp_timer_get_time();
#endif
            // Use virtual position to respect radial stagger
            uint8_t virtualPos = armLedToVirtual(armIdx, led);
            float height = fl::map_range<float, float>(virtualPos, 0, 29, 0.0f, 1.0f);

            // Get 16-bit palette index from noise (single channel)
            uint16_t noiseVal = noiseCylinderPalette16(angleRadians, height, noiseTimeOffsetMs, radius);

            // Apply contrast transformation based on mode
            uint16_t palIdx;
            switch (contrastMode) {
                case 1:  palIdx = applySCurve(noiseVal); break;
                case 2:  palIdx = applyTurbulence(noiseVal); break;
                case 3:  palIdx = applyQuantize(noiseVal); break;
                case 4:  palIdx = applyExpanded(noiseVal); break;
                case 5:  palIdx = applyCompressed(noiseVal); break;
                default: palIdx = noiseVal; break;  // Mode 0: normal
            }

            // Map to color via palette with linear blending (16-bit precision)
            CRGB color = ColorFromPaletteExtended(palette, palIdx, 255, LINEARBLEND);
            arm.pixels[led] = color;
#ifdef ENABLE_TIMING_INSTRUMENTATION
            int64_t noiseEnd = esp_timer_get_time();
            // print rotation number, arm, led, virtual pos, noise time using proper %llu, %d, %u, etc. formatting
            Serial.printf("NoiseField::render: frame: %lu, arm: %d, led: %d, virtualPos: %u, angle: %.4f, height: %.4f, timeOffset: %u, paletteIdx: %u, noise time: %lld us\n",
                          ctx.frameCount, armIdx, led, virtualPos, angleRadians, height, noiseTimeOffsetMs, palIdx, noiseEnd - noiseStart);
#endif
        }
    }
}

void NoiseField::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
  noiseTimeOffsetMs  = (timestamp_t)(timestamp / 50);

  // Pulsate radius using sine wave over RADIUS_PERIOD_SECONDS
  // sin16 takes 0-65535 as input (one full cycle), returns -32768 to 32767
  uint16_t phase = (uint16_t)((timestamp % (uint64_t)RADIUS_PERIOD_US) * 65536ULL / (uint64_t)RADIUS_PERIOD_US);
  int16_t sinVal = sin16(phase);  // -32768 to 32767
  float normalized = (sinVal + 32768) / 65536.0f;  // 0.0 to 1.0
  radius = RADIUS_MIN + normalized * (RADIUS_MAX - RADIUS_MIN);

#ifdef ENABLE_TIMING_INSTRUMENTATION
  Serial.printf("NoiseField::onRevolution: revCount: %u, timestamp: %llu us, paletteIdx: %u\n",
                revolutionCount, timestamp, paletteIndex);
#endif
}

void NoiseField::nextMode() {
    contrastMode = (contrastMode + 1) % CONTRAST_MODE_COUNT;
    const char* modeNames[] = {"Normal", "S-curve", "Turbulence", "Quantize", "Expanded", "Compressed"};
    Serial.printf("[NoiseField] Contrast mode -> %s (%d)\n", modeNames[contrastMode], contrastMode);
}

void NoiseField::prevMode() {
    contrastMode = (contrastMode + CONTRAST_MODE_COUNT - 1) % CONTRAST_MODE_COUNT;
    const char* modeNames[] = {"Normal", "S-curve", "Turbulence", "Quantize", "Expanded", "Compressed"};
    Serial.printf("[NoiseField] Contrast mode -> %s (%d)\n", modeNames[contrastMode], contrastMode);
}

void NoiseField::paramUp() {
    paletteIndex = (paletteIndex + 1) % NOISE_PALETTE_COUNT;
    palette = *NOISE_PALETTES[paletteIndex];
    const char* paletteNames[] = {
        "Ember", "Abyss", "Void", "Firefly",        // Original
        "Sunset", "Ice", "Gold", "Lava",            // New hue families
        "FireIce", "Synthwave",                     // Dual-hue
        "EmberSubtle", "NeonAbyss"                  // Variations
    };
    Serial.printf("[NoiseField] Palette -> %s (%d)\n", paletteNames[paletteIndex], paletteIndex);
}

void NoiseField::paramDown() {
    paletteIndex = (paletteIndex + NOISE_PALETTE_COUNT - 1) % NOISE_PALETTE_COUNT;
    palette = *NOISE_PALETTES[paletteIndex];
    const char* paletteNames[] = {
        "Ember", "Abyss", "Void", "Firefly",        // Original
        "Sunset", "Ice", "Gold", "Lava",            // New hue families
        "FireIce", "Synthwave",                     // Dual-hue
        "EmberSubtle", "NeonAbyss"                  // Variations
    };
    Serial.printf("[NoiseField] Palette -> %s (%d)\n", paletteNames[paletteIndex], paletteIndex);
}
