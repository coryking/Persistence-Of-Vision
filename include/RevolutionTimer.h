#ifndef REVOLUTION_TIMER_H
#define REVOLUTION_TIMER_H

#include "types.h"
#include "RollingAverage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_timer.h"

/**
 * Snapshot of timing values for atomic access
 *
 * Use getTimingSnapshot() to read all values consistently,
 * avoiding race conditions between main loop and hall processing task.
 */
struct TimingSnapshot {
    timestamp_t lastTimestamp;      // When hall sensor last triggered
    interval_t microsecondsPerRev;  // Smoothed revolution period (for display/resolution calc)
    interval_t lastActualInterval;  // Most recent actual revolution time (for angle calc)
    bool isRotating;                // Currently rotating?
    bool warmupComplete;            // Warmup period done?
    float angularResolution;        // Current degrees per render slot
};

/**
 * Valid angular resolutions that evenly divide 360°
 *
 * At low RPM we can use finer resolution (more time per degree).
 * At high RPM we need coarser resolution to keep up.
 * All values divide 360 evenly for clean 0° alignment.
 */
static constexpr float VALID_RESOLUTIONS[] = {
    0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f,  // Fine (0.5° increments, but 3.5 doesn't divide 360)
    4.0f, 4.5f, 5.0f, 6.0f,               // Medium
    8.0f, 9.0f, 10.0f, 12.0f, 15.0f, 18.0f, 20.0f  // Coarse
};
static constexpr size_t NUM_VALID_RESOLUTIONS = sizeof(VALID_RESOLUTIONS) / sizeof(VALID_RESOLUTIONS[0]);
static constexpr float DEFAULT_RESOLUTION = 3.0f;
static constexpr float RENDER_TIME_SAFETY_MARGIN = 1.5f;

// Outlier rejection: reject intervals that deviate too much from rolling average
// 0.5 = 50% deviation allowed (interval must be between 50% and 150% of average)
static constexpr float MAX_INTERVAL_DEVIATION = 0.5f;

/**
 * RevolutionTimer - High-precision revolution timing for POV displays
 *
 * Tracks hall sensor triggers to calculate smoothed revolution timing.
 * Uses rolling average to handle variations in rotation speed.
 */
class RevolutionTimer {
public:
    /**
     * Constructor
     * @param warmupCount Number of revolutions before timing is considered stable
     * @param avgSize Size of rolling average window
     * @param timeoutUs Timeout in microseconds to detect stopped rotation
     */
    RevolutionTimer(size_t warmupCount = 20, size_t avgSize = 20, interval_t timeoutUs = 2000000)
        : warmupRevolutions(warmupCount)
        , rollingAvgSize(avgSize)
        , rotationTimeoutUs(timeoutUs)
        , lastTimestamp(0)
        , revolutionCount(0)
        , lastInterval(0)
        , smoothedInterval(0)
        , isRotating(false)
        , _renderStartTime(0)
        , _currentAngularResolution(DEFAULT_RESOLUTION)
    {
    }

    // ========== Render Timing ==========

    /**
     * Call at the START of each render cycle
     * Captures timestamp for render duration measurement
     */
    void startRender() {
        _renderStartTime = esp_timer_get_time();
    }

    /**
     * Call at the END of each render cycle
     * Records render duration for adaptive resolution calculation
     */
    void endRender() {
        if (_renderStartTime > 0) {
            uint32_t renderTime = static_cast<uint32_t>(esp_timer_get_time() - _renderStartTime);
            _renderTimeAvg.add(renderTime);
            _renderStartTime = 0;
        }
    }

    /**
     * Get current angular resolution (degrees per render slot)
     *
     * This value only changes at revolution boundaries to ensure
     * consistent slot alignment throughout each revolution.
     */
    float getAngularResolution() const {
        return _currentAngularResolution;
    }

    /**
     * Get average render time in microseconds
     */
    uint32_t getAverageRenderTime() const {
        return static_cast<uint32_t>(_renderTimeAvg.average());
    }

    /**
     * Add a new timestamp from hall sensor trigger
     * Call this from ISR or immediately after
     * @param timestamp Current time in microseconds (from esp_timer_get_time())
     */
    void addTimestamp(timestamp_t timestamp) {
        // Calculate new values outside critical section
        interval_t interval = 0;
        bool hasInterval = (lastTimestamp != 0);

        if (hasInterval) {
            interval = timestamp - lastTimestamp;

            // OUTLIER REJECTION: Once warmed up, reject intervals that deviate
            // too much from the rolling average. This filters noise/bounce.
            // During warmup, accept everything (speed changes rapidly).
            if (smoothedInterval > 0 && revolutionCount >= warmupRevolutions) {
                interval_t lowerBound = static_cast<interval_t>(
                    smoothedInterval * (1.0f - MAX_INTERVAL_DEVIATION));
                interval_t upperBound = static_cast<interval_t>(
                    smoothedInterval * (1.0f + MAX_INTERVAL_DEVIATION));

                if (interval < lowerBound || interval > upperBound) {
                    // Reject this sample - don't update lastInterval or rolling average
                    // But DO update lastTimestamp so next interval is calculated correctly
                    portENTER_CRITICAL(&_spinlock);
                    lastTimestamp = timestamp;
                    portEXIT_CRITICAL(&_spinlock);
                    return;
                }
            }
        }

        // Update shared state atomically
        portENTER_CRITICAL(&_spinlock);

        if (hasInterval) {
            // Check for timeout (rotation stopped)
            if (interval > rotationTimeoutUs) {
                // Rotation stopped - reset state
                isRotating = false;
                revolutionCount = 0;
                smoothedInterval = 0;
                // Note: rollingAvg.reset() done outside critical section below
            } else {
                // Valid rotation detected
                isRotating = true;
                lastInterval = interval;
                revolutionCount++;
            }
        } else {
            // First timestamp - no interval to calculate yet
            isRotating = true;
        }

        lastTimestamp = timestamp;

        // Capture state needed for rolling average update
        bool needsReset = hasInterval && (interval > rotationTimeoutUs);
        bool needsAdd = hasInterval && (interval <= rotationTimeoutUs);
        interval_t intervalForAvg = interval;

        portEXIT_CRITICAL(&_spinlock);

        // Update rolling average outside critical section (it's only used by this task)
        if (needsReset) {
            rollingAvg.reset();
            _renderTimeAvg.reset();
            _currentAngularResolution = DEFAULT_RESOLUTION;
        } else if (needsAdd) {
            rollingAvg.add(static_cast<double>(intervalForAvg));
            // Update smoothed interval (brief critical section)
            portENTER_CRITICAL(&_spinlock);
            smoothedInterval = static_cast<interval_t>(rollingAvg.average());
            portEXIT_CRITICAL(&_spinlock);

            // Recalculate angular resolution once per revolution
            // Only update if we have render time data
            if (_renderTimeAvg.count() > 0) {
                _currentAngularResolution = _calculateOptimalResolution();
            }
        }
    }

private:
    /**
     * Calculate optimal angular resolution based on RPM and render performance
     *
     * Returns the smallest valid resolution that we can sustain given:
     * - Current rotation speed (microseconds per degree)
     * - Measured render time (with safety margin)
     */
    float _calculateOptimalResolution() const {
        if (smoothedInterval == 0) {
            return DEFAULT_RESOLUTION;
        }

        // How much time do we have per degree?
        float microsecondsPerDegree = static_cast<float>(smoothedInterval) / 360.0f;

        // How much time does a render take (with safety margin)?
        float renderTimeWithMargin = static_cast<float>(_renderTimeAvg.average()) * RENDER_TIME_SAFETY_MARGIN;

        // Minimum slot size = render time / time per degree
        float minResolution = renderTimeWithMargin / microsecondsPerDegree;

        // Find smallest valid resolution >= minResolution
        for (size_t i = 0; i < NUM_VALID_RESOLUTIONS; i++) {
            if (VALID_RESOLUTIONS[i] >= minResolution) {
                return VALID_RESOLUTIONS[i];
            }
        }

        // Fall back to largest valid resolution
        return VALID_RESOLUTIONS[NUM_VALID_RESOLUTIONS - 1];
    }

public:

    /**
     * Get the smoothed microseconds per revolution
     * @return Smoothed interval in microseconds, or 0 if not rotating
     */
    interval_t getMicrosecondsPerRevolution() const {
        return smoothedInterval;
    }

    /**
     * Get the last raw interval (unsmoothed)
     * @return Last interval in microseconds
     */
    interval_t getLastInterval() const {
        return lastInterval;
    }

    /**
     * Get the timestamp of the last hall sensor trigger
     * @return Timestamp in microseconds
     */
    timestamp_t getLastTimestamp() const {
        return lastTimestamp;
    }

    /**
     * Get atomic snapshot of all timing values
     *
     * Use this instead of calling individual getters to avoid race conditions
     * between the main loop and hall processing task. The critical section
     * ensures all values are consistent with each other.
     *
     * @return TimingSnapshot with consistent lastTimestamp, microsecondsPerRev, etc.
     */
    TimingSnapshot getTimingSnapshot() {
        TimingSnapshot snap;
        portENTER_CRITICAL(&_spinlock);
        snap.lastTimestamp = lastTimestamp;
        snap.microsecondsPerRev = smoothedInterval;
        snap.lastActualInterval = lastInterval;  // Use this for angle calculation!
        snap.isRotating = isRotating;
        snap.warmupComplete = (revolutionCount >= warmupRevolutions) && (revolutionCount >= 20);
        snap.angularResolution = _currentAngularResolution;
        portEXIT_CRITICAL(&_spinlock);
        return snap;
    }

    /**
     * Check if warm-up period is complete
     * @return true if enough revolutions have occurred for stable timing
     */
    bool isWarmupComplete() const {
        return revolutionCount >= warmupRevolutions && rollingAvg.isFull();
    }

    /**
     * Check if device is currently rotating
     * @return true if rotation detected, false if stopped or timed out
     */
    bool isCurrentlyRotating() const {
        return isRotating;
    }

    /**
     * Get the current revolution count
     * @return Number of revolutions since startup or last reset
     */
    size_t getRevolutionCount() const {
        return revolutionCount;
    }

    /**
     * Get current RPM
     * @return Revolutions per minute, or 0 if not rotating
     */
    uint32_t getRPM() const {
        if (smoothedInterval == 0) {
            return 0;
        }
        // Convert microseconds per revolution to RPM
        // RPM = (60 seconds * 1,000,000 microseconds/second) / microseconds per revolution
        return 60000000UL / smoothedInterval;
    }

    /**
     * Reset all timing state
     */
    void reset() {
        lastTimestamp = 0;
        revolutionCount = 0;
        lastInterval = 0;
        smoothedInterval = 0;
        isRotating = false;
        rollingAvg.reset();
    }

private:
    size_t warmupRevolutions;           // Number of revolutions for warm-up
    size_t rollingAvgSize;              // Size of rolling average window
    interval_t rotationTimeoutUs;       // Timeout to detect stopped rotation

    timestamp_t lastTimestamp;          // Last hall sensor timestamp
    size_t revolutionCount;             // Total revolutions since start/reset
    interval_t lastInterval;            // Last interval (unsmoothed)
    interval_t smoothedInterval;        // Smoothed interval from rolling average
    bool isRotating;                    // Current rotation state

    RollingAverage<double, 20> rollingAvg;  // Rolling average for smoothing

    // Render timing for adaptive angular resolution
    timestamp_t _renderStartTime;                    // When current render started
    RollingAverage<uint32_t, 16> _renderTimeAvg;     // Rolling average of render times
    float _currentAngularResolution;                 // Current degrees per slot (updated once per rev)

    // Spinlock for atomic access between main loop and hall processing task
    mutable portMUX_TYPE _spinlock = portMUX_INITIALIZER_UNLOCKED;
};

#endif // REVOLUTION_TIMER_H
