#include "TelemetryTask.h"
#include "Imu.h"
#include "ESPNowComm.h"
#include "messages.h"
#include "types.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Task configuration
static constexpr size_t TELEMETRY_TASK_STACK = 4096;
static constexpr UBaseType_t TELEMETRY_TASK_PRIORITY = 2;  // Below hallProcessingTask (3)
static constexpr BaseType_t TELEMETRY_TASK_CORE = 0;       // Same as WiFi stack

// Flush timeout: send partial batch after this many microseconds
// 500ms is conservative - at 8kHz fills 50-sample batch in ~6ms
static constexpr uint64_t BATCH_FLUSH_TIMEOUT_US = 500000;

// Task state
static TaskHandle_t s_taskHandle = nullptr;
static volatile bool s_enabled = false;

// Batch buffer and state
static AccelSampleMsg s_msg;
static sequence_t s_sequenceNum = 0;
static timestamp_t s_lastSendTime = 0;

/**
 * Add a sample to the current batch (accel + gyro)
 */
static void addSampleToBatch(timestamp_t timestamp,
                              int16_t x, int16_t y, int16_t z,
                              int16_t gx, int16_t gy, int16_t gz) {
    uint8_t idx = s_msg.sample_count;

    if (idx == 0) {
        // First sample in batch - set base timestamp and sequence
        s_msg.base_timestamp = timestamp;
        s_msg.start_sequence = s_sequenceNum;
        s_msg.samples[idx].delta_us = 0;
    } else {
        // Subsequent samples - compute delta from base
        uint64_t delta = timestamp - s_msg.base_timestamp;
        // Clamp to 16-bit (max 65535 us = ~65ms, batch spans ~6ms at 8kHz)
        s_msg.samples[idx].delta_us = (delta > 65535) ? 65535 : static_cast<uint16_t>(delta);
    }

    // Accelerometer
    s_msg.samples[idx].x = x;
    s_msg.samples[idx].y = y;
    s_msg.samples[idx].z = z;

    // Gyroscope
    s_msg.samples[idx].gx = gx;
    s_msg.samples[idx].gy = gy;
    s_msg.samples[idx].gz = gz;

    s_msg.sample_count++;
    s_sequenceNum++;
}

/**
 * Send the current batch via ESP-NOW and reset
 */
static void sendBatch() {
    if (s_msg.sample_count == 0) return;
    sendAccelSamples(s_msg);
    s_msg.sample_count = 0;
    s_lastSendTime = esp_timer_get_time();
}

/**
 * FreeRTOS task function
 */
static void telemetryTaskFunc(void* pvParameters) {
    (void)pvParameters;

    while (true) {
        // Wait for enable signal
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        Serial.println("[TELEM] Telemetry task started");

        // Reset batch state
        s_msg.sample_count = 0;
        s_sequenceNum = 0;
        s_lastSendTime = esp_timer_get_time();

        // Processing loop - runs while enabled
        while (s_enabled) {
            // Wait for DATA_READY signal from ISR (10ms timeout)
            if (imu.waitForSample(pdMS_TO_TICKS(10))) {
                // Fast burst read via SPI (~37µs)
                int16_t x, y, z, gx, gy, gz;
                if (imu.readRaw(x, y, z, gx, gy, gz)) {
                    // Timestamp at read completion - this is when we know the value
                    timestamp_t sampleTimestamp = esp_timer_get_time();

                    addSampleToBatch(sampleTimestamp, x, y, z, gx, gy, gz);

                    // Send batch when full
                    if (s_msg.sample_count >= ACCEL_SAMPLES_MAX_BATCH) {
                        sendBatch();
                    }
                }
            }

            // Time-based flush: send partial batch if timeout exceeded
            if (s_msg.sample_count > 0) {
                timestamp_t now = esp_timer_get_time();
                if ((now - s_lastSendTime) >= BATCH_FLUSH_TIMEOUT_US) {
                    sendBatch();
                }
            }
        }

        // Flush remaining samples on stop
        if (s_msg.sample_count > 0) {
            sendBatch();
        }

        Serial.printf("[TELEM] Telemetry task stopped (%u samples sent)\n", s_sequenceNum);
    }
}

void telemetryTaskInit() {
    // Initialize message type
    s_msg.type = MSG_ACCEL_SAMPLES;
    s_msg.sample_count = 0;

    BaseType_t result = xTaskCreatePinnedToCore(
        telemetryTaskFunc,
        "telemetryTask",
        TELEMETRY_TASK_STACK,
        nullptr,
        TELEMETRY_TASK_PRIORITY,
        &s_taskHandle,
        TELEMETRY_TASK_CORE
    );

    if (result != pdPASS) {
        Serial.println("[TELEM] Failed to create telemetry task!");
        return;
    }

    Serial.println("[TELEM] Telemetry task initialized (waiting for start)");
}

void telemetryTaskStart() {
    if (!s_taskHandle) {
        Serial.println("[TELEM] Task not initialized!");
        return;
    }

    s_enabled = true;
    xTaskNotifyGive(s_taskHandle);
}

void telemetryTaskStop() {
    s_enabled = false;
    // Task will flush and return to waiting state on next queue timeout
}
