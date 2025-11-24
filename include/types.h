#ifndef TYPES_H
#define TYPES_H

#include <cstdint>

// Timestamp in microseconds (from esp_timer_get_time())
typedef uint64_t timestamp_t;

// Interval in microseconds
typedef uint64_t interval_t;

// RPM as floating point
typedef float rpm_t;

#endif // TYPES_H
