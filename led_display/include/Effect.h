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
     * IR remote RIGHT button
     * Default: no-op
     */
    virtual void right() {}

    /**
     * IR remote LEFT button
     * Default: no-op
     */
    virtual void left() {}

    /**
     * IR remote UP button
     * Default: no-op
     */
    virtual void up() {}

    /**
     * IR remote DOWN button
     * Default: no-op
     */
    virtual void down() {}

    /**
     * IR remote ENTER button
     * Default: no-op
     */
    virtual void enter() {}

    /**
     * Notification when display power state changes
     * @param enabled true = display on, false = display off
     * Default: no-op
     */
    virtual void onDisplayPower(bool enabled) { (void)enabled; }

    /**
     * Whether this effect requires full brightness (ignores brightness control)
     * Override to return true for effects that need maximum LED output
     * Default: false (normal brightness control)
     */
    virtual bool requiresFullBrightness() const { return false; }
};

#endif // EFFECT_H
