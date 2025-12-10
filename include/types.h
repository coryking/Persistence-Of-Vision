#ifndef TYPES_H
#define TYPES_H

#include <cstdint>


/**
 * Convert degrees to angle units (3600 units = 360 degrees)
 * Works with both integer and floating-point inputs.
 * Examples: ANGLE_UNITS(360) -> 3600, ANGLE_UNITS(360.5) -> 3605
 */
#define ANGLE_UNITS(deg) ((angle_t)((deg) * 10))
#define UNITS_TO_DEGREES(units) ((float)(units) / 10.0f)

// Timestamp in microseconds (from esp_timer_get_time())
typedef uint64_t timestamp_t;

// Interval in microseconds
typedef uint64_t interval_t;

// Time conversion macros
#define SECONDS_TO_MICROS(s) ((s) * 1000000ULL)

// Angle in units where 3600 = 360 degrees (0.1 degree precision)
typedef uint16_t angle_t;

// Angle constants
static constexpr angle_t ANGLE_FULL_CIRCLE = ANGLE_UNITS(360);
static constexpr angle_t ANGLE_HALF_CIRCLE = ANGLE_UNITS(180);
static constexpr angle_t ANGLE_QUARTER_CIRCLE = ANGLE_UNITS(90);
static constexpr angle_t ANGLE_PER_PATTERN = ANGLE_UNITS(18);      // 18 degrees = 1 pattern

// Phase offsets for each arm (indexed by physical position)
// arm[0] = outer (+240° from hall), arm[1] = middle (0°, hall), arm[2] = inside (+120° from hall)
static constexpr angle_t OUTER_ARM_PHASE = ANGLE_UNITS(240);   // 240 degrees - arm[0]
static constexpr angle_t MIDDLE_ARM_PHASE = ANGLE_UNITS(0);    // 0 degrees (hall sensor reference) - arm[1]
static constexpr angle_t INSIDE_ARM_PHASE = ANGLE_UNITS(120);  // 120 degrees - arm[2]

// Phase lookup table (indexed by arm position)
static constexpr angle_t ARM_PHASE[3] = {
    OUTER_ARM_PHASE,   // arm[0]
    MIDDLE_ARM_PHASE,  // arm[1]
    INSIDE_ARM_PHASE   // arm[2]
}

// Speed constants (microseconds per revolution)
// Lower value = faster rotation
static constexpr interval_t MICROS_PER_REV_MIN = 21428;   // ~2800 RPM (fastest)
static constexpr interval_t MICROS_PER_REV_MAX = 85714;   // ~700 RPM (slowest)

#endif // TYPES_H
