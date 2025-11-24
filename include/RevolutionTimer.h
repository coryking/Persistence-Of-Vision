#ifndef REVOLUTION_TIMER_H
#define REVOLUTION_TIMER_H

#include "types.h"
#include "RollingAverage.h"

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
    {
    }

    /**
     * Add a new timestamp from hall sensor trigger
     * Call this from ISR or immediately after
     * @param timestamp Current time in microseconds (from esp_timer_get_time())
     */
    void addTimestamp(timestamp_t timestamp) {
        if (lastTimestamp != 0) {
            // Calculate interval since last trigger
            interval_t interval = timestamp - lastTimestamp;

            // Check for timeout (rotation stopped)
            if (interval > rotationTimeoutUs) {
                // Rotation stopped - reset state
                isRotating = false;
                revolutionCount = 0;
                rollingAvg.reset();
                smoothedInterval = 0;
            } else {
                // Valid rotation detected
                isRotating = true;
                lastInterval = interval;

                // Add to rolling average
                rollingAvg.add(static_cast<double>(interval));
                smoothedInterval = static_cast<interval_t>(rollingAvg.average());

                // Increment revolution count
                revolutionCount++;
            }
        } else {
            // First timestamp - no interval to calculate yet
            isRotating = true;
        }

        lastTimestamp = timestamp;
    }

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
    rpm_t getRPM() const {
        if (smoothedInterval == 0) {
            return 0.0f;
        }
        // Convert microseconds per revolution to RPM
        // RPM = (60 seconds * 1,000,000 microseconds/second) / microseconds per revolution
        return 60000000.0f / static_cast<float>(smoothedInterval);
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
};

#endif // REVOLUTION_TIMER_H
