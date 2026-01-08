#include "Accelerometer.h"
#include "hardware_config.h"

#include <Arduino.h>
#include <Wire.h>

// Global instance
Accelerometer accel;

// ADXL345 I2C address (SDO pin HIGH = 0x1D, LOW = 0x53)
static constexpr uint8_t ADXL345_ADDR = 0x1D;

// ADXL345 registers
static constexpr uint8_t REG_DEVID = 0x00;
static constexpr uint8_t REG_BW_RATE = 0x2C;
static constexpr uint8_t REG_POWER_CTL = 0x2D;
static constexpr uint8_t REG_DATA_FORMAT = 0x31;
static constexpr uint8_t REG_DATAX0 = 0x32;  // 6 bytes: X0,X1,Y0,Y1,Z0,Z1

// Register values
static constexpr uint8_t BW_RATE_400HZ = 0x0C;
static constexpr uint8_t DATA_FORMAT_FULL_RES_4G = 0x09;  // Full resolution, ±4g
static constexpr uint8_t POWER_CTL_MEASURE = 0x08;

static void writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ADXL345_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

static uint8_t readReg(uint8_t reg) {
    Wire.beginTransmission(ADXL345_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);  // Repeated start
    Wire.requestFrom(ADXL345_ADDR, (uint8_t)1);
    return Wire.read();
}

bool Accelerometer::begin() {
    Serial.println("[ACCEL] Initializing ADXL345 via I2C...");

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

    // Verify device ID
    uint8_t devId = readReg(REG_DEVID);
    if (devId != 0xE5) {
        Serial.printf("[ACCEL] Wrong device ID: 0x%02X (expected 0xE5)\n", devId);
        return false;
    }

    // Configure: 400Hz sample rate
    writeReg(REG_BW_RATE, BW_RATE_400HZ);

    // Configure: full resolution, ±4g range
    writeReg(REG_DATA_FORMAT, DATA_FORMAT_FULL_RES_4G);

    // Enable measurement mode
    writeReg(REG_POWER_CTL, POWER_CTL_MEASURE);

    // Read initial values as sanity check
    Reading r;
    read(r);
    Serial.printf("[ACCEL] Initial reading: X=%d Y=%d Z=%d\n", r.x, r.y, r.z);

    m_ready = true;
    Serial.println("[ACCEL] Ready (I2C @ 400kHz)");
    return true;
}

bool Accelerometer::read(Reading& reading) {
    if (!m_ready) return false;

    // Read 6 bytes starting at DATAX0
    Wire.beginTransmission(ADXL345_ADDR);
    Wire.write(REG_DATAX0);
    Wire.endTransmission(false);
    Wire.requestFrom(ADXL345_ADDR, (uint8_t)6);

    // Data is little-endian: low byte first
    reading.x = Wire.read() | (Wire.read() << 8);
    reading.y = Wire.read() | (Wire.read() << 8);
    reading.z = Wire.read() | (Wire.read() << 8);

    return true;
}
