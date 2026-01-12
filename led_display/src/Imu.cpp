#include "Imu.h"
#include "hardware_config.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_timer.h>
#include <driver/gpio.h>

// Global instance
Imu imu;

// Queue for DATA_READY timestamps from ISR
QueueHandle_t g_imuTimestampQueue = nullptr;

// Queue size - 100ms buffer at 1kHz (allows telemetry task some slack)
static constexpr size_t IMU_QUEUE_SIZE = 100;

// MPU-9250 I2C address (ADO pin LOW = 0x68, HIGH = 0x69)
static constexpr uint8_t MPU9250_ADDR = 0x68;

// Forward declaration for ISR wrapper
static void imuDataReadyISR();

// Wrapper for ESP-IDF gpio_isr_handler_add (requires void* arg signature)
static void IRAM_ATTR imuIsrWrapper(void* arg) {
    (void)arg;
    imuDataReadyISR();
}

// ISR for DATA_READY interrupt - captures timestamp and queues it
static void IRAM_ATTR imuDataReadyISR() {
    timestamp_t now = esp_timer_get_time();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Send timestamp to queue (non-blocking, drop if full)
    xQueueSendFromISR(g_imuTimestampQueue, &now, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

bool Imu::begin() {
    Serial.println("[IMU] Initializing MPU-9250 with DATA_READY interrupt...");

    // Create timestamp queue
    g_imuTimestampQueue = xQueueCreate(IMU_QUEUE_SIZE, sizeof(timestamp_t));
    if (!g_imuTimestampQueue) {
        Serial.println("[IMU] Failed to create timestamp queue!");
        return false;
    }
    Serial.printf("[IMU] Timestamp queue created: %zu slots\n", IMU_QUEUE_SIZE);

    // Configure NCS pin HIGH to enable I2C mode (vs SPI)
    pinMode(HardwareConfig::IMU_NCS_PIN, OUTPUT);
    digitalWrite(HardwareConfig::IMU_NCS_PIN, HIGH);

    // Configure ADO pin LOW to select address 0x68
    pinMode(HardwareConfig::IMU_ADO_PIN, OUTPUT);
    digitalWrite(HardwareConfig::IMU_ADO_PIN, LOW);

    // Small delay for pins to settle
    delay(10);

    // Initialize I2C on IMU pins
    Wire.begin(HardwareConfig::IMU_SDA_PIN, HardwareConfig::IMU_SCL_PIN);
    Wire.setClock(400000);  // 400kHz Fast Mode

    // Create library instance
    m_imu = new MPU9250_WE(MPU9250_ADDR);

    if (!m_imu->init()) {
        Serial.println("[IMU] Library init failed!");
        return false;
    }

    // Auto-offsets calibration (keeps device still briefly)
    Serial.println("[IMU] Calibrating offsets (keep still)...");
    delay(1000);
    m_imu->autoOffsets();

    // Configure accelerometer: ±16g range, DLPF mode 1 (184Hz BW, 1kHz rate)
    m_imu->setAccRange(MPU9250_ACC_RANGE_16G);
    m_imu->enableAccDLPF(true);
    m_imu->setAccDLPF(MPU9250_DLPF_1);

    // Configure gyroscope: ±2000°/s range, DLPF mode 1 (184Hz BW, 1kHz rate)
    m_imu->setGyrRange(MPU9250_GYRO_RANGE_2000);
    m_imu->enableGyrDLPF();
    m_imu->setGyrDLPF(MPU9250_DLPF_1);

    // Sample rate = 1kHz / (1 + divider) = 1kHz with divider = 0
    m_imu->setSampleRateDivider(0);

    // Enable DATA_READY interrupt
    m_imu->enableInterrupt(MPU9250_DATA_READY);

    // Configure INT pin as input with interrupt on rising edge
    // Use ESP-IDF API directly to avoid "GPIO isr service already installed" warning
    // (the hall sensor driver already installed the service)
    gpio_num_t intPin = static_cast<gpio_num_t>(HardwareConfig::IMU_INT_PIN);
    gpio_set_direction(intPin, GPIO_MODE_INPUT);
    gpio_set_intr_type(intPin, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(intPin, imuIsrWrapper, nullptr);

    // Read initial values as sanity check (also clears any pending interrupt)
    xyzFloat accelG = m_imu->getGValues();
    xyzFloat gyroDps = m_imu->getGyrValues();
    Serial.printf("[IMU] Initial accel: X=%.2f Y=%.2f Z=%.2f g\n",
                  accelG.x, accelG.y, accelG.z);
    Serial.printf("[IMU] Initial gyro: X=%.1f Y=%.1f Z=%.1f dps\n",
                  gyroDps.x, gyroDps.y, gyroDps.z);

    m_ready = true;
    Serial.println("[IMU] Ready (1kHz, ±16g accel, ±2000°/s gyro, DATA_READY on INT)");
    return true;
}

bool Imu::read(xyzFloat& accel, xyzFloat& gyro) {
    if (!m_ready || !m_imu) return false;

    // Read corrected raw values (int16 as float, WITH calibration offsets applied)
    accel = m_imu->getCorrectedAccRawValues();
    gyro = m_imu->getCorrectedGyrRawValues();

    return true;
}

bool Imu::sampleReady() {
    if (!g_imuTimestampQueue) return false;
    return uxQueueMessagesWaiting(g_imuTimestampQueue) > 0;
}

bool Imu::getNextTimestamp(timestamp_t& timestamp) {
    if (!g_imuTimestampQueue) return false;
    return xQueueReceive(g_imuTimestampQueue, &timestamp, 0) == pdTRUE;
}
