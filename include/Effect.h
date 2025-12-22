#ifndef EFFECT_H
#define EFFECT_H

#include "RenderContext.h"
#include "types.h"

/**
 * Speed range for effect validity (in RPM for readability)
 * Use 0 for "no limit" on either end
 */
struct SpeedRange {
    uint16_t minRPM = 0;    // 0 = works at any slow speed
    uint16_t maxRPM = 0;    // 0 = works at any fast speed

    /**
     * Check if a speed (in µs/rev) is within this range
     * Remember: higher µs/rev = slower rotation
     */
    bool contains(interval_t microsPerRev) const {
        // Too slow: microsPerRev > threshold for minRPM
        if (minRPM > 0 && microsPerRev > RPM_TO_MICROS(minRPM)) return false;
        // Too fast: microsPerRev < threshold for maxRPM
        if (maxRPM > 0 && microsPerRev < RPM_TO_MICROS(maxRPM)) return false;
        return true;
    }
};

/**
 * Base class for all visual effects
 *
 * Effects write to ctx.arms[].pixels[] during render().
 * The main loop handles copying to the LED strip.
 */
class Effect {
public:
    virtual ~Effect() = default;

    /**
     * Called once when effect is activated
     * Use for one-time initialization, loading palettes, etc.
     */
    virtual void begin() {}

    /**
     * Called once when effect is deactivated
     * Use for cleanup if needed
     */
    virtual void end() {}

    /**
     * Override to specify valid speed range for this effect
     * Default: works at any speed (0, 0)
     *
     * Examples:
     *   {10, 200}   - Hand-spin only (10-200 RPM)
     *   {200, 3000} - Motor speed only
     *   {10, 3000}  - Works at any speed
     */
    virtual SpeedRange getSpeedRange() const { return {0, 0}; }

    /**
     * THE MAIN WORK: Called for each render cycle
     *
     * Write to ctx.arms[].pixels[] directly, or use ctx.virt() for
     * virtual pixel access.
     *
     * @param ctx Render context with timing info and pixel buffers
     */
    virtual void render(RenderContext& ctx) = 0;

    /**
     * Called once per revolution (at hall sensor trigger)
     *
     * Natural place for per-revolution state updates like
     * blob animation, pattern phase resets, etc.
     *
     * @param rpm Current revolutions per minute
     */
    virtual void onRevolution(timestamp_t usPerRev, timestamp_t timestamp, uint16_t revolutionCount) { (void)usPerRev; }
};

#endif // EFFECT_H
