#ifndef ARM_RENDERER_H
#define ARM_RENDERER_H

#include "RenderContext.h"
#include "hardware_config.h"
#include <functional>

/**
 * Information about one arm for rendering
 */
struct ArmInfo {
    uint16_t ledStart;    // HardwareConfig::INNER_ARM_START, etc.
    float angle;          // ctx.innerArmDegrees, etc.
    uint8_t armIndex;     // 0, 1, 2
};

/**
 * Render all three arms with a per-LED callback
 * Eliminates ~300 lines of duplication across 4 effects
 *
 * @param ctx Render context with arm angles and LED buffer
 * @param perLedFunc Lambda called for each LED: void(physicalLed, ledIdx, arm)
 *                   - physicalLed: 0-29 global LED index
 *                   - ledIdx: 0-9 LED index within arm
 *                   - arm: ArmInfo struct with ledStart, angle, armIndex
 *
 * Example usage:
 *   renderAllArms(ctx, [&](uint16_t physicalLed, uint16_t ledIdx, const ArmInfo& arm) {
 *       ctx.leds[physicalLed] = CRGB::Red;  // Your per-LED logic here
 *   });
 */
template<typename F>
void renderAllArms(RenderContext& ctx, F&& perLedFunc) {
    ArmInfo arms[3] = {
        {HardwareConfig::INNER_ARM_START, ctx.innerArmDegrees, 0},
        {HardwareConfig::MIDDLE_ARM_START, ctx.middleArmDegrees, 1},
        {HardwareConfig::OUTER_ARM_START, ctx.outerArmDegrees, 2}
    };

    for (int armIdx = 0; armIdx < 3; armIdx++) {
        const ArmInfo& arm = arms[armIdx];

        for (uint16_t ledIdx = 0; ledIdx < HardwareConfig::LEDS_PER_ARM; ledIdx++) {
            uint16_t physicalLed = arm.ledStart + ledIdx;

            // Call per-LED function: perLedFunc(physicalLed, ledIdx, arm)
            perLedFunc(physicalLed, ledIdx, arm);
        }
    }
}

#endif
