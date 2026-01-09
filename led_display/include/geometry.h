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
