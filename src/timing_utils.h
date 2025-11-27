#pragma once
#include <esp_timer.h>
#include <Arduino.h>

/**
 * Simple timing measurement helper
 * Usage:
 *   int64_t start = timingStart();
 *   doWork();
 *   int64_t elapsed = timingEnd(start);
 */
inline int64_t timingStart() {
    return esp_timer_get_time();
}

inline int64_t timingEnd(int64_t start) {
    return esp_timer_get_time() - start;
}
