#ifndef ARM_ALIGNMENT_H
#define ARM_ALIGNMENT_H

#include "Effect.h"

/**
 * ArmAlignment - Comprehensive diagnostic test for POV display
 *
 * PURPOSE:
 * After hardware changes (hall sensor repositioning, arm rewiring, LED reversal),
 * this effect validates both angular alignment and LED wiring order.
 *
 * TEST SEQUENCE:
 *
 * 1. ARM DISPLAY TEST (Phases 0-2): 15 seconds total
 *    Each arm displays individually for 5 seconds, showing:
 *    - Arm 0 (outer): RED band fading from 0° to ±120°
 *    - Arm 1 (middle): GREEN band fading from 0° to ±120°
 *    - Arm 2 (inside): BLUE band fading from 0° to ±120°
 *    - WHITE/ORANGE spike at 0° (hall crossing) for crisp visual marker
 *    - BLACK opposite side (±120° to 180°) for contrast
 *    - 0.5 second fade-to-black between each arm
 *
 *    WHAT TO LOOK FOR:
 *    - If correctly aligned: All three white spikes appear at SAME angle
 *    - If misaligned: White spikes at different angles (shows which arm is wrong)
 *    - Colored arc identifies which arm (R/G/B), white spike shows alignment
 *
 * 2. WALKING PIXEL TEST (Phase 3): Until complete, then loops
 *    Single pixel walks from hub to tip, INTERLEAVED across arms:
 *    - Position 0: arm[2] LED 0 (inside, innermost) - BLUE
 *    - Position 1: arm[1] LED 0 (middle, innermost) - GREEN
 *    - Position 2: arm[0] LED 0 (outer, innermost) - RED
 *    - Position 3: arm[2] LED 1 (second from hub) - BLUE
 *    - ... and so on in groups of three per radial distance
 *    - After position 32, loops back to Phase 0 (arm display test)
 *
 *    WHAT TO LOOK FOR:
 *    - Pixel moves in groups of 3: inside→middle→outer, then next radial band
 *    - Should form three interleaved radial lines (one per arm)
 *    - Each line colored for its arm (BLUE/GREEN/RED)
 *    - If LED reversal is wrong: one line will jump backward instead of outward
 *
 * FIXING MISALIGNMENT:
 * 1. Identify which arm is which by physical position (inner/middle/outer)
 * 2. Measure angular separation between bright spots
 * 3. Adjust phase offsets in include/types.h:
 *    - OUTER_ARM_PHASE (currently 240°)
 *    - INSIDE_ARM_PHASE (currently 120°)
 *    - Middle arm is always 0° (hall reference)
 */
class ArmAlignment : public Effect {
public:
    void begin() override;
    void end() override { }
    void render(RenderContext& ctx) override;
    void onRevolution(timestamp_t usPerRev, timestamp_t timestamp,
                     uint16_t revolutionCount) override;

private:
    // State machine
    timestamp_t phaseStartTime = 0;
    uint8_t currentPhase = 0;        // 0-2=arm display, 3=walking pixel
    bool inFadeTransition = false;

    // Fade control
    uint8_t fadeLevel = 255;

    // Walking pixel (0-32 for 3 arms × 11 LEDs)
    uint8_t walkingPixelPosition = 0;
    uint16_t lastSeenRevolution = 0;

    // Rendering helpers
    void renderArmOnly(RenderContext& ctx, uint8_t armIndex);
    void renderWalkingPixel(RenderContext& ctx);
};

#endif // ARM_ALIGNMENT_H
