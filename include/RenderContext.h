#ifndef RENDER_CONTEXT_H
#define RENDER_CONTEXT_H

#include <FastLED.h>

/**
 * @brief Unified rendering context passed to all effect render functions
 *
 * Contains timing information, arm positions, and LED buffer for effects
 * to render the current frame on the POV display.
 */
struct RenderContext {
    unsigned long currentMicros;        // Current time in microseconds
    float innerArmDegrees;              // Inner arm position in degrees (0-360)
    float middleArmDegrees;             // Middle arm position in degrees (0-360)
    float outerArmDegrees;              // Outer arm position in degrees (0-360)
    unsigned long microsecondsPerRev;   // Time per revolution in microseconds

    // FastLED buffer - effects render to this
    CRGB* leds;                         // Pointer to external LED buffer (30 elements)
};

#endif // RENDER_CONTEXT_H
