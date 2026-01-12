#ifndef SHARED_TYPES_H
#define SHARED_TYPES_H

#include <cstdint>

// =============================================================================
// Common type definitions for POV Display System
// Used by both led_display and motor_controller projects
// =============================================================================

// Microsecond timestamp from esp_timer_get_time()
// 64-bit to avoid overflow (32-bit overflows in ~71 minutes)
typedef uint64_t timestamp_t;

// Microsecond duration/interval
typedef uint64_t interval_t;

// Hall sensor period in microseconds
// 32-bit is sufficient: max useful period ~1 second at 60 RPM = 1,000,000 µs
typedef uint32_t period_t;

// Accelerometer axis value (int16_t - MPU-9250 outputs 16-bit signed integers)
// Using int16_t for raw values; conversion to g done in Python analysis.
typedef int16_t accel_raw_t;

// Gyroscope axis value (int16_t - MPU-9250 outputs 16-bit signed integers)
// Using int16_t for raw values; conversion to °/s done in Python analysis.
typedef int16_t gyro_raw_t;

// Sample sequence number for drop detection (wraps at 65535)
typedef uint16_t sequence_t;

// Revolution counter (wraps at 65535 = ~23 min at 2800 RPM)
typedef uint16_t rotation_t;

// Time conversion macro
#define SECONDS_TO_MICROS(s) ((s) * 1000000ULL)

#endif // SHARED_TYPES_H
