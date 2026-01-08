#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H

#include <cstdint>

/**
 * ADXL345 Accelerometer wrapper for rotor balancing calibration
 *
 * Uses I2C (Wire) with direct register access - no external library needed.
 * CS and SDO pins are set HIGH in software to enable I2C mode at address 0x1D.
 * Configured for full resolution mode (256 LSB/g) at 400Hz sample rate.
 */
class Accelerometer {
public:
    // Raw accelerometer reading (256 LSB/g in full resolution mode)
    struct Reading {
        int16_t x;
        int16_t y;
        int16_t z;
    };

    /**
     * Initialize accelerometer on SPI bus
     * @return true if device ID verified (0xE5), false on failure
     */
    bool begin();

    /**
     * Read current acceleration values
     * @param reading Output struct for X, Y, Z values
     * @return true on success
     */
    bool read(Reading& reading);

    /**
     * Check if accelerometer was initialized successfully
     */
    bool isReady() const { return m_ready; }

private:
    bool m_ready = false;
};

// Global accelerometer instance (initialized in main.cpp)
extern Accelerometer accel;

#endif // ACCELEROMETER_H
