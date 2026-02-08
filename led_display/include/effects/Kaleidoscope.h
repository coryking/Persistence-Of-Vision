#pragma once

#include "Effect.h"
#include "effects/SharedPalettes.h"

/**
 * Kaleidoscope Effect - N-fold symmetric geometric patterns
 *
 * Multiplies the angle by N (fold count) before feeding into waveform generators
 * (triwave8, cubicwave8, sin8) to create symmetric geometric patterns that morph
 * organically over time.
 *
 * The POV disc hardware IS a kaleidoscope — this effect leverages that.
 *
 * Controls:
 *   - Left/Right: Cycle pattern mode (6 geometric patterns)
 *   - Up/Down: Cycle palette (shared collection, ~18 palettes)
 *   - ENTER: Cycle fold count (3→4→5→6→8→10→12)
 *
 * Patterns:
 *   0. Star - Sharp N-pointed geometric star
 *   1. Flower - Soft lobes that linger at extremes
 *   2. Spiral - Twisted star (twist via beatsin8)
 *   3. Diamond - Angular × radial interference lattice
 *   4. Ripple - Expanding/contracting concentric ripples
 *   5. Warp - Geometric + noise perturbation
 */
class Kaleidoscope : public Effect {
public:
    void render(RenderContext& ctx) override;
    void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) override;
    void begin() override;

    void right() override;  // Next pattern
    void left() override;   // Previous pattern
    void up() override;     // Next palette
    void down() override;   // Previous palette
    void enter() override;  // Cycle fold count

private:
    uint8_t patternMode = 0;     // 0-5 (6 patterns)
    uint8_t paletteIndex = 12;   // Start on Rainbow (first full-spectrum palette)
    uint8_t foldsIndex = 3;      // Index into FOLD_OPTIONS[] (default = 6 folds)
    uint8_t folds = 6;           // Active fold count
    CRGBPalette16 palette;       // Active palette (initialized in begin())
    uint8_t cyclePhase = 0;      // Rotation phase (incremented each revolution)

    static constexpr uint8_t FOLD_OPTIONS[] = {3, 4, 5, 6, 8, 10, 12};
    static constexpr uint8_t FOLD_COUNT = sizeof(FOLD_OPTIONS) / sizeof(FOLD_OPTIONS[0]);
    static constexpr uint8_t PATTERN_COUNT = 6;

    /**
     * Compute pattern value (0-255) based on pattern mode
     * @param angleByte 8-bit angle with N-fold symmetry baked in (0-255)
     * @param radiusByte 8-bit radius (0=hub, 255=tip)
     * @param twist Twist amount for Spiral mode
     * @param rings Radial frequency for Diamond/Ripple modes
     * @param timeByte Time value for animated patterns
     * @return 8-bit palette index
     */
    uint8_t computePattern(uint8_t angleByte, uint8_t radiusByte,
                          uint8_t twist, uint8_t rings, uint8_t timeByte);
};
