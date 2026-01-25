#include "telemetry_capture.h"
#include "messages.h"
#include "motor_speed.h"
#include <Arduino.h>
#include <esp_partition.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <cstring>

// Maximum payload size for captured messages - use ESP-NOW v2.0 max
static const size_t MAX_CAPTURE_PAYLOAD = ESP_NOW_MAX_DATA_LEN_V2;

// Timeouts (ms)
static const uint32_t TASK_STOP_TIMEOUT_MS = 5000;    // Max wait for task to stop
static const uint32_t TASK_STOP_POLL_MS = 10;         // Poll interval when waiting
static const uint32_t QUEUE_RECEIVE_TIMEOUT_MS = 100; // Task queue poll interval
static const uint32_t QUEUE_SEND_TIMEOUT_MS = 100;    // Blocking queue send timeout
static const int STOP_SEND_MAX_RETRIES = 10;          // Retries for sending STOP to queue

// State
static CaptureState s_state = CaptureState::IDLE;

// FreeRTOS queue for receiving messages from ESP-NOW callback
static QueueHandle_t s_captureQueue = nullptr;
static TaskHandle_t s_captureTask = nullptr;

// Task acknowledgment flag - set when task has stopped and flushed
static volatile bool s_taskStopped = true;  // Start stopped

// Dump-in-progress flag - suppresses debug serial output during DUMP
static volatile bool s_dumpInProgress = false;

// Queue full counter for diagnostics
static volatile uint32_t s_queueFullCount = 0;

// Message types for capture queue
enum class CaptureMessageType : uint8_t {
    DATA,   // Telemetry data to write
    STOP    // Flush and acknowledge
};

// Message wrapper for queue (commands + raw ESP-NOW payload)
struct CaptureMessage {
    CaptureMessageType type;
    uint8_t msgType;    // For DATA: which partition (MSG_ACCEL_SAMPLES, etc.)
    uint16_t len;       // ESP-NOW v2.0 can be up to 1470 bytes
    uint8_t data[MAX_CAPTURE_PAYLOAD];
} __attribute__((packed));

static const size_t CAPTURE_QUEUE_SIZE = 32;  // ~0.8 sec buffer at 40 batches/sec

// =============================================================================
// Partition Writer State
// =============================================================================

struct PartitionWriter {
    const esp_partition_t* partition;
    size_t write_offset;      // Next write position (starts after header)
    uint64_t base_timestamp;  // Set on first sample
    uint32_t start_sequence;  // First sample sequence
    uint32_t sample_count;    // Total samples written

    uint8_t buffer[FLASH_SECTOR_SIZE];  // Sector buffer
    size_t buffer_used;

    // Initialize writer for a partition
    void init(const esp_partition_t* part) {
        partition = part;
        reset();
    }

    void reset() {
        write_offset = sizeof(TelemetryHeader);  // Skip header
        base_timestamp = 0;
        start_sequence = 0;
        sample_count = 0;
        buffer_used = 0;
    }

    // Check if partition has room for more data
    bool hasSpace(size_t needed) const {
        if (!partition) return false;
        return (write_offset + buffer_used + needed) <= partition->size;
    }

    // Flush buffer to flash (when sector full or on stop)
    bool flush() {
        if (!partition || buffer_used == 0) return true;

        // Pad to 4-byte alignment (flash write requirement)
        size_t aligned = (buffer_used + 3) & ~3;
        if (aligned > buffer_used) {
            memset(buffer + buffer_used, 0xFF, aligned - buffer_used);
        }

        esp_err_t err = esp_partition_write(partition, write_offset, buffer, aligned);
        if (err != ESP_OK) {
            Serial.printf("[CAPTURE] Flash write failed: %s\n", esp_err_to_name(err));
            return false;
        }

        write_offset += aligned;
        buffer_used = 0;
        return true;
    }

    // Write header to partition start
    bool writeHeader() {
        if (!partition) return false;

        TelemetryHeader header = {};
        header.magic = TELEMETRY_MAGIC;
        header.version = TELEMETRY_VERSION;
        header.base_timestamp = base_timestamp;
        header.start_sequence = start_sequence;
        header.sample_count = sample_count;

        esp_err_t err = esp_partition_write(partition, 0, &header, sizeof(header));
        if (err != ESP_OK) {
            Serial.printf("[CAPTURE] Header write failed: %s\n", esp_err_to_name(err));
            return false;
        }
        return true;
    }
};

static PartitionWriter s_accel;
static PartitionWriter s_hall;
static PartitionWriter s_stats;

// =============================================================================
// Partition Lookup
// =============================================================================

static const esp_partition_t* findPartition(uint8_t subtype) {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    static_cast<esp_partition_subtype_t>(subtype),
                                    nullptr);
}

// Get human-readable name for message type
static const char* getMsgTypeName(uint8_t msgType) {
    switch (msgType) {
        case MSG_ACCEL_SAMPLES: return "MSG_ACCEL_SAMPLES";
        case MSG_HALL_EVENT: return "MSG_HALL_EVENT";
        case MSG_ROTOR_STATS: return "MSG_ROTOR_STATS";
        default: return "UNKNOWN";
    }
}

// Get partition name for message type
static const char* getPartitionName(uint8_t msgType) {
    switch (msgType) {
        case MSG_ACCEL_SAMPLES: return "accel";
        case MSG_HALL_EVENT: return "hall";
        case MSG_ROTOR_STATS: return "stats";
        default: return "unknown";
    }
}

// =============================================================================
// Erase Partitions
// =============================================================================

static bool eraseAllPartitions() {
    bool success = true;

    if (s_accel.partition) {
        esp_err_t err = esp_partition_erase_range(s_accel.partition, 0, s_accel.partition->size);
        if (err != ESP_OK) {
            Serial.printf("[CAPTURE] Erase accel failed: %s\n", esp_err_to_name(err));
            success = false;
        }
        s_accel.reset();
    }

    if (s_hall.partition) {
        esp_err_t err = esp_partition_erase_range(s_hall.partition, 0, s_hall.partition->size);
        if (err != ESP_OK) {
            Serial.printf("[CAPTURE] Erase hall failed: %s\n", esp_err_to_name(err));
            success = false;
        }
        s_hall.reset();
    }

    if (s_stats.partition) {
        esp_err_t err = esp_partition_erase_range(s_stats.partition, 0, s_stats.partition->size);
        if (err != ESP_OK) {
            Serial.printf("[CAPTURE] Erase stats failed: %s\n", esp_err_to_name(err));
            success = false;
        }
        s_stats.reset();
    }

    return success;
}

// =============================================================================
// Write Records to Partition Buffer
// =============================================================================

static bool writeAccelSample(const AccelSampleRaw& sample) {
    if (!s_accel.hasSpace(sizeof(AccelSampleRaw))) {
        s_state = CaptureState::FULL;
        return false;
    }

    // Copy to buffer
    memcpy(s_accel.buffer + s_accel.buffer_used, &sample, sizeof(sample));
    s_accel.buffer_used += sizeof(sample);
    s_accel.sample_count++;

    // Flush when buffer full (256 samples = 4KB)
    if (s_accel.buffer_used >= FLASH_SECTOR_SIZE) {
        return s_accel.flush();
    }
    return true;
}

static bool writeHallRecord(const HallRecordRaw& record) {
    if (!s_hall.hasSpace(sizeof(HallRecordRaw))) {
        s_state = CaptureState::FULL;
        return false;
    }

    memcpy(s_hall.buffer + s_hall.buffer_used, &record, sizeof(record));
    s_hall.buffer_used += sizeof(record);
    s_hall.sample_count++;

    // Flush when buffer approaches sector size
    if (s_hall.buffer_used + sizeof(HallRecordRaw) > FLASH_SECTOR_SIZE) {
        return s_hall.flush();
    }
    return true;
}

static bool writeStatsRecord(const RotorStatsRecord& record) {
    if (!s_stats.hasSpace(sizeof(RotorStatsRecord))) {
        s_state = CaptureState::FULL;
        return false;
    }

    memcpy(s_stats.buffer + s_stats.buffer_used, &record, sizeof(record));
    s_stats.buffer_used += sizeof(record);
    s_stats.sample_count++;

    // Flush when buffer approaches sector size
    if (s_stats.buffer_used + sizeof(RotorStatsRecord) > FLASH_SECTOR_SIZE) {
        return s_stats.flush();
    }
    return true;
}

// =============================================================================
// Process Messages from Queue
// =============================================================================

static void processMessage(uint8_t msgType, const uint8_t* data, size_t len) {
    switch (msgType) {
        case MSG_ACCEL_SAMPLES: {
            // Validate minimum size: header (type + count + base_timestamp + start_sequence)
            if (len < ACCEL_MSG_HEADER_SIZE) return;
            const AccelSampleMsg* msg = reinterpret_cast<const AccelSampleMsg*>(data);

            // Validate sample_count bounds
            if (msg->sample_count == 0 || msg->sample_count > ACCEL_SAMPLES_MAX_BATCH) return;

            // Validate length matches sample_count
            size_t expected = ACCEL_MSG_HEADER_SIZE + (msg->sample_count * sizeof(AccelSampleWire));
            if (len < expected) return;

            // Set base timestamp on first batch
            if (s_accel.base_timestamp == 0) {
                s_accel.base_timestamp = msg->base_timestamp;
                s_accel.start_sequence = msg->start_sequence;
            }

            // Convert each sample to raw format with delta from base
            for (uint8_t i = 0; i < msg->sample_count; i++) {
                const AccelSampleWire& s = msg->samples[i];

                AccelSampleRaw raw;
                // Compute absolute timestamp, then delta from base
                uint64_t abs_time = msg->base_timestamp + s.delta_us;
                raw.delta_us = static_cast<uint32_t>(abs_time - s_accel.base_timestamp);
                raw.x = s.x;
                raw.y = s.y;
                raw.z = s.z;
                raw.gx = s.gx;
                raw.gy = s.gy;
                raw.gz = s.gz;

                if (!writeAccelSample(raw)) return;
            }
            break;
        }

        case MSG_HALL_EVENT: {
            if (len < sizeof(HallEventMsg)) return;
            const HallEventMsg* msg = reinterpret_cast<const HallEventMsg*>(data);

            // Set base timestamp on first event
            if (s_hall.base_timestamp == 0) {
                s_hall.base_timestamp = msg->timestamp_us;
            }

            HallRecordRaw raw;
            raw.delta_us = static_cast<uint32_t>(msg->timestamp_us - s_hall.base_timestamp);
            raw.period_us = msg->period_us;
            raw.rotation_num = msg->rotation_num;

            writeHallRecord(raw);
            break;
        }

        case MSG_ROTOR_STATS: {
            if (len < sizeof(RotorStatsMsg)) return;
            const RotorStatsMsg* msg = reinterpret_cast<const RotorStatsMsg*>(data);

            RotorStatsRecord rec;
            rec.reportSequence = msg->reportSequence;
            rec.created_us = msg->created_us;
            rec.lastUpdated_us = msg->lastUpdated_us;
            rec.hallEventsTotal = msg->hallEventsTotal;
            rec.hallOutliersFiltered = msg->hallOutliersFiltered;
            rec.lastOutlierInterval_us = msg->lastOutlierInterval_us;
            rec.hallAvg_us = msg->hallAvg_us;
            rec.espnowSendAttempts = msg->espnowSendAttempts;
            rec.espnowSendFailures = msg->espnowSendFailures;
            rec.renderCount = msg->renderCount;
            rec.skipCount = msg->skipCount;
            rec.notRotatingCount = msg->notRotatingCount;
            rec.effectNumber = msg->effectNumber;
            rec.brightness = msg->brightness;
            rec.speedPreset = static_cast<uint8_t>(getSpeedPreset());
            rec.pwmValue = getCurrentPWM();

            writeStatsRecord(rec);
            break;
        }

        default:
            // Unknown message type - ignore
            break;
    }
}

// =============================================================================
// Flush and Finalize
// =============================================================================

static void flushAllBuffers() {
    s_accel.flush();
    s_hall.flush();
    s_stats.flush();
}

static void writeAllHeaders() {
    s_accel.writeHeader();
    s_hall.writeHeader();
    s_stats.writeHeader();
}

// =============================================================================
// FreeRTOS Capture Task
// =============================================================================

// Send STOP command to capture queue with retry logic.
static bool sendStopToQueue() {
    if (!s_captureQueue) return false;

    CaptureMessage msg = {};
    msg.type = CaptureMessageType::STOP;

    // Check queue space first for diagnostics
    UBaseType_t spaces = uxQueueSpacesAvailable(s_captureQueue);
    if (spaces == 0) {
        Serial.printf("[CAPTURE] Queue full (%d items), waiting for space\n",
                      (int)uxQueueMessagesWaiting(s_captureQueue));
    }

    // Try to send STOP - retry if queue full
    for (int attempt = 0; attempt < STOP_SEND_MAX_RETRIES; attempt++) {
        if (xQueueSend(s_captureQueue, &msg, pdMS_TO_TICKS(QUEUE_SEND_TIMEOUT_MS)) == pdPASS) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    Serial.println("[CAPTURE] Failed to send STOP after retries");
    return false;
}

// Wait for capture task to acknowledge stop.
static bool waitForTaskStop() {
    uint32_t startMs = millis();
    while (!s_taskStopped && (millis() - startMs) < TASK_STOP_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(TASK_STOP_POLL_MS));
    }
    return s_taskStopped;
}

// FreeRTOS task that processes capture queue (runs on Core 1)
static void captureTask(void* pvParameters) {
    (void)pvParameters;
    // Static to avoid stack overflow - CaptureMessage is ~1475 bytes
    static CaptureMessage msg;

    while (true) {
        // Wait for wake-up notification (from captureStart)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        s_taskStopped = false;

        // Recording loop - process messages until STOP command
        while (true) {
            if (xQueueReceive(s_captureQueue, &msg, pdMS_TO_TICKS(QUEUE_RECEIVE_TIMEOUT_MS)) == pdPASS) {
                if (msg.type == CaptureMessageType::STOP) {
                    break;  // Exit recording loop
                }
                if (msg.type == CaptureMessageType::DATA) {
                    processMessage(msg.msgType, msg.data, msg.len);
                }
            }
        }

        // STOP received - flush buffers and write headers
        flushAllBuffers();
        writeAllHeaders();
        s_taskStopped = true;
        // Loop back to wait for next recording session
    }
}

// =============================================================================
// Dump Partitions as CSV
// =============================================================================

// Read header from partition, return sample_count (0 if invalid)
static uint32_t readPartitionHeader(const esp_partition_t* part, TelemetryHeader* header) {
    if (!part) return 0;

    esp_err_t err = esp_partition_read(part, 0, header, sizeof(TelemetryHeader));
    if (err != ESP_OK) return 0;

    if (header->magic != TELEMETRY_MAGIC || header->version != TELEMETRY_VERSION) {
        return 0;
    }
    return header->sample_count;
}

// Dump accel partition as CSV
static void dumpAccelCSV(bool scriptMode) {
    TelemetryHeader header;
    uint32_t count = readPartitionHeader(s_accel.partition, &header);
    if (count == 0) return;

    if (scriptMode) {
        Serial.println(">>> MSG_ACCEL_SAMPLES.bin");
    } else {
        Serial.printf("=== FILE: MSG_ACCEL_SAMPLES.bin (%lu records) ===\n", count);
    }
    Serial.println("timestamp_us,sequence_num,x,y,z,gx,gy,gz");

    // Read in chunks
    static AccelSampleRaw samples[256];  // One sector worth
    size_t offset = sizeof(TelemetryHeader);
    uint32_t seq = header.start_sequence;

    for (uint32_t remaining = count; remaining > 0; ) {
        uint32_t batch = (remaining > 256) ? 256 : remaining;
        size_t bytes = batch * sizeof(AccelSampleRaw);

        esp_err_t err = esp_partition_read(s_accel.partition, offset, samples, bytes);
        if (err != ESP_OK) {
            Serial.printf("[CAPTURE] Read error at offset %zu: %s\n", offset, esp_err_to_name(err));
            break;
        }

        for (uint32_t i = 0; i < batch; i++) {
            const AccelSampleRaw& s = samples[i];
            uint64_t timestamp = header.base_timestamp + s.delta_us;
            Serial.printf("%llu,%u,%d,%d,%d,%d,%d,%d\n",
                          timestamp, seq++,
                          s.x, s.y, s.z, s.gx, s.gy, s.gz);
        }

        offset += bytes;
        remaining -= batch;
    }
}

// Dump hall partition as CSV
static void dumpHallCSV(bool scriptMode) {
    TelemetryHeader header;
    uint32_t count = readPartitionHeader(s_hall.partition, &header);
    if (count == 0) return;

    if (scriptMode) {
        Serial.println(">>> MSG_HALL_EVENT.bin");
    } else {
        Serial.printf("=== FILE: MSG_HALL_EVENT.bin (%lu records) ===\n", count);
    }
    Serial.println("timestamp_us,period_us,rotation_num");

    static HallRecordRaw records[341];  // ~4KB worth (341 * 12 = 4092)
    size_t offset = sizeof(TelemetryHeader);

    for (uint32_t remaining = count; remaining > 0; ) {
        uint32_t batch = (remaining > 341) ? 341 : remaining;
        size_t bytes = batch * sizeof(HallRecordRaw);

        esp_err_t err = esp_partition_read(s_hall.partition, offset, records, bytes);
        if (err != ESP_OK) {
            Serial.printf("[CAPTURE] Read error: %s\n", esp_err_to_name(err));
            break;
        }

        for (uint32_t i = 0; i < batch; i++) {
            const HallRecordRaw& r = records[i];
            uint64_t timestamp = header.base_timestamp + r.delta_us;
            Serial.printf("%llu,%u,%u\n", timestamp, r.period_us, r.rotation_num);
        }

        offset += bytes;
        remaining -= batch;
    }
}

// Dump stats partition as CSV
static void dumpStatsCSV(bool scriptMode) {
    TelemetryHeader header;
    uint32_t count = readPartitionHeader(s_stats.partition, &header);
    if (count == 0) return;

    if (scriptMode) {
        Serial.println(">>> MSG_ROTOR_STATS.bin");
    } else {
        Serial.printf("=== FILE: MSG_ROTOR_STATS.bin (%lu records) ===\n", count);
    }
    Serial.println("seq,created_us,updated_us,hall_total,outliers,last_outlier_us,"
                   "hall_avg_us,espnow_ok,espnow_fail,render,skip,not_rot,effect,brightness,"
                   "speed_preset,pwm");

    static RotorStatsRecord records[78];  // ~4KB worth (78 * 52 = 4056)
    size_t offset = sizeof(TelemetryHeader);

    for (uint32_t remaining = count; remaining > 0; ) {
        uint32_t batch = (remaining > 78) ? 78 : remaining;
        size_t bytes = batch * sizeof(RotorStatsRecord);

        esp_err_t err = esp_partition_read(s_stats.partition, offset, records, bytes);
        if (err != ESP_OK) {
            Serial.printf("[CAPTURE] Read error: %s\n", esp_err_to_name(err));
            break;
        }

        for (uint32_t i = 0; i < batch; i++) {
            const RotorStatsRecord& r = records[i];
            Serial.printf("%u,%llu,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                          r.reportSequence, r.created_us, r.lastUpdated_us,
                          r.hallEventsTotal, r.hallOutliersFiltered, r.lastOutlierInterval_us,
                          r.hallAvg_us, r.espnowSendAttempts - r.espnowSendFailures, r.espnowSendFailures,
                          r.renderCount, r.skipCount, r.notRotatingCount,
                          r.effectNumber, r.brightness, r.speedPreset, r.pwmValue);
        }

        offset += bytes;
        remaining -= batch;
    }
}

// Wait for any serial input (for interactive play)
static void waitForKeypress() {
    Serial.println("\nPress any key to continue...");
    Serial.flush();

    while (Serial.available()) Serial.read();
    while (!Serial.available()) delay(10);
    while (Serial.available()) Serial.read();
    Serial.println();
}

// =============================================================================
// Public API
// =============================================================================

void captureInit() {
    // Find telemetry partitions
    s_accel.init(findPartition(PARTITION_SUBTYPE_ACCEL));
    s_hall.init(findPartition(PARTITION_SUBTYPE_HALL));
    s_stats.init(findPartition(PARTITION_SUBTYPE_STATS));

    if (!s_accel.partition || !s_hall.partition || !s_stats.partition) {
        Serial.println("[CAPTURE] ERROR: Missing telemetry partitions!");
        Serial.printf("  accel: %s\n", s_accel.partition ? "OK" : "MISSING");
        Serial.printf("  hall: %s\n", s_hall.partition ? "OK" : "MISSING");
        Serial.printf("  stats: %s\n", s_stats.partition ? "OK" : "MISSING");
        return;
    }

    Serial.printf("[CAPTURE] Partitions found: accel=%zuKB hall=%zuKB stats=%zuKB\n",
                  s_accel.partition->size / 1024,
                  s_hall.partition->size / 1024,
                  s_stats.partition->size / 1024);

    s_state = CaptureState::IDLE;

    // Create capture queue
    Serial.printf("[CAPTURE] Creating queue: %zu items × %zu bytes = %zu bytes\n",
                  CAPTURE_QUEUE_SIZE, sizeof(CaptureMessage),
                  CAPTURE_QUEUE_SIZE * sizeof(CaptureMessage));
    s_captureQueue = xQueueCreate(CAPTURE_QUEUE_SIZE, sizeof(CaptureMessage));
    if (!s_captureQueue) {
        Serial.println("[CAPTURE] Failed to create capture queue!");
        return;
    }

    // Create capture task (pinned to Core 1, app core)
    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        captureTask,
        "captureTask",
        4096,           // Stack size
        nullptr,
        2,              // Priority (lower than time-critical tasks)
        &s_captureTask,
        1               // Pin to Core 1 (app core)
    );
    if (taskCreated != pdPASS) {
        Serial.println("[CAPTURE] Failed to create capture task!");
        vQueueDelete(s_captureQueue);
        s_captureQueue = nullptr;
        return;
    }
    Serial.println("[CAPTURE] Capture system ready (partition-based storage)");
}

void captureErase() {
    Serial.println("[CAPTURE] Erasing partitions...");
    uint32_t startMs = millis();
    eraseAllPartitions();
    Serial.printf("[CAPTURE] Erase complete (%lu ms)\n", millis() - startMs);
}

void captureStart() {
    // Already recording - ignore
    if (s_state == CaptureState::RECORDING) {
        Serial.println("[CAPTURE] Already recording");
        return;
    }

    // Task must be stopped before we can start
    if (!s_taskStopped) {
        Serial.println("[CAPTURE] Task still running, cannot start");
        return;
    }

    // Erase partitions (this takes time but ensures clean start)
    captureErase();

    // Drain any stale messages from queue
    if (s_captureQueue) {
        CaptureMessage msg;
        int drained = 0;
        while (xQueueReceive(s_captureQueue, &msg, 0) == pdPASS) {
            drained++;
        }
        if (drained > 0) {
            Serial.printf("[CAPTURE] Drained %d stale messages\n", drained);
        }
    }

    // Reset queue full counter
    s_queueFullCount = 0;

    s_state = CaptureState::RECORDING;

    // Wake the task to start recording
    if (s_captureTask) {
        xTaskNotifyGive(s_captureTask);
    }

    Serial.println("[CAPTURE] CAPTURE STARTED");
}

void captureStop() {
    if (s_state != CaptureState::RECORDING && s_state != CaptureState::FULL) {
        Serial.println("[CAPTURE] Not recording");
        return;
    }

    if (!sendStopToQueue()) {
        Serial.println("[CAPTURE] ERROR: Failed to send stop command");
        return;
    }

    if (!waitForTaskStop()) {
        Serial.println("[CAPTURE] ERROR: Task did not acknowledge stop");
        return;
    }

    Serial.println("[CAPTURE] CAPTURE STOPPED");
    Serial.println("--- Capture Summary ---");
    Serial.printf("  accel: %lu samples\n", s_accel.sample_count);
    Serial.printf("  hall: %lu events\n", s_hall.sample_count);
    Serial.printf("  stats: %lu records\n", s_stats.sample_count);
    if (s_queueFullCount > 0) {
        Serial.printf("  queue_full_drops: %lu\n", s_queueFullCount);
    }

    s_state = CaptureState::IDLE;
}

void capturePlay() {
    // Stop if currently recording
    if (s_state == CaptureState::RECORDING || s_state == CaptureState::FULL) {
        captureStop();
    }

    bool hasData = false;

    TelemetryHeader header;
    if (readPartitionHeader(s_accel.partition, &header) > 0) {
        dumpAccelCSV(false);
        hasData = true;
    }

    if (readPartitionHeader(s_hall.partition, &header) > 0) {
        if (hasData) waitForKeypress();
        dumpHallCSV(false);
        hasData = true;
    }

    if (readPartitionHeader(s_stats.partition, &header) > 0) {
        if (hasData) waitForKeypress();
        dumpStatsCSV(false);
        hasData = true;
    }

    if (!hasData) {
        Serial.println("[CAPTURE] No capture data");
    } else {
        Serial.println("\n=== DUMP COMPLETE ===");
    }
}

void captureDelete() {
    // Stop recording first if needed
    if (s_state == CaptureState::RECORDING || s_state == CaptureState::FULL) {
        captureStop();
    }

    captureErase();

    s_state = CaptureState::IDLE;
    Serial.println("[CAPTURE] Partitions erased");
}

void captureWrite(uint8_t msgType, const uint8_t* data, size_t len) {
    // Called from ESP-NOW callback (WiFi task) - must be fast and non-blocking
    if (s_state != CaptureState::RECORDING) return;
    if (len > MAX_CAPTURE_PAYLOAD || !s_captureQueue) return;

    CaptureMessage msg = {};
    msg.type = CaptureMessageType::DATA;
    msg.msgType = msgType;
    msg.len = static_cast<uint16_t>(len);
    memcpy(msg.data, data, len);

    // Non-blocking send (drop message if queue full)
    if (xQueueSend(s_captureQueue, &msg, 0) != pdPASS) {
        s_queueFullCount = s_queueFullCount + 1;
    }
}

CaptureState getCaptureState() {
    return s_state;
}

bool isCapturing() {
    return s_state == CaptureState::RECORDING;
}

bool isDumpInProgress() {
    return s_dumpInProgress;
}

// =============================================================================
// Serial Command Interface (script-friendly output)
// =============================================================================

void captureStatus() {
    switch (s_state) {
        case CaptureState::IDLE:      Serial.println("IDLE"); break;
        case CaptureState::RECORDING: Serial.println("RECORDING"); break;
        case CaptureState::FULL:      Serial.println("FULL"); break;
    }
}

void captureList() {
    TelemetryHeader header;

    uint32_t accelCount = readPartitionHeader(s_accel.partition, &header);
    if (accelCount > 0) {
        Serial.printf("MSG_ACCEL_SAMPLES.bin\t%lu\t%zu\n",
                      accelCount, accelCount * sizeof(AccelSampleRaw));
    }

    uint32_t hallCount = readPartitionHeader(s_hall.partition, &header);
    if (hallCount > 0) {
        Serial.printf("MSG_HALL_EVENT.bin\t%lu\t%zu\n",
                      hallCount, hallCount * sizeof(HallRecordRaw));
    }

    uint32_t statsCount = readPartitionHeader(s_stats.partition, &header);
    if (statsCount > 0) {
        Serial.printf("MSG_ROTOR_STATS.bin\t%lu\t%zu\n",
                      statsCount, statsCount * sizeof(RotorStatsRecord));
    }

    Serial.println();  // Blank line = end of list
}

void captureDump() {
    s_dumpInProgress = true;

    dumpAccelCSV(true);
    dumpHallCSV(true);
    dumpStatsCSV(true);

    Serial.println(">>>");  // End of dump marker
    s_dumpInProgress = false;
}

void captureStartSerial() {
    if (s_state == CaptureState::RECORDING) {
        Serial.println("ERR: Already recording");
        return;
    }
    if (!s_taskStopped) {
        Serial.println("ERR: Task busy");
        return;
    }

    // Note: Caller must use DELETE_ALL_CAPTURES first to erase partitions.
    // START_CAPTURE is now fast (instant) - erase is done separately.

    // Drain stale messages
    if (s_captureQueue) {
        CaptureMessage msg;
        while (xQueueReceive(s_captureQueue, &msg, 0) == pdPASS) {}
    }

    // Reset queue full counter
    s_queueFullCount = 0;

    s_state = CaptureState::RECORDING;

    if (s_captureTask) {
        xTaskNotifyGive(s_captureTask);
    }

    Serial.println("OK");
}

void captureStopSerial() {
    if (s_state != CaptureState::RECORDING && s_state != CaptureState::FULL) {
        Serial.println("ERR: Not recording");
        return;
    }

    if (!sendStopToQueue()) {
        Serial.println("ERR: Failed to send stop");
        return;
    }

    if (!waitForTaskStop()) {
        Serial.println("ERR: Stop timeout");
        return;
    }

    s_state = CaptureState::IDLE;
    Serial.println("OK");
}

void captureDeleteSerial() {
    // Stop if recording
    if (s_state == CaptureState::RECORDING || s_state == CaptureState::FULL) {
        if (!sendStopToQueue()) {
            Serial.println("ERR: Failed to stop for delete");
            return;
        }

        if (!waitForTaskStop()) {
            Serial.println("ERR: Stop timeout");
            return;
        }

        s_state = CaptureState::IDLE;
    }

    eraseAllPartitions();

    Serial.println("OK");
}
