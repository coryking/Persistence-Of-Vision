#ifndef ROLLING_AVERAGE_H
#define ROLLING_AVERAGE_H

#include <cstddef>

/**
 * RollingAverage - Efficient O(1) rolling average calculator
 *
 * Uses a circular buffer to maintain a running average without
 * needing to iterate through all samples on each update.
 *
 * Template parameters:
 *   T - Numeric type (int, float, double, etc.)
 *   N - Number of samples in the rolling window
 */
template<typename T, size_t N>
class RollingAverage {
public:
    RollingAverage() : sampleIndex(0), total(0), sampleCount(0) {
        // Initialize all samples to zero
        for (size_t i = 0; i < N; i++) {
            samples[i] = 0;
        }
    }

    /**
     * Add a new sample to the rolling average
     * @param value The new sample value
     * @return Reference to this instance for method chaining
     */
    RollingAverage& add(T value) {
        // Subtract the oldest sample from total
        total -= samples[sampleIndex];

        // Store new sample
        samples[sampleIndex] = value;

        // Add new sample to total
        total += value;

        // Move to next position (circular buffer)
        sampleIndex = (sampleIndex + 1) % N;

        // Track how many samples we've added (up to N)
        if (sampleCount < N) {
            sampleCount++;
        }

        return *this;
    }

    /**
     * Get the current rolling average
     * @return The average of all samples in the window
     */
    T average() const {
        if (sampleCount == 0) {
            return 0;
        }
        return total / sampleCount;
    }

    /**
     * Check if the rolling average has been fully populated
     * @return true if N samples have been added
     */
    bool isFull() const {
        return sampleCount >= N;
    }

    /**
     * Get the number of samples currently in the window
     * @return Number of samples (0 to N)
     */
    size_t count() const {
        return sampleCount;
    }

    /**
     * Get average of the n most recent samples
     * Useful for adaptive smoothing - fewer samples = faster response
     *
     * @param n Number of recent samples to average (clamped to available)
     * @return Average of the n most recent samples
     */
    T averageRecent(size_t n) const {
        if (sampleCount == 0 || n == 0) return 0;
        if (n > sampleCount) n = sampleCount;

        T sum = 0;
        // Start n samples back from current position
        size_t idx = (sampleIndex + N - n) % N;
        for (size_t i = 0; i < n; i++) {
            sum += samples[(idx + i) % N];
        }
        return sum / n;
    }

    /**
     * Reset the rolling average to initial state
     */
    void reset() {
        sampleIndex = 0;
        total = 0;
        sampleCount = 0;
        for (size_t i = 0; i < N; i++) {
            samples[i] = 0;
        }
    }

private:
    T samples[N];           // Circular buffer of samples
    size_t sampleIndex;     // Current position in circular buffer
    T total;                // Running sum of all samples
    size_t sampleCount;     // Number of samples added (up to N)
};

#endif // ROLLING_AVERAGE_H
