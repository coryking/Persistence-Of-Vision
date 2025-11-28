#ifndef RENDER_CONTEXT_H
#define RENDER_CONTEXT_H

#include <FastLED.h>
#include "types.h"

/**
 * Render context passed to every effect
 *
 * Exposes the physical reality: 3 arms, each at its own angle,
 * each with 10 radial LEDs. Effects write to arms[].pixels[].
 *
 * Ownership: Context owns the pixel buffers. Effects write to them.
 * After render() returns, the caller copies to the actual LED strip.
 */
struct RenderContext {
    // === Timing ===
    uint32_t timeUs;              // Current timestamp (microseconds)
    interval_t microsPerRev;      // Microseconds per revolution

    // === Convenience Methods ===

    /**
     * Current rotation speed in RPM
     */
    float rpm() const {
        if (microsPerRev == 0) return 0.0f;
        return 60000000.0f / static_cast<float>(microsPerRev);
    }

    /**
     * Angular resolution: how many degrees does one render cover?
     *
     * @param renderTimeUs Time taken for one render cycle (microseconds)
     * @return Degrees covered per render at current RPM
     *
     * Examples at 50µs render time:
     *   2800 RPM: ~0.84° per render
     *   700 RPM:  ~0.21° per render
     */
    float degreesPerRender(uint32_t renderTimeUs) const {
        if (microsPerRev == 0) return 0.0f;
        float revsPerMicro = 1.0f / static_cast<float>(microsPerRev);
        return static_cast<float>(renderTimeUs) * revsPerMicro * 360.0f;
    }

    // === The Three Arms (physical reality) ===
    struct Arm {
        float angle;              // THIS arm's current angle (0-360 degrees)
        CRGB pixels[10];          // THIS arm's LEDs: [0]=hub, [9]=tip
    } arms[3];                    // [0]=inner(+120°), [1]=middle(0°), [2]=outer(+240°)

    // === Virtual Pixel Access ===
    //
    // Virtual pixels 0-29 map to physical LEDs in radial order:
    //   virt 0  = arm0:led0 (innermost)
    //   virt 1  = arm1:led0
    //   virt 2  = arm2:led0
    //   virt 3  = arm0:led1
    //   ...
    //   virt 29 = arm2:led9 (outermost)
    //
    // Note: These 30 "virtual pixels" are at 3 different angular
    // positions right now! The virtual line only exists when spinning.

    /**
     * Access virtual pixel by position (0-29)
     */
    CRGB& virt(uint8_t v) {
        return arms[v % 3].pixels[v / 3];
    }

    const CRGB& virt(uint8_t v) const {
        return arms[v % 3].pixels[v / 3];
    }

    /**
     * Fill virtual pixel range with solid color
     *
     * @param start First virtual pixel (inclusive)
     * @param end Last virtual pixel (exclusive)
     * @param color Color to fill
     */
    void fillVirtual(uint8_t start, uint8_t end, CRGB color) {
        for (uint8_t v = start; v < end && v < 30; v++) {
            virt(v) = color;
        }
    }

    /**
     * Fill virtual pixel range with gradient from palette
     *
     * @param start First virtual pixel (inclusive)
     * @param end Last virtual pixel (exclusive)
     * @param palette Color palette to sample
     * @param paletteStart Palette index for start pixel (0-255)
     * @param paletteEnd Palette index for end pixel (0-255)
     */
    void fillVirtualGradient(uint8_t start, uint8_t end,
                             const CRGBPalette16& palette,
                             uint8_t paletteStart = 0,
                             uint8_t paletteEnd = 255) {
        if (end <= start) return;
        for (uint8_t v = start; v < end && v < 30; v++) {
            uint8_t palIdx = map(v - start, 0, end - start - 1, paletteStart, paletteEnd);
            virt(v) = ColorFromPalette(palette, palIdx);
        }
    }

    /**
     * Clear all pixels to black
     */
    void clear() {
        for (auto& arm : arms) {
            memset(arm.pixels, 0, sizeof(arm.pixels));
        }
    }
};

#endif // RENDER_CONTEXT_H
