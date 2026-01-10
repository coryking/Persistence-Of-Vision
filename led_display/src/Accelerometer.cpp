#include "Accelerometer.h"
#include "hardware_config.h"

#include <Arduino.h>
#include <Wire.h>

// Global instance
Accelerometer accel;

// ADXL345 I2C address (SDO pin HIGH = 0x1D, LOW = 0x53)
static constexpr uint8_t ADXL345_ADDR = 0x1D;

bool Accelerometer::begin() {
    Serial.println("[ACCEL] Initializing ADXL345 via library...");

    // Configure CS pin HIGH to enable I2C mode
    pinMode(HardwareConfig::ACCEL_CS_PIN, OUTPUT);
    digitalWrite(HardwareConfig::ACCEL_CS_PIN, HIGH);

    // Configure SDO pin HIGH to select address 0x1D
    pinMode(HardwareConfig::ACCEL_SDO_PIN, OUTPUT);
    digitalWrite(HardwareConfig::ACCEL_SDO_PIN, HIGH);

    // Small delay for pins to settle
    delay(5);

    // Initialize I2C on accelerometer pins
    Wire.begin(HardwareConfig::ACCEL_SDA_PIN, HardwareConfig::ACCEL_SCL_PIN);
    Wire.setClock(400000);  // 400kHz Fast Mode

    // Create library instance
    m_adxl = new ADXL345_WE(ADXL345_ADDR);

    if (!m_adxl->init()) {
        Serial.println("[ACCEL] Library init failed!");
        return false;
    }

    // Configure: 400Hz sample rate, ±4g range, full resolution
    m_adxl->setDataRate(ADXL345_DATA_RATE_400);
    m_adxl->setRange(ADXL345_RANGE_4G);
    m_adxl->setFullRes(true);

    // Read initial values as sanity check
    xyzFloat raw;
    m_adxl->getRawValues(&raw);
    Serial.printf("[ACCEL] Initial reading: X=%.1f Y=%.1f Z=%.1f\n", raw.x, raw.y, raw.z);

    m_ready = true;
    Serial.println("[ACCEL] Ready (library, I2C @ 400kHz)");
    return true;
}

bool Accelerometer::read(xyzFloat& reading) {
    if (!m_ready || !m_adxl) return false;
    return m_adxl->getRawValues(&reading);
}
