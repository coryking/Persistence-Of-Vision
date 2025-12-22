#ifndef SOLID_ARMS_H
#define SOLID_ARMS_H

#include "Effect.h"

/**
 * Diagnostic test pattern - 20 discrete angular zones
 *
 * 360° divided into 20 patterns of 18° each:
 * - Patterns 0-3: Full RGB combinations (all LEDs)
 * - Patterns 4-7: Striped alignment tests (positions 0,4,9 only)
 * - Patterns 8-11: Arm A (Inner) individual color tests
 * - Patterns 12-15: Arm B (Middle) individual color tests
 * - Patterns 16-19: Arm C (Outer) individual color tests
 *
 * Each arm independently determines its pattern from its own angle,
 * so arms can display different patterns simultaneously.
 */
class SolidArms : public Effect {
public:
    void render(RenderContext& ctx) override;

private:
    CRGB getArmColor(uint8_t pattern, uint8_t armIndex) const;
    bool isStripedPattern(uint8_t pattern) const;
};

#endif // SOLID_ARMS_H
