#include "effects/NoiseField.h"
#include <FastLED.h>
#include "fl/noise.h"  // noiseCylinderCRGB (new in master, post-3.10.3)
#include "fl/map_range.h"
#include "polar_helpers.h"

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
            uint16_t paletteIndex = noiseCylinderPalette16(angleRadians, height, noiseTimeOffsetMs, radius);

            // Map to color via palette with linear blending (16-bit precision)
            CRGB color = ColorFromPaletteExtended(palette, paletteIndex, 255, LINEARBLEND);
            arm.pixels[led] = color;
#ifdef ENABLE_TIMING_INSTRUMENTATION
            int64_t noiseEnd = esp_timer_get_time();
            // print rotation number, arm, led, virtual pos, noise time using proper %llu, %d, %u, etc. formatting
            Serial.printf("NoiseField::render: frame: %lu, arm: %d, led: %d, virtualPos: %u, angle: %.4f, height: %.4f, timeOffset: %u, paletteIdx: %u, noise time: %lld us\n",
                          ctx.frameCount, armIdx, led, virtualPos, angleRadians, height, noiseTimeOffsetMs, paletteIndex, noiseEnd - noiseStart);
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

  // Switch palette every PALETTE_SWITCH_SECONDS
  uint32_t seconds = timestamp / 1000000;
  uint8_t newPaletteIndex = (seconds / PALETTE_SWITCH_SECONDS) % NOISE_PALETTE_COUNT;
  if (newPaletteIndex != paletteIndex) {
    paletteIndex = newPaletteIndex;
    palette = *NOISE_PALETTES[paletteIndex];
  }

#ifdef ENABLE_TIMING_INSTRUMENTATION
  Serial.printf("NoiseField::onRevolution: revCount: %u, timestamp: %llu us, paletteIdx: %u\n",
                revolutionCount, timestamp, paletteIndex);
#endif
}
