#include "Accelerometer.h"
#include "hardware_config.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_timer.h>

// Global instance
Accelerometer accel;

// Queue for DATA_READY timestamps from ISR
QueueHandle_t g_accelTimestampQueue = nullptr;

// Queue size - 80ms buffer at 800Hz (allows telemetry task some slack)
static constexpr size_t ACCEL_QUEUE_SIZE = 64;

// ADXL345 I2C address (SDO pin HIGH = 0x1D, LOW = 0x53)
static constexpr uint8_t ADXL345_ADDR = 0x1D;

// ISR for DATA_READY interrupt - captures timestamp and queues it
void IRAM_ATTR accelDataReadyISR() {
    timestamp_t now = esp_timer_get_time();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Send timestamp to queue (non-blocking, drop if full)
    xQueueSendFromISR(g_accelTimestampQueue, &now, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

bool Accelerometer::begin() {
    Serial.println("[ACCEL] Initializing ADXL345 with DATA_READY interrupt...");

    // Create timestamp queue
    g_accelTimestampQueue = xQueueCreate(ACCEL_QUEUE_SIZE, sizeof(timestamp_t));
    if (!g_accelTimestampQueue) {
        Serial.println("[ACCEL] Failed to create timestamp queue!");
        return false;
    }
    Serial.printf("[ACCEL] Timestamp queue created: %zu slots\n", ACCEL_QUEUE_SIZE);

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

    // Configure: 800Hz sample rate, ±16g range, full resolution
    m_adxl->setDataRate(ADXL345_DATA_RATE_800);
    m_adxl->setRange(ADXL345_RANGE_16G);
    m_adxl->setFullRes(true);

    // Configure DATA_READY interrupt on INT1 pin
    m_adxl->setInterrupt(ADXL345_DATA_READY, 1);  // Route to INT1

    // Configure INT1 pin as input and attach interrupt
    pinMode(HardwareConfig::ACCEL_INT1_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(HardwareConfig::ACCEL_INT1_PIN),
                    accelDataReadyISR, RISING);

    // Read initial values as sanity check (also clears any pending interrupt)
    xyzFloat raw;
    m_adxl->getRawValues(&raw);
    Serial.printf("[ACCEL] Initial reading: X=%.1f Y=%.1f Z=%.1f\n", raw.x, raw.y, raw.z);

    m_ready = true;
    Serial.println("[ACCEL] Ready (800Hz, ±16g, DATA_READY interrupt on INT1)");
    return true;
}

bool Accelerometer::read(xyzFloat& reading) {
    if (!m_ready || !m_adxl) return false;
    return m_adxl->getRawValues(&reading);
}

bool Accelerometer::sampleReady() {
    if (!g_accelTimestampQueue) return false;
    return uxQueueMessagesWaiting(g_accelTimestampQueue) > 0;
}

bool Accelerometer::getNextTimestamp(timestamp_t& timestamp) {
    if (!g_accelTimestampQueue) return false;
    return xQueueReceive(g_accelTimestampQueue, &timestamp, 0) == pdTRUE;
}
