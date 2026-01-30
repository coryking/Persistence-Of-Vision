#ifndef ROTOR_DIAGNOSTIC_STATS_H
#define ROTOR_DIAGNOSTIC_STATS_H

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/portmacro.h"
#include "types.h"

/**
 * RotorDiagnosticStats - Always-running diagnostic data collector
 *
 * This singleton class runs independently of any effect and collects
 * diagnostic data about the hall sensor and ESP-NOW communication.
 * Data is sent to the motor controller every 500ms for logging/analysis.
 *
 * Usage:
 *   // In setup()
 *   RotorDiagnosticStats::instance().start(500);
 *
 *   // When hall events occur (in hallProcessingTask)
 *   RotorDiagnosticStats::instance().recordHallEvent();
 *
 *   // When outliers are filtered (in RevolutionTimer)
 *   RotorDiagnosticStats::instance().recordOutlier(interval_us);
 *
 *   // After ESP-NOW send attempts
 *   RotorDiagnosticStats::instance().recordEspNowResult(success);
 *
 *   // When effect/brightness changes
 *   RotorDiagnosticStats::instance().setEffectNumber(num);
 *   RotorDiagnosticStats::instance().setBrightness(val);
 */
class RotorDiagnosticStats {
public:
    /**
     * Get singleton instance
     */
    static RotorDiagnosticStats& instance();

    /**
     * Start the diagnostic timer
     * @param intervalMs Interval between stats transmissions (default 500ms)
     */
    void start(uint32_t intervalMs = 500);

    /**
     * Stop the diagnostic timer
     */
    void stop();

    /**
     * Reset all statistics and update created_us timestamp
     * Called when MSG_RESET_ROTOR_STATS is received
     */
    void reset();

    // ========== Recording Methods (thread-safe, called from ISR/tasks) ==========

    /**
     * Record a hall sensor event
     * Called from hallProcessingTask after each valid hall trigger
     */
    void recordHallEvent();

    /**
     * Record outlier rejected for being too fast (< MIN_REASONABLE_INTERVAL)
     * @param interval_us The rejected interval in microseconds
     */
    void recordOutlierTooFast(interval_t interval_us);

    /**
     * Record outlier rejected for being too slow (> MAX_INTERVAL_RATIO * avg)
     * Likely a missed trigger - interval is 2x+ expected
     * @param interval_us The rejected interval in microseconds
     */
    void recordOutlierTooSlow(interval_t interval_us);

    /**
     * Record outlier rejected for ratio below threshold (< MIN_INTERVAL_RATIO * avg)
     * Spurious trigger that passed absolute check but failed ratio check
     * @param interval_us The rejected interval in microseconds
     */
    void recordOutlierRatioLow(interval_t interval_us);

    /**
     * Record ESP-NOW send result
     * Called after each esp_now_send() attempt
     * @param success true if ESP_OK, false otherwise
     */
    void recordEspNowResult(bool success);

    /**
     * Record a render event (for render pipeline stats)
     * @param rendered true if frame was rendered, false if skipped
     * @param notRotating true if not rotating (early exit)
     */
    void recordRenderEvent(bool rendered, bool notRotating);

    // ========== State Setters ==========

    /**
     * Set current effect number (1-based)
     */
    void setEffectNumber(uint8_t effectNum);

    /**
     * Set current brightness level (0-10)
     */
    void setBrightness(uint8_t brightness);

    /**
     * Set current smoothed hall period
     * Called after RevolutionTimer updates the smoothed interval
     */
    void setHallAvgUs(period_t avgUs);

    // ========== Debug ==========

    /**
     * Print current stats to serial (for debugging)
     */
    void print() const;

private:
    // Singleton - private constructor
    RotorDiagnosticStats() = default;

    // Timer callback (static to interface with FreeRTOS)
    static void timerCallback(TimerHandle_t xTimer);

    // Send stats via ESP-NOW
    void sendViaEspNow();

    // Statistics (all atomically updated via spinlock)
    timestamp_t _created_us = 0;        // When stats were last reset
    timestamp_t _lastUpdated_us = 0;    // Most recent update

    // Hall sensor stats
    uint32_t _hallEventsTotal = 0;      // Total hall events since reset
    period_t _hallAvg_us = 0;           // Smoothed hall period

    // Enhanced outlier tracking (separate counters by rejection reason)
    uint32_t _outliersTooFast = 0;      // Rejected: < MIN_REASONABLE_INTERVAL
    uint32_t _outliersTooSlow = 0;      // Rejected: > MAX_INTERVAL_RATIO * avg
    uint32_t _outliersRatioLow = 0;     // Rejected: < MIN_INTERVAL_RATIO * avg
    uint32_t _lastOutlierInterval_us = 0; // Most recent rejected interval
    uint8_t _lastOutlierReason = 0;     // 0=none, 1=too_fast, 2=too_slow, 3=ratio_low

    // ESP-NOW stats
    uint32_t _espnowSendAttempts = 0;   // Total send attempts
    uint32_t _espnowSendFailures = 0;   // Failed sends

    // Render pipeline stats (reset each report for delta values)
    uint16_t _renderCount = 0;          // Successful renders since last report
    uint16_t _skipCount = 0;            // Skipped (behind schedule)
    uint16_t _notRotatingCount = 0;     // Loop exited early (not rotating)

    // Current state
    uint8_t _effectNumber = 0;          // 1-based effect number
    uint8_t _brightness = 5;            // 0-10 brightness level

    // Report sequence number (increments each send)
    uint32_t _reportSequence = 0;

    // FreeRTOS timer handle
    TimerHandle_t _timer = nullptr;

    // Spinlock for thread-safe access (ISR-safe)
    mutable portMUX_TYPE _spinlock = portMUX_INITIALIZER_UNLOCKED;
};

#endif // ROTOR_DIAGNOSTIC_STATS_H
