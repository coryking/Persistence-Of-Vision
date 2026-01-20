#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <MPU9250_WE.h>
#include <NeoPixelBus.h>
#include <vector>

// FreeRTOS and ESP-IDF headers
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_timer.h>
#include <driver/gpio.h>

// ============================================================================
// IMU SAMPLING BENCHMARK
// ============================================================================
//
// Automated benchmark comparing I2C vs SPI performance for MPU-9250.
// Runs each interface + preset combination for 5 seconds, collects stats,
// and outputs a comparison table at the end.
//
// This informs optimal settings for production led_display IMU integration.
//
// ============================================================================

// ============================================================================
// Interface Configuration
// ============================================================================

enum class ImuInterface {
    I2C_400KHZ,
    SPI_1MHZ,
    SPI_20MHZ
};

const char* getInterfaceName(ImuInterface iface) {
    switch (iface) {
        case ImuInterface::I2C_400KHZ: return "I2C 400kHz";
        case ImuInterface::SPI_1MHZ:   return "SPI 1MHz";
        case ImuInterface::SPI_20MHZ:  return "SPI 20MHz";
        default: return "Unknown";
    }
}

// ============================================================================
// Pin Configuration
// ============================================================================

constexpr uint8_t IMU_SCL_PIN = D0;    // Orange wire - I2C Clock / SPI SCLK
constexpr uint8_t IMU_SDA_PIN = D1;    // Yellow wire - I2C Data / SPI MOSI
constexpr uint8_t IMU_ADO_PIN = D2;    // Green wire - I2C addr LSB / SPI MISO
constexpr uint8_t IMU_INT_PIN = D3;    // Blue wire - DATA_READY interrupt
constexpr uint8_t IMU_NCS_PIN = D4;    // Black wire - SPI chip select (HIGH = I2C mode)

constexpr uint8_t MPU9250_ADDR = 0x68;

// --- LED Configuration ---
constexpr uint8_t LED_DATA_PIN = D10;  // SK9822 Data (MOSI)
constexpr uint8_t LED_CLK_PIN = D8;    // SK9822 Clock (SCK)
constexpr uint16_t NUM_LEDS = 33;

// ============================================================================
// Test Presets
// ============================================================================

struct TestPreset {
    uint8_t dlpf;
    uint8_t divider;
    const char* name;
    uint32_t targetHz;
};

constexpr TestPreset PRESETS[] = {
    {1, 0, "1kHz baseline",      1000},  // DLPF=1, 1kHz base, 184Hz BW
    {7, 0, "8kHz (stress test)", 8000},  // DLPF=7, 8kHz base (tests max throughput)
    {7, 1, "4kHz",               4000},  // 8kHz/2
    {7, 3, "2kHz",               2000},  // 8kHz/4
    {7, 7, "1kHz no-DLPF",       1000},  // 8kHz/8 (test if DIV works with DLPF=7)
    {6, 0, "1kHz 5Hz-BW",        1000},  // DLPF=6, 1kHz base, 5Hz BW
};
constexpr size_t NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

// ============================================================================
// Benchmark Configuration
// ============================================================================

constexpr uint32_t BENCHMARK_DURATION_MS = 5000;  // 5 seconds per preset
constexpr size_t QUEUE_SIZE = 100;

// ============================================================================
// Data Structures
// ============================================================================

using timestamp_t = int64_t;

struct ImuRawData {
    int16_t accelX, accelY, accelZ;
    int16_t temp;
    int16_t gyroX, gyroY, gyroZ;
};

// Histogram configurations tuned to observed timing ranges:
// - IMU reads: I2C ~475-500us, SPI ~87-113us
// - LED timing: ~50-84us
// - Total: I2C ~528-537us, SPI ~137-162us

// IMU read histogram: 5us buckets, 0-600us range (covers both I2C and SPI)
constexpr size_t READ_HIST_BUCKETS = 120;
constexpr uint32_t READ_HIST_BUCKET_US = 5;

// LED histogram: 2us buckets, 0-100us range
constexpr size_t LED_HIST_BUCKETS = 50;
constexpr uint32_t LED_HIST_BUCKET_US = 2;

// Total loop histogram: 5us buckets, 0-700us range
constexpr size_t TOTAL_HIST_BUCKETS = 140;
constexpr uint32_t TOTAL_HIST_BUCKET_US = 5;

template<size_t N, uint32_t BUCKET_US>
struct Histogram {
    uint32_t buckets[N];
    uint32_t overflow;  // Values > N * BUCKET_US

    void reset() volatile {
        for (size_t i = 0; i < N; i++) {
            buckets[i] = 0;
        }
        overflow = 0;
    }

    void record(uint32_t valueUs) volatile {
        size_t idx = valueUs / BUCKET_US;
        if (idx >= N) {
            overflow++;
        } else {
            buckets[idx]++;
        }
    }

    // Returns value at given percentile (0-100)
    uint32_t percentile(uint32_t pct, uint32_t totalCount) const volatile {
        if (totalCount == 0) return 0;
        uint32_t target = (totalCount * pct) / 100;
        uint32_t cumulative = 0;

        for (size_t i = 0; i < N; i++) {
            cumulative += buckets[i];
            if (cumulative >= target) {
                // Return midpoint of bucket
                return (i * BUCKET_US) + (BUCKET_US / 2);
            }
        }
        // In overflow bucket
        return N * BUCKET_US;
    }
};

// Type aliases for each histogram type
using ReadHistogram = Histogram<READ_HIST_BUCKETS, READ_HIST_BUCKET_US>;
using LedHistogram = Histogram<LED_HIST_BUCKETS, LED_HIST_BUCKET_US>;
using TotalHistogram = Histogram<TOTAL_HIST_BUCKETS, TOTAL_HIST_BUCKET_US>;

struct ImuMetrics {
    uint32_t isrCount;
    uint32_t sampleCount;
    uint32_t droppedCount;

    // IMU read timing
    uint32_t minReadUs;
    uint32_t maxReadUs;
    uint64_t sumReadUs;
    ReadHistogram readHist;

    // ISR-to-task latency
    uint32_t minLatencyUs;
    uint32_t maxLatencyUs;
    uint64_t sumLatencyUs;

    // LED timing (Show() call + DMA wait)
    uint32_t minLedUs;
    uint32_t maxLedUs;
    uint64_t sumLedUs;
    LedHistogram ledHist;

    // Total loop time (read + LED)
    TotalHistogram totalHist;

    // Inter-sample interval (jitter tracking)
    uint32_t minIntervalUs;
    uint32_t maxIntervalUs;
    uint64_t sumIntervalUs;
    uint32_t intervalCount;

    // Queue pressure
    uint32_t maxQueueDepth;
};

struct BenchmarkResult {
    ImuInterface interface;
    uint8_t presetIndex;
    uint32_t durationMs;

    uint32_t isrCount;
    uint32_t sampleCount;
    uint32_t dropCount;

    // IMU Read timing (microseconds)
    uint32_t avgReadUs;
    uint32_t p50ReadUs;
    uint32_t p95ReadUs;
    uint32_t p99ReadUs;
    uint32_t maxReadUs;

    // LED timing
    uint32_t avgLedUs;
    uint32_t p50LedUs;
    uint32_t p95LedUs;
    uint32_t p99LedUs;
    uint32_t maxLedUs;

    // Total loop time (read + LED)
    uint32_t avgTotalUs;
    uint32_t p50TotalUs;
    uint32_t p95TotalUs;
    uint32_t p99TotalUs;
    uint32_t maxTotalUs;

    // Jitter (inter-sample interval)
    uint32_t avgIntervalUs;
    uint32_t minIntervalUs;
    uint32_t maxIntervalUs;

    // Queue
    uint32_t maxQueueDepth;

    // Rates
    float isrRate;
    float sampleRate;
    float dropPct;
};

// ============================================================================
// Global State
// ============================================================================

// LED strip (SK9822/DotStar) on SPI2/FSPI at 40MHz
NeoPixelBus<DotStarBgrFeature, DotStarSpi40MhzMethod> g_ledStrip(NUM_LEDS);

// IMU instance - managed via pointer for runtime interface switching
MPU9250_WE* g_mpu = nullptr;
ImuInterface g_currentInterface;

// Second SPI instance for IMU on GP-SPI3 (HSPI)
SPIClass SPI_IMU(HSPI);

// SPI clock speed for custom burst reads (set during interface init)
uint32_t g_spiClockSpeed = 1000000;

// FreeRTOS handles
QueueHandle_t g_timestampQueue = nullptr;
TaskHandle_t g_imuTaskHandle = nullptr;

// Metrics (updated by IMU task)
volatile ImuMetrics g_metrics;
volatile bool g_benchmarkRunning = false;

// Benchmark results
std::vector<BenchmarkResult> g_results;

// ============================================================================
// 14-byte Burst Read (Interface-agnostic)
// ============================================================================

bool readAllSensorData_SPI(ImuRawData& data) {
    uint8_t buffer[14];

    SPI_IMU.beginTransaction(SPISettings(g_spiClockSpeed, MSBFIRST, SPI_MODE3));
    digitalWrite(IMU_NCS_PIN, LOW);
    SPI_IMU.transfer(0x3B | 0x80);  // ACCEL_XOUT_H with read flag
    for (int i = 0; i < 14; i++) {
        buffer[i] = SPI_IMU.transfer(0x00);
    }
    digitalWrite(IMU_NCS_PIN, HIGH);
    SPI_IMU.endTransaction();

    data.accelX = (buffer[0] << 8) | buffer[1];
    data.accelY = (buffer[2] << 8) | buffer[3];
    data.accelZ = (buffer[4] << 8) | buffer[5];
    data.temp   = (buffer[6] << 8) | buffer[7];
    data.gyroX  = (buffer[8] << 8) | buffer[9];
    data.gyroY  = (buffer[10] << 8) | buffer[11];
    data.gyroZ  = (buffer[12] << 8) | buffer[13];

    return true;
}

bool readAllSensorData_I2C(ImuRawData& data) {
    Wire.beginTransmission(MPU9250_ADDR);
    Wire.write(0x3B);
    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom(MPU9250_ADDR, (uint8_t)14) != 14) return false;

    data.accelX = (Wire.read() << 8) | Wire.read();
    data.accelY = (Wire.read() << 8) | Wire.read();
    data.accelZ = (Wire.read() << 8) | Wire.read();
    data.temp   = (Wire.read() << 8) | Wire.read();
    data.gyroX  = (Wire.read() << 8) | Wire.read();
    data.gyroY  = (Wire.read() << 8) | Wire.read();
    data.gyroZ  = (Wire.read() << 8) | Wire.read();

    return true;
}

bool readAllSensorData(ImuRawData& data) {
    if (g_currentInterface == ImuInterface::I2C_400KHZ) {
        return readAllSensorData_I2C(data);
    } else {
        return readAllSensorData_SPI(data);
    }
}

// ============================================================================
// ISR - Minimal: just timestamp and queue
// ============================================================================

static void IRAM_ATTR imuIsrWrapper(void* arg) {
    (void)arg;
    if (!g_benchmarkRunning) return;

    timestamp_t now = esp_timer_get_time();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    g_metrics.isrCount++;

    if (xQueueSendFromISR(g_timestampQueue, &now, &xHigherPriorityTaskWoken) != pdTRUE) {
        g_metrics.droppedCount++;
    }

    portYIELD_FROM_ISR();
}

// ============================================================================
// IMU Task - Processes queue, reads sensor, updates LEDs, updates metrics
// ============================================================================

void imuTask(void* param) {
    (void)param;
    static uint16_t ledIndex = 0;
    static timestamp_t lastIsrTimestamp = 0;

    while (true) {
        timestamp_t isrTimestamp;

        if (xQueueReceive(g_timestampQueue, &isrTimestamp, portMAX_DELAY) == pdTRUE) {
            if (!g_benchmarkRunning) {
                lastIsrTimestamp = 0;  // Reset jitter tracking when not running
                continue;
            }

            // Track queue depth
            UBaseType_t queueDepth = uxQueueMessagesWaiting(g_timestampQueue);
            if (queueDepth > g_metrics.maxQueueDepth)
                g_metrics.maxQueueDepth = queueDepth;

            // --- IMU Read ---
            timestamp_t readStart = esp_timer_get_time();
            ImuRawData data;
            bool success = readAllSensorData(data);
            timestamp_t readEnd = esp_timer_get_time();

            if (success) {
                uint32_t readUs = (uint32_t)(readEnd - readStart);
                uint32_t latencyUs = (uint32_t)(readStart - isrTimestamp);

                // --- LED Update (like production would do) ---
                g_ledStrip.ClearTo(RgbColor(0, 0, 0));
                g_ledStrip.SetPixelColor(ledIndex, RgbColor(8, 8, 8));
                ledIndex = (ledIndex + 1) % NUM_LEDS;

                timestamp_t ledStart = esp_timer_get_time();
                g_ledStrip.Show();
                while (!g_ledStrip.CanShow()) {}  // Wait for DMA
                timestamp_t ledEnd = esp_timer_get_time();

                uint32_t ledUs = (uint32_t)(ledEnd - ledStart);

                g_metrics.sampleCount++;

                // Read timing
                if (readUs < g_metrics.minReadUs || g_metrics.minReadUs == 0)
                    g_metrics.minReadUs = readUs;
                if (readUs > g_metrics.maxReadUs)
                    g_metrics.maxReadUs = readUs;
                g_metrics.sumReadUs += readUs;

                // Latency timing
                if (latencyUs < g_metrics.minLatencyUs || g_metrics.minLatencyUs == 0)
                    g_metrics.minLatencyUs = latencyUs;
                if (latencyUs > g_metrics.maxLatencyUs)
                    g_metrics.maxLatencyUs = latencyUs;
                g_metrics.sumLatencyUs += latencyUs;

                // LED timing
                if (ledUs < g_metrics.minLedUs || g_metrics.minLedUs == 0)
                    g_metrics.minLedUs = ledUs;
                if (ledUs > g_metrics.maxLedUs)
                    g_metrics.maxLedUs = ledUs;
                g_metrics.sumLedUs += ledUs;
                g_metrics.ledHist.record(ledUs);

                // Record to histograms for percentile calculation
                g_metrics.readHist.record(readUs);
                uint32_t totalUs = readUs + ledUs;
                g_metrics.totalHist.record(totalUs);

                // Inter-sample interval (jitter)
                if (lastIsrTimestamp > 0) {
                    uint32_t intervalUs = (uint32_t)(isrTimestamp - lastIsrTimestamp);
                    if (intervalUs < g_metrics.minIntervalUs || g_metrics.minIntervalUs == 0)
                        g_metrics.minIntervalUs = intervalUs;
                    if (intervalUs > g_metrics.maxIntervalUs)
                        g_metrics.maxIntervalUs = intervalUs;
                    g_metrics.sumIntervalUs += intervalUs;
                    g_metrics.intervalCount++;
                }
                lastIsrTimestamp = isrTimestamp;
            }

            // Periodic yield to prevent watchdog timeout
            static uint32_t yieldCounter = 0;
            if (++yieldCounter >= 100) {
                yieldCounter = 0;
                vTaskDelay(1);
            }
        }
    }
}

// ============================================================================
// Interface Initialization
// ============================================================================

void cleanupInterface() {
    // Disable interrupt
    gpio_num_t intPin = static_cast<gpio_num_t>(IMU_INT_PIN);
    gpio_isr_handler_remove(intPin);

    // Delete old MPU instance
    if (g_mpu) {
        delete g_mpu;
        g_mpu = nullptr;
    }

    // End any active bus
    Wire.end();
    SPI_IMU.end();

    delay(10);  // Let things settle
}

bool initImuInterface(ImuInterface iface) {
    cleanupInterface();

    switch (iface) {
        case ImuInterface::I2C_400KHZ:
            // Configure pins for I2C mode
            pinMode(IMU_NCS_PIN, OUTPUT);
            digitalWrite(IMU_NCS_PIN, HIGH);  // HIGH = I2C mode
            pinMode(IMU_ADO_PIN, OUTPUT);
            digitalWrite(IMU_ADO_PIN, LOW);   // LOW = address 0x68

            Wire.begin(IMU_SDA_PIN, IMU_SCL_PIN);
            Wire.setClock(400000);

            g_mpu = new MPU9250_WE(MPU9250_ADDR);
            break;

        case ImuInterface::SPI_1MHZ:
            SPI_IMU.begin(IMU_SCL_PIN, IMU_ADO_PIN, IMU_SDA_PIN, IMU_NCS_PIN);
            g_mpu = new MPU9250_WE(&SPI_IMU, IMU_NCS_PIN, IMU_SDA_PIN, IMU_ADO_PIN, IMU_SCL_PIN, true, true);
            g_mpu->setSPIClockSpeed(1000000);
            g_spiClockSpeed = 1000000;
            break;

        case ImuInterface::SPI_20MHZ:
            SPI_IMU.begin(IMU_SCL_PIN, IMU_ADO_PIN, IMU_SDA_PIN, IMU_NCS_PIN);
            g_mpu = new MPU9250_WE(&SPI_IMU, IMU_NCS_PIN, IMU_SDA_PIN, IMU_ADO_PIN, IMU_SCL_PIN, true, true);
            g_mpu->setSPIClockSpeed(20000000);
            g_spiClockSpeed = 20000000;
            break;
    }

    g_currentInterface = iface;

    if (!g_mpu->init()) {
        Serial.printf("  ERROR: MPU-9250 init failed for %s\n", getInterfaceName(iface));
        return false;
    }

    // Configure ranges
    g_mpu->setAccRange(MPU9250_ACC_RANGE_16G);
    g_mpu->setGyrRange(MPU9250_GYRO_RANGE_2000);

    // Enable DATA_READY interrupt
    g_mpu->enableInterrupt(MPU9250_DATA_READY);

    // Re-enable interrupt handler
    gpio_num_t intPin = static_cast<gpio_num_t>(IMU_INT_PIN);
    gpio_isr_handler_add(intPin, imuIsrWrapper, nullptr);

    return true;
}

// ============================================================================
// Preset Configuration
// ============================================================================

void applyPreset(uint8_t presetIndex) {
    if (presetIndex >= NUM_PRESETS || !g_mpu) return;

    const TestPreset& p = PRESETS[presetIndex];

    // Disable interrupt during config
    gpio_num_t intPin = static_cast<gpio_num_t>(IMU_INT_PIN);
    gpio_isr_handler_remove(intPin);

    // Flush queue
    xQueueReset(g_timestampQueue);

    // Apply DLPF settings
    if (p.dlpf == 7) {
        g_mpu->enableAccDLPF(false);
        g_mpu->setGyrDLPF(MPU9250_DLPF_7);
    } else {
        g_mpu->enableAccDLPF(true);
        g_mpu->setAccDLPF(static_cast<MPU9250_dlpf>(p.dlpf));
        g_mpu->setGyrDLPF(static_cast<MPU9250_dlpf>(p.dlpf));
    }
    g_mpu->setSampleRateDivider(p.divider);

    // Re-enable interrupt
    gpio_isr_handler_add(intPin, imuIsrWrapper, nullptr);
}

// ============================================================================
// Metrics Reset
// ============================================================================

void resetMetrics() {
    g_metrics.isrCount = 0;
    g_metrics.sampleCount = 0;
    g_metrics.droppedCount = 0;
    g_metrics.minReadUs = 0;
    g_metrics.maxReadUs = 0;
    g_metrics.sumReadUs = 0;
    g_metrics.minLatencyUs = 0;
    g_metrics.maxLatencyUs = 0;
    g_metrics.sumLatencyUs = 0;
    g_metrics.minLedUs = 0;
    g_metrics.maxLedUs = 0;
    g_metrics.sumLedUs = 0;
    g_metrics.minIntervalUs = 0;
    g_metrics.maxIntervalUs = 0;
    g_metrics.sumIntervalUs = 0;
    g_metrics.intervalCount = 0;
    g_metrics.maxQueueDepth = 0;

    // Reset histograms
    g_metrics.readHist.reset();
    g_metrics.ledHist.reset();
    g_metrics.totalHist.reset();
}

// ============================================================================
// Single Preset Benchmark
// ============================================================================

BenchmarkResult runPresetBenchmark(ImuInterface iface, uint8_t presetIndex) {
    const TestPreset& p = PRESETS[presetIndex];

    Serial.printf("  Preset %d: %s (DLPF=%d, DIV=%d, target=%luHz)\n",
                  presetIndex + 1, p.name, p.dlpf, p.divider, p.targetHz);

    // Apply preset and reset metrics
    applyPreset(presetIndex);
    resetMetrics();

    // Run benchmark
    g_benchmarkRunning = true;
    uint32_t startTime = millis();

    while (millis() - startTime < BENCHMARK_DURATION_MS) {
        delay(100);  // Let benchmark run
    }

    g_benchmarkRunning = false;
    uint32_t actualDuration = millis() - startTime;
    uint32_t sampleCount = g_metrics.sampleCount;

    // Collect results
    BenchmarkResult result;
    result.interface = iface;
    result.presetIndex = presetIndex;
    result.durationMs = actualDuration;

    result.isrCount = g_metrics.isrCount;
    result.sampleCount = sampleCount;
    result.dropCount = g_metrics.droppedCount;

    // IMU read timing + percentiles
    result.avgReadUs = (sampleCount > 0) ? (uint32_t)(g_metrics.sumReadUs / sampleCount) : 0;
    result.p50ReadUs = g_metrics.readHist.percentile(50, sampleCount);
    result.p95ReadUs = g_metrics.readHist.percentile(95, sampleCount);
    result.p99ReadUs = g_metrics.readHist.percentile(99, sampleCount);
    result.maxReadUs = g_metrics.maxReadUs;

    // LED timing + percentiles
    result.avgLedUs = (sampleCount > 0) ? (uint32_t)(g_metrics.sumLedUs / sampleCount) : 0;
    result.p50LedUs = g_metrics.ledHist.percentile(50, sampleCount);
    result.p95LedUs = g_metrics.ledHist.percentile(95, sampleCount);
    result.p99LedUs = g_metrics.ledHist.percentile(99, sampleCount);
    result.maxLedUs = g_metrics.maxLedUs;

    // Total loop timing + percentiles
    result.avgTotalUs = result.avgReadUs + result.avgLedUs;
    result.p50TotalUs = g_metrics.totalHist.percentile(50, sampleCount);
    result.p95TotalUs = g_metrics.totalHist.percentile(95, sampleCount);
    result.p99TotalUs = g_metrics.totalHist.percentile(99, sampleCount);
    result.maxTotalUs = g_metrics.maxReadUs + g_metrics.maxLedUs;  // Worst case

    // Jitter
    result.avgIntervalUs = (g_metrics.intervalCount > 0)
        ? (uint32_t)(g_metrics.sumIntervalUs / g_metrics.intervalCount) : 0;
    result.minIntervalUs = g_metrics.minIntervalUs;
    result.maxIntervalUs = g_metrics.maxIntervalUs;

    // Queue
    result.maxQueueDepth = g_metrics.maxQueueDepth;

    // Rates
    float durationSec = actualDuration / 1000.0f;
    result.isrRate = result.isrCount / durationSec;
    result.sampleRate = result.sampleCount / durationSec;
    result.dropPct = (result.isrCount > 0)
        ? (result.dropCount * 100.0f / result.isrCount) : 0.0f;

    // Print summary
    Serial.printf("    Rate: %.0f/s | Drops: %.1f%% | Queue max: %lu\n",
                  result.sampleRate, result.dropPct, result.maxQueueDepth);
    Serial.printf("    IMU:   avg=%lu p50=%lu p95=%lu p99=%lu max=%lu us\n",
                  result.avgReadUs, result.p50ReadUs, result.p95ReadUs,
                  result.p99ReadUs, result.maxReadUs);
    Serial.printf("    LED:   avg=%lu p50=%lu p95=%lu p99=%lu max=%lu us\n",
                  result.avgLedUs, result.p50LedUs, result.p95LedUs,
                  result.p99LedUs, result.maxLedUs);
    Serial.printf("    Total: avg=%lu p50=%lu p95=%lu p99=%lu us\n",
                  result.avgTotalUs, result.p50TotalUs, result.p95TotalUs, result.p99TotalUs);
    Serial.printf("    Jitter: avg=%lu min=%lu max=%lu us\n",
                  result.avgIntervalUs, result.minIntervalUs, result.maxIntervalUs);
    Serial.println();

    return result;
}

// ============================================================================
// Summary Output (brief, not redundant with per-line output)
// ============================================================================

void printSummary() {
    Serial.println();
    Serial.println("================================================================================");
    Serial.println("SUMMARY BY INTERFACE:");
    Serial.println();

    ImuInterface interfaces[] = { ImuInterface::I2C_400KHZ, ImuInterface::SPI_1MHZ, ImuInterface::SPI_20MHZ };
    for (auto iface : interfaces) {
        uint32_t avgRead = 0, avgLed = 0, avgTotal = 0;
        uint32_t maxP99Total = 0;
        uint32_t count = 0;
        float maxRate = 0;

        for (const auto& r : g_results) {
            if (r.interface == iface && r.avgReadUs > 0) {
                avgRead += r.avgReadUs;
                avgLed += r.avgLedUs;
                avgTotal += r.avgTotalUs;
                if (r.p99TotalUs > maxP99Total) maxP99Total = r.p99TotalUs;
                count++;
                if (r.sampleRate > maxRate) maxRate = r.sampleRate;
            }
        }

        if (count > 0) {
            avgRead /= count;
            avgLed /= count;
            avgTotal /= count;
            uint32_t avgLimit = (avgTotal > 0) ? (1000000 / avgTotal) : 0;
            uint32_t p99Limit = (maxP99Total > 0) ? (1000000 / maxP99Total) : 0;
            Serial.printf("%s:\n", getInterfaceName(iface));
            Serial.printf("  IMU %luus + LED %luus = %luus avg total\n", avgRead, avgLed, avgTotal);
            Serial.printf("  Max sustained: %.0f/s | Theoretical avg limit: %lu/s | p99 limit: %lu/s\n",
                          maxRate, avgLimit, p99Limit);
        }
    }

    Serial.println();
    Serial.println("NOTE: DLPF=0/7 ignores SMPLRT_DIV (always 8kHz base rate).");
    Serial.println("================================================================================");
}

// ============================================================================
// Main Benchmark Runner
// ============================================================================

void runBenchmark() {
    Serial.println("\n");
    Serial.println("================================================================================");
    Serial.println("                        IMU SAMPLING BENCHMARK");
    Serial.println("================================================================================");
    Serial.println();
    Serial.printf("Duration per preset: %lu ms\n", BENCHMARK_DURATION_MS);
    Serial.printf("Presets: %zu\n", NUM_PRESETS);
    Serial.println("Interfaces: I2C 400kHz, SPI 1MHz, SPI 20MHz");
    Serial.println();

    g_results.clear();

    ImuInterface interfaces[] = { ImuInterface::I2C_400KHZ, ImuInterface::SPI_1MHZ, ImuInterface::SPI_20MHZ };

    for (auto iface : interfaces) {
        Serial.println();
        Serial.printf("--- Testing %s ---\n\n", getInterfaceName(iface));

        if (!initImuInterface(iface)) {
            Serial.printf("  SKIPPING %s (init failed)\n", getInterfaceName(iface));
            continue;
        }

        delay(100);  // Let interface stabilize

        for (size_t p = 0; p < NUM_PRESETS; p++) {
            BenchmarkResult result = runPresetBenchmark(iface, p);
            g_results.push_back(result);
        }
    }

    printSummary();

    Serial.println("\nBenchmark complete. Press any key to run again...\n");
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
    Serial.println(prompt);
    while (!Serial.available()) {
        delay(10);
    }
    while (Serial.available()) Serial.read();
}

// ============================================================================
// Arduino Entry Points
// ============================================================================

void setup() {
    Serial.begin(115200);
    waitForSerial();

    Serial.println("\n\n");
    Serial.println("=== MPU-9250 + LED Sampling Benchmark ===");
    Serial.println();

    // Initialize LED strip FIRST on SPI2/FSPI at 40MHz
    Serial.printf("LED Strip: %d LEDs on DATA=%d, CLK=%d (SPI2 40MHz)\n", NUM_LEDS, LED_DATA_PIN, LED_CLK_PIN);
    g_ledStrip.Begin(LED_CLK_PIN, -1, LED_DATA_PIN, -1);

    // Quick LED test
    Serial.println("Testing LEDs...");
    g_ledStrip.SetPixelColor(0, RgbColor(32, 0, 0));
    g_ledStrip.Show();
    while (!g_ledStrip.CanShow()) {}
    delay(300);
    g_ledStrip.ClearTo(RgbColor(0, 0, 0));
    g_ledStrip.Show();
    while (!g_ledStrip.CanShow()) {}
    Serial.println("LED strip OK");
    Serial.println();

    // Create timestamp queue
    g_timestampQueue = xQueueCreate(QUEUE_SIZE, sizeof(timestamp_t));
    if (!g_timestampQueue) {
        Serial.println("ERROR: Failed to create queue!");
        while (true) delay(1000);
    }

    // Configure INT pin
    gpio_num_t intPin = static_cast<gpio_num_t>(IMU_INT_PIN);
    gpio_set_direction(intPin, GPIO_MODE_INPUT);
    gpio_set_intr_type(intPin, GPIO_INTR_POSEDGE);
    gpio_install_isr_service(0);

    // Create IMU task pinned to Core 0
    xTaskCreatePinnedToCore(imuTask, "IMU", 4096, nullptr,
                            tskIDLE_PRIORITY + 2, &g_imuTaskHandle, 0);
    Serial.println("IMU task created on Core 0");
    Serial.println();

    // Quick test to verify we can talk to IMU
    Serial.println("Testing IMU connection (I2C mode)...");
    if (!initImuInterface(ImuInterface::I2C_400KHZ)) {
        Serial.println("ERROR: Cannot communicate with MPU-9250!");
        Serial.println("Check wiring and connections.");
        while (true) delay(1000);
    }
    Serial.println("MPU-9250 detected!");
    Serial.println();

    waitForKeypress("Press any key to start benchmark...");
}

void loop() {
    runBenchmark();

    // Wait for keypress to run again
    while (!Serial.available()) {
        delay(100);
    }
    while (Serial.available()) Serial.read();
}
