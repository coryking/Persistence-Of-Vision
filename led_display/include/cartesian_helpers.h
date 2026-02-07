#ifndef CARTESIAN_HELPERS_H
#define CARTESIAN_HELPERS_H

#include "geometry.h"

/**
 * Cartesian coordinate helpers for POV display effects
 *
 * These helpers convert between physical polar coordinates and Cartesian
 * pixel-space coordinates for effects that operate in rectangular space.
 */

// The display has ~4 "virtual" pixels in the center hole + 40 real LED rings
static constexpr int HOLE_PIXELS = 4;
static constexpr int DISPLAY_PIXELS = 40;
static constexpr int PIXELS_TO_EDGE = HOLE_PIXELS + DISPLAY_PIXELS;  // 44

/**
 * Convert physical radius (mm) to "pixel units" for coordinate system
 * @param radius_mm Physical radius from RadialGeometry
 * @return Radius in pixel units (where edge = PIXELS_TO_EDGE)
 */
static inline float radiusToPixels(float radius_mm) {
    // Scale: outermost LED center is at PIXELS_TO_EDGE pixels
    return radius_mm * PIXELS_TO_EDGE / RadialGeometry::OUTERMOST_LED_CENTER_MM;
}

#endif // CARTESIAN_HELPERS_H
