#ifndef RADAR_H
#define RADAR_H

#include "Effect.h"
#include "hardware_config.h"
#include <FastLED.h>

/**
 * Authentic PPI Radar display effect
 *
 * Features:
 * - Rotating sweep beam (independent of physical disc rotation)
 * - P7 phosphor physics: blue-white flash → yellow-green decay
 * - World targets that move and spawn blips when swept
 * - Palette-based decay (no runtime math)
 */

// ============================================================
// Data Structures
// ============================================================

// Phosphor type selection
enum class PhosphorType : uint8_t {
    P7_BLUE_YELLOW = 0,   // WWII/Cold War standard
    P12_ORANGE = 1,       // Medium persistence
    P19_ORANGE_LONG = 2,  // Very long persistence
    P1_GREEN = 3,         // Classic oscilloscope
    COUNT = 4
};

// Radar preset modes
enum class RadarMode : uint8_t {
    AIRCRAFT = 0,   // ATC scope: fast sweep, green phosphor
    CLASSIC = 1,    // WWII drama: medium sweep, P7 blue-yellow
    MARINE = 2,     // Shipping lanes: slow sweep, orange phosphor
    ZOMBIE = 3,     // Waves coming at you!
    COUNT = 4
};

// Preset configuration
struct RadarPreset {
    timestamp_t sweepPeriodUs;   // Time for one full sweep rotation
    float targetSpeed;           // Units per second (wall-clock)
    uint8_t minTargets;
    uint8_t maxTargets;
    PhosphorType phosphor;
    const char* name;
};

// Normalized world coordinates: x,y in [-1, 1], display is inscribed circle
struct WorldTarget {
    float x, y;           // Position (0,0 = center, radius 1 = edge)
    float vx, vy;         // Velocity in units per second (wall-clock)
    bool active;
};

// Blip spawned when sweep passes a world target
struct Blip {
    angle_t bearing;           // Angular position when detected
    uint8_t virtualPixel;      // Radial position (0 = center, TOTAL_LOGICAL_LEDS-1 = edge)
    timestamp_t createdAt;     // When spawned (immutable)
    bool active;
};

// ============================================================
// Radar Effect Class
// ============================================================

class Radar : public Effect {
public:
    void begin() override;
    void render(RenderContext& ctx) override;
    void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) override;
    void right() override;   // Cycle phosphor type
    void left() override;   // Cycle phosphor type (reverse)
    void up() override;    // Cycle to next preset mode
    void down() override;  // Cycle to previous preset mode
    bool requiresFullBrightness() const override { return true; }

private:
    // === Configuration ===
    static constexpr int MAX_WORLD_TARGETS = 25;  // Zombie mode needs up to 25
    static constexpr int MAX_BLIPS = 100;  // ~4 blips per target fade time

    // Trail decay time in microseconds (~5 seconds for P7 phosphor)
    static constexpr timestamp_t SWEEP_DECAY_TIME_US = 5000000ULL;

    // Blip lifetime (slightly longer than sweep decay for overlap)
    static constexpr timestamp_t MAX_BLIP_LIFETIME_US = 6000000ULL;

    // === State ===
    timestamp_t lastRevolutionTime = 0;
    interval_t currentMicrosPerRev = 46000;  // ~1300 RPM default
    timestamp_t lastUpdateTime = 0;  // For wall-clock delta in target movement

    // Preset mode
    RadarMode currentMode = RadarMode::CLASSIC;

    // Phosphor selection (set by preset)
    PhosphorType currentPhosphorType = PhosphorType::P7_BLUE_YELLOW;

    // World targets
    WorldTarget worldTargets[MAX_WORLD_TARGETS];
    uint8_t targetCount = 3;  // Adjustable via paramUp/paramDown (0-5)

    // Blips
    Blip blips[MAX_BLIPS];

    // Random seed for target initialization
    uint16_t randomSeed = 12345;

    // === Palette Arrays (generated at runtime in begin()) ===
    CRGBPalette256 blipPalettes[4];   // One per PhosphorType (full brightness)
    CRGBPalette256 sweepPalettes[4];  // One per PhosphorType (dimmer)

    // === Helper Methods ===

    /**
     * Get color from phosphor palette based on decay time
     * @param ageUs Age of the phosphor in microseconds
     * @param maxAgeUs Maximum lifetime before fully dark
     * @param forSweep true = use dim sweep palette, false = use bright blip palette
     * @return CRGB16 color from palette lookup
     */
    CRGB16 getPhosphorColor(timestamp_t ageUs, timestamp_t maxAgeUs, bool forSweep) const;

    /**
     * Pre-compute blip contributions into blipAccum buffer.
     * Uses inverted loop: iterate active blips O(numActive), not O(LEDs × MAX_BLIPS).
     * @param ctx Render context with arm angles and slot size
     * @param now Current timestamp for age calculation
     */
    void renderBlipsToBuffer(const RenderContext& ctx, timestamp_t now);

    /**
     * Update world target positions based on wall-clock time.
     * Handles respawn when targets exit bounds or reach center (zombie mode).
     * @param now Current timestamp
     * @param preset Current radar preset configuration
     */
    void updateWorldTargets(timestamp_t now, const RadarPreset& preset);

    // === Blip accumulation buffer ===
    // Pre-computed blip colors per LED, filled by renderBlipsToBuffer()
    CRGB16 blipAccum[HardwareConfig::NUM_ARMS][HardwareConfig::LEDS_PER_ARM];

    /**
     * Initialize or respawn a world target at random position/velocity
     */
    void initWorldTarget(WorldTarget& target);

    /**
     * Convert world target position to polar coordinates
     * @param target World target
     * @param bearing Output: angular position in angle units
     * @param range Output: virtual pixel position (0 = center)
     * @return true if target is within display bounds
     */
    bool worldToPolar(const WorldTarget& target, angle_t& bearing, uint8_t& range) const;

    /**
     * Check if sweep just crossed a target's bearing
     * @param oldSweep Previous sweep angle
     * @param newSweep Current sweep angle
     * @param targetBearing Target's angular position
     * @return true if sweep crossed the target
     */
    bool sweepCrossedBearing(angle_t oldSweep, angle_t newSweep, angle_t targetBearing) const;

    /**
     * Find an inactive blip slot
     * @return Pointer to inactive blip, or nullptr if all slots full
     */
    Blip* findFreeBlip();

    /**
     * Simple pseudo-random number generator
     */
    uint16_t nextRandom();
    float randomFloat();  // Returns 0.0 to 1.0

    /**
     * Apply current preset mode settings
     * Sets phosphor type, target count, and reinitializes targets
     */
    void applyPreset();
};

#endif // RADAR_H
