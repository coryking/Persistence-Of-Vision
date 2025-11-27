#ifndef RENDER_CONTEXT_H
#define RENDER_CONTEXT_H

/**
 * @brief Unified rendering context passed to all effect render functions
 *
 * Contains timing information and arm positions needed by effects to render
 * the current frame on the POV display.
 */
struct RenderContext {
    unsigned long currentMicros;        // Current time in microseconds
    float innerArmDegrees;              // Inner arm position in degrees (0-360)
    float middleArmDegrees;             // Middle arm position in degrees (0-360)
    float outerArmDegrees;              // Outer arm position in degrees (0-360)
    unsigned long microsecondsPerRev;   // Time per revolution in microseconds
};

#endif // RENDER_CONTEXT_H
