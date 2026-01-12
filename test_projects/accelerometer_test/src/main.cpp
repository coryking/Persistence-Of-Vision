#include <Arduino.h>
#include <Wire.h>
#include <MPU9250_WE.h>

// FreeRTOS and ESP-IDF headers
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_timer.h>
#include <driver/gpio.h>

// ============================================================================
// EXPERIMENT CONFIGURATION
// ============================================================================
//
// This test measures MPU-9250 I2C read performance to find hardware limits.
//
// --- DLPF (Digital Low Pass Filter) Mode ---
// From MPU-9250 datasheet section 3.4.2:
//
// CRITICAL: DLPF_CFG affects BOTH bandwidth AND base sample rate!
//
// DLPF_CFG | Gyro BW  | Accel BW | Base Rate | Notes
// ---------|----------|----------|-----------|---------------------------
//    0     |  250 Hz  |  460 Hz  |  8 kHz    | High BW but 8kHz base!
//    1     |  184 Hz  |  184 Hz  |  1 kHz    | Good balance
//    2     |   92 Hz  |   92 Hz  |  1 kHz    |
//    3     |   41 Hz  |   41 Hz  |  1 kHz    |
//    4     |   20 Hz  |   20 Hz  |  1 kHz    |
//    5     |   10 Hz  |   10 Hz  |  1 kHz    |
//    6     |    5 Hz  |    5 Hz  |  1 kHz    | Most filtering
//    7     | 3600 Hz  |  460 Hz  |  8 kHz    | DLPF bypass, 8kHz base
//
// KEY INSIGHT: Only DLPF_CFG 1-6 give 1kHz base rate.
//              DLPF_CFG 0 and 7 both give 8kHz base rate!
//
// --- Sample Rate Divider ---
// Output Rate = Base Rate / (1 + SMPLRT_DIV)
//
// NOTE: From testing, SMPLRT_DIV may not affect DATA_READY interrupt rate
//       when DLPF_CFG=0 or 7. The interrupt appears to fire at 8kHz regardless
//       of divider setting. More investigation needed.
//
// ============================================================================

// --- I2C Clock Speed (compile-time only, requires Wire reinit) ---
constexpr uint32_t I2C_CLOCK_HZ = 400000;

// --- Test Presets (runtime selectable via keys 1-6) ---
struct TestPreset {
    uint8_t dlpf;
    uint8_t divider;
    const char* name;
    uint32_t targetHz;  // Pre-calculated for display
};

constexpr TestPreset PRESETS[] = {
    {1, 0, "1kHz baseline",      1000},  // Key '1' - DLPF=1, 1kHz base, 184Hz BW
    {7, 0, "8kHz (will drop)",   8000},  // Key '2' - DLPF=7, 8kHz base (I2C can't keep up)
    {7, 1, "4kHz",               4000},  // Key '3' - 8kHz/2 (still too fast for I2C)
    {7, 3, "2kHz",               2000},  // Key '4' - 8kHz/4 (at I2C edge)
    {7, 7, "1kHz no-DLPF",       1000},  // Key '5' - 8kHz/8 (test if DIV works with DLPF=7)
    {6, 0, "1kHz 5Hz-BW",        1000},  // Key '6' - DLPF=6, 1kHz base, 5Hz BW (contrast test)
};
constexpr size_t NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

// Current active preset
uint8_t g_currentPreset = 0;

// ============================================================================
// Pin Configuration
// ============================================================================

constexpr uint8_t IMU_SCL_PIN = D1;    // Blue wire - I2C Clock
constexpr uint8_t IMU_SDA_PIN = D0;    // Green wire - I2C Data
constexpr uint8_t IMU_ADO_PIN = D3;    // Purple wire - I2C addr LSB
constexpr uint8_t IMU_NCS_PIN = D2;    // Orange wire - SPI chip select
constexpr uint8_t IMU_INT_PIN = D9;    // Yellow wire - DATA_READY interrupt

constexpr uint8_t MPU9250_ADDR = 0x68;

// ============================================================================
// Data Structures
// ============================================================================

using timestamp_t = int64_t;

struct ImuRawData {
    int16_t accelX, accelY, accelZ;
    int16_t temp;
    int16_t gyroX, gyroY, gyroZ;
};

// Note: Not using volatile here - single task writes, loop() reads.
// Minor races are acceptable for this test bench.
struct ImuMetrics {
    // Pipeline counters
    uint32_t isrCount;        // Total DATA_READY interrupts (queued + dropped)
    uint32_t sampleCount;     // Successfully read via I2C
    uint32_t droppedCount;    // Queue overflows (couldn't queue timestamp)

    // I2C read timing (microseconds)
    uint32_t lastI2cReadUs;
    uint32_t minI2cReadUs;
    uint32_t maxI2cReadUs;
    uint64_t sumI2cReadUs;

    // ISR-to-task latency (microseconds)
    uint32_t lastIsrToTaskUs;
    uint32_t minIsrToTaskUs;
    uint32_t maxIsrToTaskUs;

    // Inter-sample interval for jitter (microseconds)
    uint32_t lastIntervalUs;
    uint32_t minIntervalUs;
    uint32_t maxIntervalUs;

    // Queue stats
    uint32_t maxQueueDepth;
    uint32_t currentQueueDepth;  // Snapshot for display
};

// ============================================================================
// Global State
// ============================================================================

MPU9250_WE mpu = MPU9250_WE(MPU9250_ADDR);

// FreeRTOS handles
QueueHandle_t g_timestampQueue = nullptr;
TaskHandle_t g_imuTaskHandle = nullptr;

constexpr size_t QUEUE_SIZE = 100;

// Metrics and latest sample
ImuMetrics g_metrics;
ImuRawData g_latestSample;  // Not volatile - single writer (task), single reader (loop)

// Rate calculation (updated once per second in loop)
volatile float g_isrRate = 0.0f;       // DATA_READY interrupts per second
volatile float g_taskRate = 0.0f;      // Samples successfully read per second
volatile float g_dropRate = 0.0f;      // Queue overflows per second

// ============================================================================
// ANSI Terminal Helpers
// ============================================================================

#define ANSI_CLEAR_SCREEN "\033[2J"
#define ANSI_HOME         "\033[H"
#define ANSI_HIDE_CURSOR  "\033[?25l"
#define ANSI_SHOW_CURSOR  "\033[?25h"
#define ANSI_CLEAR_LINE   "\033[2K"

void moveTo(int row, int col) {
    Serial.printf("\033[%d;%dH", row, col);
}

// ============================================================================
// 14-byte Burst Read
// ============================================================================

bool readAllSensorData(ImuRawData& data) {
    // MPU-9250 register layout starting at 0x3B:
    // 0x3B-0x40: ACCEL_XOUT_H/L, ACCEL_YOUT_H/L, ACCEL_ZOUT_H/L (6 bytes)
    // 0x41-0x42: TEMP_OUT_H/L (2 bytes)
    // 0x43-0x48: GYRO_XOUT_H/L, GYRO_YOUT_H/L, GYRO_ZOUT_H/L (6 bytes)
    // Total: 14 bytes in one I2C transaction

    Wire.beginTransmission(MPU9250_ADDR);
    Wire.write(0x3B);  // ACCEL_XOUT_H
    if (Wire.endTransmission(false) != 0) return false;  // Repeated start

    if (Wire.requestFrom(MPU9250_ADDR, (uint8_t)14) != 14) return false;

    // Read in order: accel (6), temp (2), gyro (6)
    data.accelX = (Wire.read() << 8) | Wire.read();
    data.accelY = (Wire.read() << 8) | Wire.read();
    data.accelZ = (Wire.read() << 8) | Wire.read();
    data.temp   = (Wire.read() << 8) | Wire.read();
    data.gyroX  = (Wire.read() << 8) | Wire.read();
    data.gyroY  = (Wire.read() << 8) | Wire.read();
    data.gyroZ  = (Wire.read() << 8) | Wire.read();

    return true;
}

// ============================================================================
// ISR - Minimal: just timestamp and queue
// ============================================================================

static void IRAM_ATTR imuIsrWrapper(void* arg) {
    (void)arg;
    timestamp_t now = esp_timer_get_time();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Always count ISR invocations (measures actual MPU interrupt rate)
    g_metrics.isrCount++;

    if (xQueueSendFromISR(g_timestampQueue, &now, &xHigherPriorityTaskWoken) != pdTRUE) {
        // Queue full - couldn't enqueue timestamp
        g_metrics.droppedCount++;
    }

    portYIELD_FROM_ISR();
}

// ============================================================================
// IMU Task - Processes queue, reads I2C, updates metrics
// ============================================================================

void imuTask(void* param) {
    (void)param;
    timestamp_t lastSampleTime = 0;

    while (true) {
        timestamp_t isrTimestamp;

        // Block waiting for interrupt timestamp
        if (xQueueReceive(g_timestampQueue, &isrTimestamp, portMAX_DELAY) == pdTRUE) {
            // Measure I2C read time
            timestamp_t readStart = esp_timer_get_time();

            ImuRawData data;
            bool success = readAllSensorData(data);

            timestamp_t readEnd = esp_timer_get_time();

            if (success) {
                uint32_t i2cTimeUs = (uint32_t)(readEnd - readStart);
                uint32_t isrToTaskUs = (uint32_t)(readStart - isrTimestamp);

                // Update sample count
                g_metrics.sampleCount++;

                // Update I2C timing
                g_metrics.lastI2cReadUs = i2cTimeUs;
                if (i2cTimeUs < g_metrics.minI2cReadUs || g_metrics.minI2cReadUs == 0)
                    g_metrics.minI2cReadUs = i2cTimeUs;
                if (i2cTimeUs > g_metrics.maxI2cReadUs)
                    g_metrics.maxI2cReadUs = i2cTimeUs;
                g_metrics.sumI2cReadUs += i2cTimeUs;

                // Update ISR-to-task latency
                g_metrics.lastIsrToTaskUs = isrToTaskUs;
                if (isrToTaskUs < g_metrics.minIsrToTaskUs || g_metrics.minIsrToTaskUs == 0)
                    g_metrics.minIsrToTaskUs = isrToTaskUs;
                if (isrToTaskUs > g_metrics.maxIsrToTaskUs)
                    g_metrics.maxIsrToTaskUs = isrToTaskUs;

                // Update inter-sample interval (jitter tracking)
                if (lastSampleTime > 0) {
                    uint32_t interval = (uint32_t)(isrTimestamp - lastSampleTime);
                    g_metrics.lastIntervalUs = interval;
                    if (interval < g_metrics.minIntervalUs || g_metrics.minIntervalUs == 0)
                        g_metrics.minIntervalUs = interval;
                    if (interval > g_metrics.maxIntervalUs)
                        g_metrics.maxIntervalUs = interval;
                }
                lastSampleTime = isrTimestamp;

                // Update latest sample for display
                g_latestSample = data;
            }

            // Track queue depth (current and high water mark)
            UBaseType_t queueDepth = uxQueueMessagesWaiting(g_timestampQueue);
            g_metrics.currentQueueDepth = queueDepth;
            if (queueDepth > g_metrics.maxQueueDepth)
                g_metrics.maxQueueDepth = queueDepth;
        }
    }
}

// ============================================================================
// Metrics Reset
// ============================================================================

void resetMetrics() {
    memset((void*)&g_metrics, 0, sizeof(g_metrics));
    g_isrRate = 0.0f;
    g_taskRate = 0.0f;
    g_dropRate = 0.0f;
}

// ============================================================================
// Reconfigure - Switch preset at runtime
// ============================================================================

void reconfigure(uint8_t presetIndex) {
    if (presetIndex >= NUM_PRESETS) return;

    // 1. Disable interrupt temporarily
    gpio_num_t intPin = static_cast<gpio_num_t>(IMU_INT_PIN);
    gpio_isr_handler_remove(intPin);

    // 2. Flush the queue
    xQueueReset(g_timestampQueue);

    // 3. Apply new MPU settings
    const TestPreset& p = PRESETS[presetIndex];

    // Enable/disable DLPF based on mode
    if (p.dlpf == 7) {
        // DLPF disabled = 8kHz base rate
        mpu.enableAccDLPF(false);
        // For gyro, DLPF_7 (or setting FCHOICE) gets 8kHz
        mpu.setGyrDLPF(MPU9250_DLPF_7);
    } else {
        mpu.enableAccDLPF(true);
        mpu.setAccDLPF(static_cast<MPU9250_dlpf>(p.dlpf));
        mpu.setGyrDLPF(static_cast<MPU9250_dlpf>(p.dlpf));
    }
    mpu.setSampleRateDivider(p.divider);

    // 4. Reset all metrics
    resetMetrics();

    // 5. Update current preset
    g_currentPreset = presetIndex;

    // 6. Re-enable interrupt
    gpio_isr_handler_add(intPin, imuIsrWrapper, nullptr);
}

// ============================================================================
// Display Functions - Pipeline View
// ============================================================================

namespace Layout {
    constexpr int TITLE = 1;
    constexpr int PRESETS_1 = 2;
    constexpr int PRESETS_2 = 3;
    constexpr int PRESETS_3 = 4;
    constexpr int CURRENT = 6;
    constexpr int PIPELINE_HEADER = 8;
    constexpr int PIPELINE_COLS = 9;
    constexpr int PIPELINE_MPU = 10;
    constexpr int PIPELINE_QUEUE = 11;
    constexpr int PIPELINE_I2C = 12;
    constexpr int TIMING_HEADER = 14;
    constexpr int TIMING_I2C = 15;
    constexpr int TIMING_LATENCY = 16;
    constexpr int TIMING_JITTER = 17;
    constexpr int VALUES_HEADER = 19;
    constexpr int VALUES_ACCEL = 20;
    constexpr int VALUES_GYRO = 21;
}

void printStaticLayout() {
    Serial.print(ANSI_CLEAR_SCREEN ANSI_HOME ANSI_HIDE_CURSOR);

    moveTo(Layout::TITLE, 1);
    Serial.println("=== MPU-9250 I2C Rate Test ===");

    moveTo(Layout::PRESETS_1, 1);
    Serial.println("[1] 1kHz baseline    [4] 2kHz");
    moveTo(Layout::PRESETS_2, 1);
    Serial.println("[2] 8kHz (drop test) [5] 1kHz no-DLPF");
    moveTo(Layout::PRESETS_3, 1);
    Serial.println("[3] 4kHz             [6] 1kHz 5Hz-BW      [r] reset stats");

    // Pipeline table header
    moveTo(Layout::PIPELINE_HEADER, 1);
    Serial.println("SAMPLE PIPELINE");
    moveTo(Layout::PIPELINE_COLS, 1);
    //         "  Stage              Rate/s    Queue   Drops   Status"
    Serial.println("  Stage              Rate/s    Queue   Drops   Status");
    Serial.println("  -----------------  --------  ------  ------  ----------------");

    moveTo(Layout::TIMING_HEADER, 1);
    Serial.println("TIMING (microseconds)");
    moveTo(Layout::TIMING_I2C, 1);
    Serial.println("  I2C read:");
    moveTo(Layout::TIMING_LATENCY, 1);
    Serial.println("  ISR latency:");
    moveTo(Layout::TIMING_JITTER, 1);
    Serial.println("  Jitter:");

    moveTo(Layout::VALUES_HEADER, 1);
    Serial.println("CURRENT VALUES");
    moveTo(Layout::VALUES_ACCEL, 1);
    Serial.println("  Accel X:          Y:          Z:");
    moveTo(Layout::VALUES_GYRO, 1);
    Serial.println("  Gyro  X:          Y:          Z:");
}

void updateDisplay() {
    const TestPreset& p = PRESETS[g_currentPreset];

    // Current preset info
    moveTo(Layout::CURRENT, 1);
    Serial.print(ANSI_CLEAR_LINE);
    Serial.printf(">>> Preset %d: %s  (DLPF=%d, DIV=%d, target=%luHz)",
                  g_currentPreset + 1, p.name, p.dlpf, p.divider, p.targetHz);

    // Calculate I2C throughput limit
    uint32_t avgI2cUs = (g_metrics.sampleCount > 0)
        ? (uint32_t)(g_metrics.sumI2cReadUs / g_metrics.sampleCount) : 470;
    uint32_t i2cMaxRate = (avgI2cUs > 0) ? (1000000 / avgI2cUs) : 0;

    // Pipeline row 1: MPU -> ISR (DATA_READY interrupts)
    moveTo(Layout::PIPELINE_MPU, 1);
    Serial.print(ANSI_CLEAR_LINE);
    const char* mpuStatus = "";
    if (g_isrRate > p.targetHz * 1.1f) mpuStatus = "ABOVE TARGET";
    else if (g_isrRate < p.targetHz * 0.9f && g_isrRate > 0) mpuStatus = "BELOW TARGET";
    else if (g_isrRate > 0) mpuStatus = "OK";
    Serial.printf("  MPU DATA_READY     %7.0f   -       -       %s", g_isrRate, mpuStatus);

    // Pipeline row 2: Queue (ISR -> Task)
    moveTo(Layout::PIPELINE_QUEUE, 1);
    Serial.print(ANSI_CLEAR_LINE);
    const char* queueStatus = "";
    if (g_metrics.currentQueueDepth >= QUEUE_SIZE - 1) queueStatus = "FULL!";
    else if (g_metrics.currentQueueDepth > QUEUE_SIZE / 2) queueStatus = "BACKING UP";
    else if (g_dropRate > 0) queueStatus = "OVERFLOW";
    else queueStatus = "OK";
    Serial.printf("  Queue (%zu slots)   %7.0f   %3lu     %6lu  %s",
                  QUEUE_SIZE, g_taskRate, g_metrics.currentQueueDepth,
                  g_metrics.droppedCount, queueStatus);

    // Pipeline row 3: I2C output (Task -> Done)
    moveTo(Layout::PIPELINE_I2C, 1);
    Serial.print(ANSI_CLEAR_LINE);
    const char* i2cStatus = "";
    if (g_taskRate >= i2cMaxRate * 0.95f && g_taskRate > 0) i2cStatus = "AT LIMIT";
    else if (g_taskRate > 0) i2cStatus = "OK";
    Serial.printf("  I2C Read (max %lu) %7.0f   -       -       %s",
                  i2cMaxRate, g_taskRate, i2cStatus);

    // Timing details
    moveTo(Layout::TIMING_I2C, 14);
    Serial.printf("%lu avg (%lu-%lu)",
                  avgI2cUs, g_metrics.minI2cReadUs, g_metrics.maxI2cReadUs);

    moveTo(Layout::TIMING_LATENCY, 16);
    // Show latency with warning if high
    if (g_metrics.maxIsrToTaskUs > 1000) {
        Serial.printf("%lu-%lu (max %lums!)",
                      g_metrics.minIsrToTaskUs, g_metrics.lastIsrToTaskUs,
                      g_metrics.maxIsrToTaskUs / 1000);
    } else {
        Serial.printf("%lu-%lu",
                      g_metrics.minIsrToTaskUs, g_metrics.maxIsrToTaskUs);
    }

    moveTo(Layout::TIMING_JITTER, 11);
    uint32_t expectedUs = (p.targetHz > 0) ? (1000000 / p.targetHz) : 0;
    Serial.printf("%lu (%lu-%lu, expect %lu)",
                  g_metrics.lastIntervalUs, g_metrics.minIntervalUs,
                  g_metrics.maxIntervalUs, expectedUs);

    // Current values
    float accelScale = 16.0f / 32768.0f;
    float gyroScale = 2000.0f / 32768.0f;
    ImuRawData sample = g_latestSample;

    moveTo(Layout::VALUES_ACCEL, 11);
    Serial.printf("%+7.3fg", sample.accelX * accelScale);
    moveTo(Layout::VALUES_ACCEL, 23);
    Serial.printf("%+7.3fg", sample.accelY * accelScale);
    moveTo(Layout::VALUES_ACCEL, 35);
    Serial.printf("%+7.3fg", sample.accelZ * accelScale);

    moveTo(Layout::VALUES_GYRO, 11);
    Serial.printf("%+7.1f", sample.gyroX * gyroScale);
    moveTo(Layout::VALUES_GYRO, 23);
    Serial.printf("%+7.1f", sample.gyroY * gyroScale);
    moveTo(Layout::VALUES_GYRO, 35);
    Serial.printf("%+7.1f", sample.gyroZ * gyroScale);
}

// ============================================================================
// Setup Helpers
// ============================================================================

void waitForSerial() {
    while (!Serial) {
        delay(100);
    }
    delay(100);
}

void waitForKeypress(const char* prompt) {
    while (Serial.available()) Serial.read();

    while (!Serial.available()) {
        Serial.printf("\r%s%s", ANSI_CLEAR_LINE, prompt);
        for (int i = 0; i < 100 && !Serial.available(); i++) {
            delay(10);
        }
    }

    while (Serial.available()) Serial.read();
    Serial.println();
}

// ============================================================================
// Arduino Entry Points
// ============================================================================

void setup() {
    Serial.begin(115200);

    // Configure pins for I2C mode BEFORE waiting for serial
    pinMode(IMU_NCS_PIN, OUTPUT);
    digitalWrite(IMU_NCS_PIN, HIGH);  // HIGH = I2C mode
    pinMode(IMU_ADO_PIN, OUTPUT);
    digitalWrite(IMU_ADO_PIN, LOW);   // LOW = address 0x68

    waitForSerial();

    Serial.print(ANSI_CLEAR_SCREEN ANSI_HOME);
    Serial.println("=== MPU-9250 I2C Rate Test ===\n");

    waitForKeypress("Press any key to initialize IMU...");

    Serial.printf("Pins: SDA=%d, SCL=%d, NCS=%d, ADO=%d, INT=%d\n",
                  IMU_SDA_PIN, IMU_SCL_PIN, IMU_NCS_PIN, IMU_ADO_PIN, IMU_INT_PIN);
    Serial.printf("I2C Clock: %lu Hz\n\n", I2C_CLOCK_HZ);

    // Initialize I2C
    Wire.begin(IMU_SDA_PIN, IMU_SCL_PIN);
    Wire.setClock(I2C_CLOCK_HZ);

    Serial.println("Initializing MPU-9250...");

    if (!mpu.init()) {
        Serial.println("ERROR: MPU-9250 not responding!");
        Serial.println("Check wiring and connections.");
        while (true) delay(1000);
    }

    Serial.println("MPU-9250 found!\n");

    // Create timestamp queue
    g_timestampQueue = xQueueCreate(QUEUE_SIZE, sizeof(timestamp_t));
    if (!g_timestampQueue) {
        Serial.println("ERROR: Failed to create queue!");
        while (true) delay(1000);
    }
    Serial.printf("Timestamp queue created: %zu slots\n", QUEUE_SIZE);

    // Configure ranges
    mpu.setAccRange(MPU9250_ACC_RANGE_16G);
    mpu.setGyrRange(MPU9250_GYRO_RANGE_2000);

    // Apply initial preset settings (preset 0 = 1kHz baseline)
    const TestPreset& p = PRESETS[0];
    mpu.enableAccDLPF(true);
    mpu.setAccDLPF(static_cast<MPU9250_dlpf>(p.dlpf));
    mpu.setGyrDLPF(static_cast<MPU9250_dlpf>(p.dlpf));
    mpu.setSampleRateDivider(p.divider);

    Serial.println("Config: Accel +/-16g, Gyro +/-2000dps\n");

    // Calibration
    Serial.println("CALIBRATION: Keep the sensor STILL and LEVEL!");
    waitForKeypress("Press any key to start calibration...");

    Serial.println("Calibrating... (takes a few seconds)");
    mpu.autoOffsets();

    xyzFloat accOffsets = mpu.getAccOffsets();
    xyzFloat gyrOffsets = mpu.getGyrOffsets();

    Serial.println("Calibration complete!");
    Serial.printf("  Accel offsets: X=%.3f Y=%.3f Z=%.3f\n",
                  accOffsets.x, accOffsets.y, accOffsets.z);
    Serial.printf("  Gyro offsets:  X=%.2f Y=%.2f Z=%.2f\n\n",
                  gyrOffsets.x, gyrOffsets.y, gyrOffsets.z);

    // Enable DATA_READY interrupt
    mpu.enableInterrupt(MPU9250_DATA_READY);

    // Configure INT pin using ESP-IDF API
    gpio_num_t intPin = static_cast<gpio_num_t>(IMU_INT_PIN);
    gpio_set_direction(intPin, GPIO_MODE_INPUT);
    gpio_set_intr_type(intPin, GPIO_INTR_POSEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(intPin, imuIsrWrapper, nullptr);

    Serial.println("Interrupt configured on INT pin");

    // Create IMU task pinned to Core 0 (leave Core 1 for loop/display)
    xTaskCreatePinnedToCore(imuTask, "IMU", 4096, nullptr,
                            tskIDLE_PRIORITY + 2, &g_imuTaskHandle, 0);

    Serial.println("IMU task created on Core 0\n");

    // Initialize metrics
    resetMetrics();

    waitForKeypress("Press any key for live display...");

    printStaticLayout();
}

void loop() {
    static uint32_t lastDisplayUpdate = 0;
    static uint32_t lastRateCalc = 0;
    static uint32_t lastIsrCount = 0;
    static uint32_t lastSampleCount = 0;
    static uint32_t lastDropCount = 0;

    uint32_t now = millis();

    // Calculate rates every second
    if (now - lastRateCalc >= 1000) {
        uint32_t elapsed = now - lastRateCalc;
        float scale = 1000.0f / elapsed;

        uint32_t currIsr = g_metrics.isrCount;
        uint32_t currSample = g_metrics.sampleCount;
        uint32_t currDrop = g_metrics.droppedCount;

        g_isrRate = (currIsr - lastIsrCount) * scale;
        g_taskRate = (currSample - lastSampleCount) * scale;
        g_dropRate = (currDrop - lastDropCount) * scale;

        lastIsrCount = currIsr;
        lastSampleCount = currSample;
        lastDropCount = currDrop;
        lastRateCalc = now;
    }

    // Update display at 20Hz (every 50ms)
    if (now - lastDisplayUpdate >= 50) {
        updateDisplay();
        lastDisplayUpdate = now;
    }

    // Handle serial input
    if (Serial.available()) {
        char c = Serial.read();

        if (c >= '1' && c <= '6') {
            reconfigure(c - '1');
            printStaticLayout();  // Redraw after preset change
        } else if (c == 'r' || c == 'R') {
            resetMetrics();
        }
    }

    delay(1);
}
