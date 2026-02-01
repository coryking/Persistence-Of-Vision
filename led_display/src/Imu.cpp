#include "Imu.h"
#include "hardware_config.h"

#include <SPI.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <driver/gpio.h>

static const char* TAG = "IMU";

// Global instance
Imu imu;

// Queue for DATA_READY signals from ISR (no timestamp - we timestamp at read time)
QueueHandle_t g_imuTimestampQueue = nullptr;

// Queue size - 100ms buffer at 8kHz
static constexpr size_t IMU_QUEUE_SIZE = 800;

// SPI configuration
static constexpr uint32_t IMU_SPI_CLOCK = 20000000;  // 20MHz for fast sensor reads

// Separate SPI bus for IMU (HSPI/SPI3) - LEDs use FSPI/SPI2
static SPIClass SPI_IMU(HSPI);

// Forward declaration for ISR wrapper
static void imuDataReadyISR();

// Wrapper for ESP-IDF gpio_isr_handler_add (requires void* arg signature)
static void IRAM_ATTR imuIsrWrapper(void* arg) {
    (void)arg;
    imuDataReadyISR();
}

// ISR for DATA_READY interrupt - signals sample ready (no timestamp)
// Timestamp is captured at read time for accuracy
static void IRAM_ATTR imuDataReadyISR() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Send signal to queue (value doesn't matter, just presence)
    uint8_t signal = 1;
    xQueueSendFromISR(g_imuTimestampQueue, &signal, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

bool Imu::begin() {
    ESP_LOGI(TAG, "Initializing MPU-9250 via SPI at 20MHz...");

    // Create signal queue (just signals sample ready, no timestamp)
    g_imuTimestampQueue = xQueueCreate(IMU_QUEUE_SIZE, sizeof(uint8_t));
    if (!g_imuTimestampQueue) {
        ESP_LOGE(TAG, "Failed to create signal queue!");
        return false;
    }
    ESP_LOGI(TAG, "Signal queue created: %zu slots (100ms at 8kHz)", IMU_QUEUE_SIZE);

    // Configure NCS pin as output for SPI chip select
    pinMode(HardwareConfig::IMU_NCS_PIN, OUTPUT);
    digitalWrite(HardwareConfig::IMU_NCS_PIN, HIGH);  // Deselect initially

    // Small delay for pins to settle
    delay(10);

    // Initialize SPI on HSPI bus (separate from LED's FSPI)
    // SPI.begin(sck, miso, mosi, ss)
    SPI_IMU.begin(HardwareConfig::IMU_SCL_PIN,   // SCK
                  HardwareConfig::IMU_ADO_PIN,   // MISO
                  HardwareConfig::IMU_SDA_PIN,   // MOSI
                  HardwareConfig::IMU_NCS_PIN);  // SS

    // Create library instance with SPI
    // MPU9250_WE(SPIClass*, csPin, mosiPin, misoPin, sckPin, useSPI, useSPIHS)
    m_imu = new MPU9250_WE(&SPI_IMU,
                           HardwareConfig::IMU_NCS_PIN,
                           HardwareConfig::IMU_SDA_PIN,
                           HardwareConfig::IMU_ADO_PIN,
                           HardwareConfig::IMU_SCL_PIN,
                           true,   // useSPI
                           true);  // useSPIHS (high-speed)
    m_imu->setSPIClockSpeed(IMU_SPI_CLOCK);

    if (!m_imu->init()) {
        ESP_LOGE(TAG, "Library init failed!");
        return false;
    }

    // Auto-offsets calibration (keeps device still briefly)
    ESP_LOGI(TAG, "Calibrating offsets (keep still)...");
    delay(1000);
    m_imu->autoOffsets();

    // Store calibration offsets for use in fast readRaw()
    m_accOffset = m_imu->getAccOffsets();
    m_gyrOffset = m_imu->getGyrOffsets();
    ESP_LOGI(TAG, "Calibration offsets - Accel: (%.1f, %.1f, %.1f) Gyro: (%.1f, %.1f, %.1f)",
                  m_accOffset.x, m_accOffset.y, m_accOffset.z,
                  m_gyrOffset.x, m_gyrOffset.y, m_gyrOffset.z);

    // Configure accelerometer: ±16g range
    // Range factor = 1 << enum_value (matching library's internal: accRangeFactor = 1<<accRange)
    // IMPORTANT: Python telemetry enrichment must match these values.
    // See: tools/pov_tools/serial_comm.py (ACCEL_RANGE_G, GYRO_RANGE_DPS)
    constexpr MPU9250_accRange ACC_RANGE = MPU9250_ACC_RANGE_16G;
    m_imu->setAccRange(ACC_RANGE);
    m_accRangeFactor = 1 << ACC_RANGE;

    // Configure gyroscope: ±2000°/s range
    constexpr MPU9250_gyroRange GYR_RANGE = MPU9250_GYRO_RANGE_2000;
    m_imu->setGyrRange(GYR_RANGE);
    m_gyrRangeFactor = 1 << GYR_RANGE;

    // DLPF=7 (disabled) for 8kHz base rate
    m_imu->enableAccDLPF(false);
    m_imu->setGyrDLPF(MPU9250_DLPF_7);

    // Sample rate divider = 0 (no division, 8kHz output)
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
    ESP_LOGI(TAG, "Initial accel: X=%.2f Y=%.2f Z=%.2f g",
                  accelG.x, accelG.y, accelG.z);
    ESP_LOGI(TAG, "Initial gyro: X=%.1f Y=%.1f Z=%.1f dps",
                  gyroDps.x, gyroDps.y, gyroDps.z);

    m_ready = true;
    ESP_LOGI(TAG, "Ready (8kHz, ±16g accel, ±2000°/s gyro, SPI 20MHz, DATA_READY on INT)");
    return true;
}

bool Imu::read(xyzFloat& accel, xyzFloat& gyro) {
    if (!m_ready || !m_imu) return false;

    // Read corrected raw values (int16 as float, WITH calibration offsets applied)
    accel = m_imu->getCorrectedAccRawValues();
    gyro = m_imu->getCorrectedGyrRawValues();

    return true;
}

bool Imu::readRaw(int16_t& ax, int16_t& ay, int16_t& az,
                  int16_t& gx, int16_t& gy, int16_t& gz) {
    if (!m_ready) return false;

    // Fast burst read: 14 bytes from ACCEL_XOUT_H (0x3B)
    // Format: accelX(2) + accelY(2) + accelZ(2) + temp(2) + gyroX(2) + gyroY(2) + gyroZ(2)
    uint8_t buffer[14];

    SPI_IMU.beginTransaction(SPISettings(IMU_SPI_CLOCK, MSBFIRST, SPI_MODE3));
    digitalWrite(HardwareConfig::IMU_NCS_PIN, LOW);
    SPI_IMU.transfer(0x3B | 0x80);  // ACCEL_XOUT_H with read flag
    for (int i = 0; i < 14; i++) {
        buffer[i] = SPI_IMU.transfer(0x00);
    }
    digitalWrite(HardwareConfig::IMU_NCS_PIN, HIGH);
    SPI_IMU.endTransaction();

    // Parse big-endian 16-bit values and apply calibration offsets
    int16_t rawAx = static_cast<int16_t>((buffer[0] << 8) | buffer[1]);
    int16_t rawAy = static_cast<int16_t>((buffer[2] << 8) | buffer[3]);
    int16_t rawAz = static_cast<int16_t>((buffer[4] << 8) | buffer[5]);
    // buffer[6,7] = temperature (ignored)
    int16_t rawGx = static_cast<int16_t>((buffer[8] << 8) | buffer[9]);
    int16_t rawGy = static_cast<int16_t>((buffer[10] << 8) | buffer[11]);
    int16_t rawGz = static_cast<int16_t>((buffer[12] << 8) | buffer[13]);

    // Apply calibration offsets exactly as library does in correctAccRawValues/correctGyrRawValues:
    // rawValues.x -= (accOffsetVal.x / accRangeFactor);
    ax = static_cast<int16_t>(rawAx - (m_accOffset.x / m_accRangeFactor));
    ay = static_cast<int16_t>(rawAy - (m_accOffset.y / m_accRangeFactor));
    az = static_cast<int16_t>(rawAz - (m_accOffset.z / m_accRangeFactor));
    gx = static_cast<int16_t>(rawGx - (m_gyrOffset.x / m_gyrRangeFactor));
    gy = static_cast<int16_t>(rawGy - (m_gyrOffset.y / m_gyrRangeFactor));
    gz = static_cast<int16_t>(rawGz - (m_gyrOffset.z / m_gyrRangeFactor));

    return true;
}

bool Imu::sampleReady() {
    if (!g_imuTimestampQueue) return false;
    return uxQueueMessagesWaiting(g_imuTimestampQueue) > 0;
}

bool Imu::waitForSample(TickType_t timeout) {
    if (!g_imuTimestampQueue) return false;
    uint8_t signal;
    return xQueueReceive(g_imuTimestampQueue, &signal, timeout) == pdTRUE;
}
