#ifndef EFFECT_H
#define EFFECT_H

#include "RenderContext.h"

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

    /**
     * Cycle to next internal mode (if effect supports modes)
     * Called via ESP-NOW from IR remote left/right buttons
     * Default: no-op (effect has no modes)
     */
    virtual void nextMode() {}

    /**
     * Cycle to previous internal mode (if effect supports modes)
     * Called via ESP-NOW from IR remote left/right buttons
     * Default: no-op (effect has no modes)
     */
    virtual void prevMode() {}

    /**
     * Increment effect's secondary parameter (effect-specific)
     * Called via ESP-NOW from IR remote up button
     * Examples: next palette, increase speed, zoom in
     * Default: no-op
     */
    virtual void paramUp() {}

    /**
     * Decrement effect's secondary parameter (effect-specific)
     * Called via ESP-NOW from IR remote down button
     * Examples: prev palette, decrease speed, zoom out
     * Default: no-op
     */
    virtual void paramDown() {}
};

#endif // EFFECT_H
