#ifndef PULSE_CHASER_H
#define PULSE_CHASER_H

#include "Effect.h"
#include "types.h"

/**
 * Pulse Chaser - Hand-spin effect with chasing pulses
 *
 * Each hall sensor trigger spawns a pulse at 0° that travels around
 * the circle at its birth-speed. Old pulses continue at their original
 * speed, creating layered trails as the disc winds down.
 *
 * Visual behavior:
 * - Fast spin: overlapping pulses create continuous comet tail
 * - Slow down: old fast pulses "lap" new slower ones
 * - Wind down: pulses fade as they age
 */
class PulseChaser : public Effect {
public:
    void begin() override;
    void render(RenderContext& ctx) override;
    void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) override;
    SpeedRange getSpeedRange() const override { return {10, 200}; }  // Hand-spin only

private:
    static constexpr int MAX_PULSES = 4;
    static constexpr angle_t PULSE_WIDTH_UNITS = 450;  // 45 degrees wide
    static constexpr uint32_t FADE_REVOLUTIONS = 3;    // Fade over 3 revolutions worth of time

    struct Pulse {
        timestamp_t spawnTime;      // When born (0 = empty slot)
        interval_t spawnSpeed;      // microsPerRev at birth
        uint8_t hue;                // Color hue (speed-based)
    };

    Pulse pulses[MAX_PULSES];
    uint8_t nextPulseIndex;

    uint8_t speedToHue(interval_t microsPerRev) const;
};

#endif // PULSE_CHASER_H
