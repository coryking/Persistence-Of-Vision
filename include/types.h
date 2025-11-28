#ifndef TYPES_H
#define TYPES_H

#include <cstdint>

// Timestamp in microseconds (from esp_timer_get_time())
typedef uint64_t timestamp_t;

// Interval in microseconds
typedef uint64_t interval_t;

// Angle in units where 3600 = 360 degrees (0.1 degree precision)
typedef uint16_t angle_t;

// Angle constants
static constexpr angle_t ANGLE_FULL_CIRCLE = 3600;
static constexpr angle_t ANGLE_HALF_CIRCLE = 1800;
static constexpr angle_t ANGLE_QUARTER_CIRCLE = 900;
static constexpr angle_t ANGLE_PER_PATTERN = 180;      // 18 degrees = 1 pattern
static constexpr angle_t INNER_ARM_PHASE = 1200;       // 120 degrees
static constexpr angle_t OUTER_ARM_PHASE = 2400;       // 240 degrees

// Speed constants (microseconds per revolution)
// Lower value = faster rotation
static constexpr interval_t MICROS_PER_REV_MIN = 21428;   // ~2800 RPM (fastest)
static constexpr interval_t MICROS_PER_REV_MAX = 85714;   // ~700 RPM (slowest)

#endif // TYPES_H
