#include "effects/Kaleidoscope.h"
#include <FastLED.h>
#include "polar_helpers.h"
#include "esp_log.h"

static const char* TAG = "KALEIDOSCOPE";

void Kaleidoscope::begin() {
    palette = SharedPalettes::PALETTES[paletteIndex];
    ESP_LOGI(TAG, "Initialized - Pattern: Star, Palette: %s, Folds: %d",
             SharedPalettes::PALETTE_NAMES[paletteIndex], folds);
}

uint8_t Kaleidoscope::computePattern(uint8_t angleByte, uint8_t radiusByte,
                                     uint8_t twist, uint8_t rings, uint8_t timeByte) {
    switch (patternMode) {
        case 0:  // Star - Sharp N-pointed geometric star
            return triwave8(angleByte);

        case 1:  // Flower - Soft lobes that linger at extremes
            return cubicwave8(angleByte);

        case 2:  // Spiral - Twisted star (twist via beatsin8)
            return triwave8(angleByte + (radiusByte * twist) / 255);

        case 3:  // Diamond - Angular × radial interference lattice
            return qadd8(triwave8(angleByte), triwave8((radiusByte * rings) / 255));

        case 4:  // Ripple - Expanding/contracting concentric ripples
            return sin8(angleByte) + sin8((radiusByte * rings) / 255 + timeByte);

        case 5:  // Warp - Geometric + noise perturbation
            return triwave8(angleByte + inoise8(radiusByte * 3, timeByte));

        default:
            return triwave8(angleByte);
    }
}

void IRAM_ATTR Kaleidoscope::render(RenderContext& ctx) {
    // Pre-compute animated values ONCE per frame
    uint8_t rotation = cyclePhase;                    // Updated in onRevolution()
    uint8_t twist    = beatsin8(7, 20, 80);           // Spiral twist amount
    uint8_t rings    = beatsin8(5, 2, 6);             // Radial frequency
    uint8_t timeByte = (uint8_t)(millis() / 30);      // Slow time for ripple/warp

    for (int armIdx = 0; armIdx < HardwareConfig::NUM_ARMS; armIdx++) {
        auto& arm = ctx.arms[armIdx];

        // Map angle to 8-bit with N-fold symmetry + rotation offset
        uint8_t angleByte = (uint8_t)(
            ((uint32_t)arm.angle * folds * 256UL) / ANGLE_FULL_CIRCLE
        ) + rotation;

        for (int led = 0; led < HardwareConfig::ARM_LED_COUNT[armIdx]; led++) {
            uint8_t virtualPos = armLedToVirtual(armIdx, led);
            uint8_t radiusByte = (uint8_t)(
                ((uint16_t)virtualPos * 255) / (HardwareConfig::TOTAL_LOGICAL_LEDS - 1)
            );

            uint8_t patternVal = computePattern(angleByte, radiusByte,
                                                twist, rings, timeByte);

            // Scale 8-bit pattern value to 16-bit palette index for smooth blending
            uint16_t palIdx = ((uint16_t)patternVal) << 8;
            arm.pixels[led] = ColorFromPalette16(palette, palIdx, 255, LINEARBLEND);
        }
    }
}

void Kaleidoscope::onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) {
    // Smooth rotation - increment each revolution
    cyclePhase++;
}

void Kaleidoscope::right() {
    patternMode = (patternMode + 1) % PATTERN_COUNT;
    const char* patternNames[] = {"Star", "Flower", "Spiral", "Diamond", "Ripple", "Warp"};
    ESP_LOGI(TAG, "Pattern -> %s (%d)", patternNames[patternMode], patternMode);
}

void Kaleidoscope::left() {
    patternMode = (patternMode + PATTERN_COUNT - 1) % PATTERN_COUNT;
    const char* patternNames[] = {"Star", "Flower", "Spiral", "Diamond", "Ripple", "Warp"};
    ESP_LOGI(TAG, "Pattern -> %s (%d)", patternNames[patternMode], patternMode);
}

void Kaleidoscope::up() {
    paletteIndex = (paletteIndex + 1) % SharedPalettes::PALETTE_COUNT;
    palette = SharedPalettes::PALETTES[paletteIndex];
    ESP_LOGI(TAG, "Palette -> %s (%d)", SharedPalettes::PALETTE_NAMES[paletteIndex], paletteIndex);
}

void Kaleidoscope::down() {
    paletteIndex = (paletteIndex + SharedPalettes::PALETTE_COUNT - 1) % SharedPalettes::PALETTE_COUNT;
    palette = SharedPalettes::PALETTES[paletteIndex];
    ESP_LOGI(TAG, "Palette -> %s (%d)", SharedPalettes::PALETTE_NAMES[paletteIndex], paletteIndex);
}

void Kaleidoscope::enter() {
    foldsIndex = (foldsIndex + 1) % FOLD_COUNT;
    folds = FOLD_OPTIONS[foldsIndex];
    ESP_LOGI(TAG, "Folds -> %d", folds);
}
