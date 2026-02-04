#ifndef POV_GEOMETRY_H
#define POV_GEOMETRY_H

#include "types.h"  // shared/types.h via -I path (timestamp_t, interval_t, etc.)

// =============================================================================
// POV Display Geometry and Coordinate System
// Angles, arm phases, speed ranges, and rendering targets
// =============================================================================

// -----------------------------------------------------------------------------
// Angle Units: 3600 units = 360 degrees (0.1 degree precision)
// -----------------------------------------------------------------------------

// Angle in units where 3600 = 360 degrees
typedef uint16_t angle_t;

/**
 * Convert degrees to angle units (3600 units = 360 degrees)
 * Works with both integer and floating-point inputs.
 * Examples: ANGLE_UNITS(360) -> 3600, ANGLE_UNITS(360.5) -> 3605
 */
#define ANGLE_UNITS(deg) ((angle_t)((deg) * 10))
#define UNITS_TO_DEGREES(units) ((float)(units) / 10.0f)

// Angle constants
static constexpr angle_t ANGLE_FULL_CIRCLE = ANGLE_UNITS(360);
static constexpr angle_t ANGLE_HALF_CIRCLE = ANGLE_UNITS(180);
static constexpr angle_t ANGLE_QUARTER_CIRCLE = ANGLE_UNITS(90);
static constexpr angle_t ANGLE_PER_PATTERN = ANGLE_UNITS(18);      // 18 degrees = 1 pattern

// -----------------------------------------------------------------------------
// Arm Phase Offsets (3-arm display, 120 degrees apart)
// -----------------------------------------------------------------------------

// Phase offsets for each arm (indexed by physical position)
// These offsets are ADDED to angleMiddleUnits (calculated from hall trigger)
// arm[0] = outer, arm[1] = middle (implicit 0° base), arm[2] = inside
static constexpr angle_t OUTER_ARM_PHASE = ANGLE_UNITS(120);   // 120 degrees - arm[0]
static constexpr angle_t MIDDLE_ARM_PHASE = ANGLE_UNITS(0);    // 0 degrees - arm[1] (not used in calculation)
static constexpr angle_t INSIDE_ARM_PHASE = ANGLE_UNITS(240);  // 240 degrees - arm[2]

// Phase lookup table (indexed by arm position)
static constexpr angle_t ARM_PHASE[3] = {
    OUTER_ARM_PHASE,   // arm[0]
    MIDDLE_ARM_PHASE,  // arm[1]
    INSIDE_ARM_PHASE   // arm[2]
};

// -----------------------------------------------------------------------------
// Speed Constants (microseconds per revolution)
// -----------------------------------------------------------------------------

// RPM to microseconds conversion (compile-time)
// 60,000,000 µs/min ÷ RPM = µs/rev
#define RPM_TO_MICROS(rpm) (60000000ULL / (rpm))

// Motor speed range
// Lower value = faster rotation
static constexpr interval_t MICROS_PER_REV_MIN = 21428;   // ~2800 RPM (fastest)
static constexpr interval_t MICROS_PER_REV_MAX = 85714;   // ~700 RPM (slowest)

// Hand-spin speed range (extends beyond motor range)
static constexpr interval_t MICROS_PER_REV_HANDSPIN_MIN = 1000000;   // 60 RPM (fast hand spin)
static constexpr interval_t MICROS_PER_REV_HANDSPIN_MAX = 12000000;  // 5 RPM (slow hand spin)

// Speed mode threshold - below this is "slow mode" (hand-spin)
static constexpr interval_t MICROS_PER_REV_SLOW_MODE = RPM_TO_MICROS(200);  // ~300,000 µs

// Rolling average bounds for linear interpolation of window size
static constexpr interval_t MICROS_PER_REV_MIN_SAMPLES = RPM_TO_MICROS(2800);  // 20 samples (fast)
static constexpr interval_t MICROS_PER_REV_MAX_SAMPLES = RPM_TO_MICROS(50);    // 2 samples (slow)

// -----------------------------------------------------------------------------
// Radial Geometry (Physical LED Positions)
// -----------------------------------------------------------------------------
// Measured from physical calibration - see docs/led_display/HARDWARE.md
// for measurement methodology and update instructions.
//
// The display has a central "hole" because the innermost LED is not at the
// rotation center. When mapping Cartesian coordinates to polar:
//   - Points where sqrt(x² + y²) < INNER_DISPLAY_RADIUS_MM are in the blind spot
//   - Effects that assume radius 0 = center will render incorrectly
//
// Ring layout: 40 LEDs create 40 concentric rings when spinning.
// ARM3 provides rings 0,3,6,9... (14 LEDs), ARM2 provides 1,4,7... (13 LEDs),
// ARM1 provides 2,5,8... (13 LEDs).

namespace RadialGeometry {
    // -------------------------------------------------------------------------
    // LED Strip Physical Constants (fixed by LED product, don't change)
    // -------------------------------------------------------------------------
    constexpr float LED_PITCH_MM = 7.0f;        // Center-to-center spacing on strip
    constexpr float LED_CHIP_SIZE_MM = 5.0f;    // LED chip width (5mm chip + 2mm gap = 7mm pitch)

    // -------------------------------------------------------------------------
    // Calibration Data (measured values - update via pov calibration interactive)
    // -------------------------------------------------------------------------
    // Per-arm innermost LED center positions (mm from rotation center)
    // To recalibrate: measure tip-to-inner-edge, run led_calibration.py
    constexpr float ARM3_INNER_RADIUS_MM = 10.00f;  // Rings 0, 3, 6, 9...  (14 LEDs)
    constexpr float ARM2_INNER_RADIUS_MM = 13.10f;  // Rings 1, 4, 7, 10... (13 LEDs)
    constexpr float ARM1_INNER_RADIUS_MM = 15.10f;  // Rings 2, 5, 8, 11... (13 LEDs)

    // -------------------------------------------------------------------------
    // Derived Values (computed from above - do not edit directly)
    // -------------------------------------------------------------------------
    constexpr float IDEAL_RING_PITCH_MM = LED_PITCH_MM / 3.0f;

    // Innermost/outermost LED centers
    constexpr float INNERMOST_LED_CENTER_MM = ARM3_INNER_RADIUS_MM;
    constexpr float OUTERMOST_LED_CENTER_MM = ARM3_INNER_RADIUS_MM + 13 * LED_PITCH_MM;  // Ring 39

    // Display boundaries (inner/outer edges of the LED chips)
    constexpr float INNER_DISPLAY_RADIUS_MM = INNERMOST_LED_CENTER_MM - LED_CHIP_SIZE_MM / 2.0f;
    constexpr float OUTER_DISPLAY_RADIUS_MM = OUTERMOST_LED_CENTER_MM + LED_CHIP_SIZE_MM / 2.0f;
    constexpr float INNER_HOLE_DIAMETER_MM = 2.0f * INNER_DISPLAY_RADIUS_MM;
    constexpr float DISPLAY_SPAN_MM = OUTER_DISPLAY_RADIUS_MM - INNER_DISPLAY_RADIUS_MM;

    // -------------------------------------------------------------------------
    // Functions
    // -------------------------------------------------------------------------

    /**
     * Get the physical radius (mm) for a given ring index (0-39)
     * Ring 0 is innermost, Ring 39 is outermost.
     */
    inline constexpr float ringRadiusMM(int ring) {
        // ring % 3 determines arm: 0=ARM3, 1=ARM2, 2=ARM1
        // ring / 3 determines LED index on that arm
        constexpr float armBase[3] = {ARM3_INNER_RADIUS_MM, ARM2_INNER_RADIUS_MM, ARM1_INNER_RADIUS_MM};
        return armBase[ring % 3] + (ring / 3) * LED_PITCH_MM;
    }
}

// -----------------------------------------------------------------------------
// Rendering Target
// -----------------------------------------------------------------------------

/**
 * Represents a target angular position for precision-timed rendering
 *
 * The POV display renders for a FUTURE position, then waits until
 * the disc reaches that position before firing the LED update.
 * This ensures the angle told to the renderer matches where LEDs
 * actually illuminate.
 */
struct SlotTarget {
    int slotNumber;           // Which slot (0 to totalSlots-1)
    angle_t angleUnits;       // Target angle in units (0-3599)
    timestamp_t targetTime;   // When disc will be at this angle
    angle_t slotSize;         // Size of each slot in angle units
    int totalSlots;           // Total number of slots per revolution
};

#endif // POV_GEOMETRY_H
