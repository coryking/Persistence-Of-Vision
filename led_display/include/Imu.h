#ifndef IMU_H
#define IMU_H

#include <MPU9250_WE.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "types.h"

/**
 * MPU-9250 IMU wrapper using MPU9250_WE library
 *
 * Uses SPI at 20MHz on HSPI bus (separate from LED's FSPI bus).
 * Configured for 8kHz sample rate with DLPF disabled:
 *   - Accel: ±16g range, 8kHz output
 *   - Gyro: ±2000°/s range, 8kHz output
 *
 * Uses DATA_READY interrupt to signal sample ready. Consumer waits for signal,
 * then reads data and captures timestamp at read time (not ISR time).
 */
class Imu {
public:
    /**
     * Initialize IMU and set up DATA_READY interrupt
     * @return true if device initialized successfully, false on failure
     */
    bool begin();

    /**
     * Read current acceleration and gyroscope values via library (slower, has calibration)
     * @param accel Output xyzFloat struct for accel X, Y, Z raw values
     * @param gyro Output xyzFloat struct for gyro X, Y, Z raw values
     * @return true on success
     */
    bool read(xyzFloat& accel, xyzFloat& gyro);

    /**
     * Fast burst read of raw sensor values via direct SPI (no calibration, ~37µs)
     * @param ax,ay,az Output raw accelerometer values (int16)
     * @param gx,gy,gz Output raw gyroscope values (int16)
     * @return true on success
     */
    bool readRaw(int16_t& ax, int16_t& ay, int16_t& az,
                 int16_t& gx, int16_t& gy, int16_t& gz);

    /**
     * Check if a new sample is ready (signal waiting in queue)
     * @return true if sample available
     */
    bool sampleReady();

    /**
     * Wait for next sample signal from ISR (blocking with timeout)
     * @param timeout FreeRTOS ticks to wait (pdMS_TO_TICKS(10) for 10ms)
     * @return true if signal received, false on timeout
     */
    bool waitForSample(TickType_t timeout);

    /**
     * Check if IMU was initialized successfully
     */
    bool isReady() const { return m_ready; }

    /**
     * Enable IMU: wake sensor from sleep, attach DATA_READY ISR
     * Call this before sampling data. No-op if already enabled.
     */
    void enable();

    /**
     * Disable IMU: detach ISR, disable interrupt, put sensor to sleep
     * Stops 8kHz interrupt overhead when not using the sensor.
     */
    void disable();

    /**
     * Check if IMU interrupts are enabled
     */
    bool isEnabled() const { return m_imuEnabled; }

private:
    MPU9250_WE* m_imu = nullptr;
    bool m_ready = false;
    bool m_imuEnabled = false;  // True when ISR is attached and sensor is awake

    // Calibration offsets from autoOffsets() (applied in readRaw for fast calibrated reads)
    // Offsets are captured at 2G/250dps, must be divided by range factor when applying
    // Range factor = 1 << enum_value (matching library's internal calculation)
    xyzFloat m_accOffset{0.f, 0.f, 0.f};
    xyzFloat m_gyrOffset{0.f, 0.f, 0.f};
    uint8_t m_accRangeFactor = 1;
    uint8_t m_gyrRangeFactor = 1;
};

// Global IMU instance (initialized in main.cpp)
extern Imu imu;

// Queue for DATA_READY timestamps from ISR (created in Imu::begin())
extern QueueHandle_t g_imuTimestampQueue;

#endif // IMU_H
