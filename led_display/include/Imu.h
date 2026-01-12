#ifndef IMU_H
#define IMU_H

#include <MPU9250_WE.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "types.h"

/**
 * MPU-9250 IMU wrapper using MPU9250_WE library
 *
 * ADO pin controlled in software: LOW = I2C address 0x68
 * NCS pin set HIGH to enable I2C mode.
 * Configured for 1kHz sample rate with DLPF mode 1:
 *   - Accel: ±16g range, 184Hz bandwidth
 *   - Gyro: ±2000°/s range, 184Hz bandwidth
 *
 * Uses DATA_READY interrupt for accurate timestamping. ISR captures timestamp
 * and queues it; consumer reads both accel and gyro using that timestamp.
 */
class Imu {
public:
    /**
     * Initialize IMU and set up DATA_READY interrupt
     * @return true if device initialized successfully, false on failure
     */
    bool begin();

    /**
     * Read current acceleration and gyroscope values (immediate read, no timestamp)
     * @param accel Output xyzFloat struct for accel X, Y, Z raw values
     * @param gyro Output xyzFloat struct for gyro X, Y, Z raw values
     * @return true on success
     */
    bool read(xyzFloat& accel, xyzFloat& gyro);

    /**
     * Check if a new sample is ready (timestamp waiting in queue)
     * @return true if sample available
     */
    bool sampleReady();

    /**
     * Get the timestamp of the next pending sample (non-blocking)
     * @param timestamp Output timestamp from ISR
     * @return true if timestamp was available, false if queue empty
     */
    bool getNextTimestamp(timestamp_t& timestamp);

    /**
     * Check if IMU was initialized successfully
     */
    bool isReady() const { return m_ready; }

private:
    MPU9250_WE* m_imu = nullptr;
    bool m_ready = false;
};

// Global IMU instance (initialized in main.cpp)
extern Imu imu;

// Queue for DATA_READY timestamps from ISR (created in Imu::begin())
extern QueueHandle_t g_imuTimestampQueue;

#endif // IMU_H
