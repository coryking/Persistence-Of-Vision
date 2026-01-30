#ifndef RADAR_H
#define RADAR_H

#include "Effect.h"
#include "hardware_config.h"

/**
 * Vintage radar display effect
 *
 * Features:
 * - Rotating sweep beam (independent of physical disc rotation)
 * - Phosphor decay trail behind sweep
 * - Moving ghost targets that bloom when sweep passes
 * - Range rings at fixed radial positions
 *
 * Controls:
 * - Up/Down: Target density (0-3)
 */
class Radar : public Effect {
public:
    void begin() override;
    void render(RenderContext& ctx) override;
    void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) override;
    void nextMode() override;  // Unused (sweep speed locked)
    void prevMode() override;  // Unused (sweep speed locked)
    void paramUp() override;   // Increase target density
    void paramDown() override; // Decrease target density

private:
    // === Color Palette (high contrast vintage) ===
    static constexpr CRGB SWEEP_COLOR = CRGB(255, 255, 255);       // Pure white sweep
    static constexpr CRGB PHOSPHOR_BRIGHT = CRGB(0, 180, 60);      // Darker phosphor green
    static constexpr CRGB PHOSPHOR_DIM = CRGB(0, 40, 15);          // Much dimmer trail end
    static constexpr CRGB RANGE_RING_COLOR = CRGB(10, 20, 35);     // Blue-grey rings
    static constexpr CRGB TARGET_COLOR = CRGB(255, 80, 30);        // Red/orange targets

    // === Sweep Configuration ===
    static constexpr uint32_t SWEEP_PERIOD_US = 4000000;  // 4 seconds per rotation (locked)

    // Sweep state (tracked via timestamp)
    timestamp_t sweepStartTime = 0;

    // === Phosphor Trail ===
    static constexpr angle_t TRAIL_WIDTH = ANGLE_UNITS(90);  // 90 degree trail

    // === Target Configuration ===
    static constexpr uint8_t MAX_TARGETS = 8;
    static constexpr uint8_t DENSITY_MODE_COUNT = 4;  // 0=none, 1=sparse, 2=medium, 3=busy
    uint8_t densityMode = 2;  // Start at medium

    // Target spawn timing (per density level)
    static constexpr uint8_t SPAWN_INTERVAL_REVS[DENSITY_MODE_COUNT] = {
        255,  // Density 0: never spawn
        8,    // Density 1: every 8 revolutions
        4,    // Density 2: every 4 revolutions
        2     // Density 3: every 2 revolutions
    };

    // Target decay time (microseconds)
    static constexpr timestamp_t TARGET_DECAY_US = 3000000;  // 3 seconds

    // Angular bloom window (how close sweep must be to trigger bloom)
    static constexpr angle_t BLOOM_WINDOW = ANGLE_UNITS(5);  // 5 degrees

    struct RadarTarget {
        angle_t angle;           // 0-3599 units (angular position)
        uint8_t vPixel;          // Virtual pixel (0 to TOTAL_LOGICAL_LEDS-1)
        int16_t angularVelocity; // Angle units per second (positive = CW)
        int8_t radialVelocity;   // Virtual pixels per 10 seconds (positive = outward)
        timestamp_t lastHitTime; // For decay calculation
        timestamp_t lastMoveTime;// For position updates
        uint8_t strength;        // 0-255 visibility
        bool active;
    };

    // Target movement speed range
    static constexpr int16_t MIN_ANGULAR_VELOCITY = -150;  // ~15 deg/sec CCW
    static constexpr int16_t MAX_ANGULAR_VELOCITY = 150;   // ~15 deg/sec CW
    static constexpr int8_t MIN_RADIAL_VELOCITY = -20;     // ~2 pixels/sec inward
    static constexpr int8_t MAX_RADIAL_VELOCITY = 20;      // ~2 pixels/sec outward

    RadarTarget targets[MAX_TARGETS];
    uint8_t nextTargetSlot = 0;
    uint16_t revolutionsSinceSpawn = 0;

    // Random seed for target generation
    uint32_t randomSeed = 12345;

    // === Helper Methods ===

    /**
     * Get current sweep angle based on timestamp
     */
    angle_t getSweepAngle(timestamp_t now) const;

    /**
     * Spawn a new target at random position
     */
    void spawnTarget(timestamp_t now);

    /**
     * Update target states (bloom when sweep passes, decay over time)
     */
    void updateTargets(angle_t sweepAngle, timestamp_t now);

    /**
     * Update target positions based on velocity
     */
    void moveTargets(timestamp_t now);

    /**
     * Simple LCG random number generator
     */
    uint32_t nextRandom();

    /**
     * Check if virtual pixel is on a range ring
     */
    bool isRangeRing(uint8_t vPixel) const;
};

#endif // RADAR_H
