#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H

#include <ADXL345_WE.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "types.h"

/**
 * ADXL345 Accelerometer wrapper using ADXL345_WE library
 *
 * CS and SDO pins are set HIGH in software to enable I2C mode at address 0x1D.
 * Configured for full resolution mode at 800Hz sample rate, ±16g range.
 *
 * Uses DATA_READY interrupt for accurate timestamping. ISR captures timestamp
 * and queues it; consumer reads the sample using that timestamp.
 */
class Accelerometer {
public:
    /**
     * Initialize accelerometer and set up DATA_READY interrupt
     * @return true if device initialized successfully, false on failure
     */
    bool begin();

    /**
     * Read current acceleration values (immediate read, no timestamp)
     * @param reading Output xyzFloat struct for X, Y, Z values
     * @return true on success
     */
    bool read(xyzFloat& reading);

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
     * Check if accelerometer was initialized successfully
     */
    bool isReady() const { return m_ready; }

private:
    ADXL345_WE* m_adxl = nullptr;
    bool m_ready = false;
};

// Global accelerometer instance (initialized in main.cpp)
extern Accelerometer accel;

// Queue for DATA_READY timestamps from ISR (created in Accelerometer::begin())
extern QueueHandle_t g_accelTimestampQueue;

#endif // ACCELEROMETER_H
