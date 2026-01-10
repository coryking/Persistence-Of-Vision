#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H

#include <ADXL345_WE.h>

/**
 * ADXL345 Accelerometer wrapper using ADXL345_WE library
 *
 * CS and SDO pins are set HIGH in software to enable I2C mode at address 0x1D.
 * Configured for full resolution mode at 400Hz sample rate, ±4g range.
 */
class Accelerometer {
public:
    /**
     * Initialize accelerometer
     * @return true if device initialized successfully, false on failure
     */
    bool begin();

    /**
     * Read current acceleration values
     * @param reading Output xyzFloat struct for X, Y, Z values
     * @return true on success
     */
    bool read(xyzFloat& reading);

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

#endif // ACCELEROMETER_H
