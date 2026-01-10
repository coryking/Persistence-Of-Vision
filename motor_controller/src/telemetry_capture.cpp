#include "telemetry_capture.h"
#include "messages.h"
#include <LittleFS.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// Reserve space to avoid completely filling filesystem
static const size_t FS_RESERVE_BYTES = 50 * 1024;

// Telemetry directory
static const char* TELEMETRY_DIR = "/telemetry";

// Binary record formats (packed structs for file storage)
struct AccelRecord {
    timestamp_t timestamp;  // 64-bit absolute timestamp
    accel_raw_t x;          // Raw X axis (256 LSB/g)
    accel_raw_t y;          // Raw Y axis
    accel_raw_t z;          // Raw Z axis
} __attribute__((packed));

struct HallRecord {
    timestamp_t timestamp;  // 64-bit: when hall sensor triggered
    period_t period_us;     // 32-bit: time since previous trigger
} __attribute__((packed));

struct TelemetryRecord {
    timestamp_t timestamp;      // 64-bit message timestamp
    period_t hall_avg_us;       // 32-bit rolling average hall period
    uint16_t revolutions;       // Revs since last message
    uint16_t notRotatingCount;  // Debug counter
    uint16_t skipCount;         // Debug counter
    uint16_t renderCount;       // Debug counter
} __attribute__((packed));

// State
static CaptureState s_state = CaptureState::IDLE;

// FreeRTOS queue for receiving messages from ESP-NOW callback
static QueueHandle_t s_captureQueue = nullptr;
static TaskHandle_t s_captureTask = nullptr;

// Message wrapper for queue (raw ESP-NOW payload)
// Size reduced to fit largest expected message (AccelSampleMsg = 226 bytes)
struct CaptureMessage {
    uint8_t type;
    uint8_t len;
    uint8_t data[226];  // Max AccelSampleMsg size (2 + 16*14)
} __attribute__((packed));

static const size_t CAPTURE_QUEUE_SIZE = 32;  // ~0.8 sec buffer at 40 batches/sec

// Track open file handles (sparse array indexed by msgType)
// Only a few will be used, but keeps lookup simple
static const size_t MAX_MSG_TYPES = 16;
static File s_files[MAX_MSG_TYPES];
static bool s_fileOpen[MAX_MSG_TYPES];
static uint32_t s_recordCounts[MAX_MSG_TYPES];
static uint32_t s_bytesWritten[MAX_MSG_TYPES];

// Check if filesystem has enough space
static bool hasSpace() {
    size_t used = LittleFS.usedBytes();
    size_t total = LittleFS.totalBytes();
    return (total - used) > FS_RESERVE_BYTES;
}

// Get filename for a message type
static String getFilename(uint8_t msgType) {
    return String(TELEMETRY_DIR) + "/" + String(msgType) + ".bin";
}

// Open file for a message type (lazy creation)
static bool ensureFileOpen(uint8_t msgType) {
    if (msgType >= MAX_MSG_TYPES) return false;

    if (s_fileOpen[msgType]) {
        // Verify file handle is still valid
        if (!s_files[msgType]) {
            Serial.printf("[CAPTURE] File %u handle became invalid!\n", msgType);
            s_fileOpen[msgType] = false;
            // Fall through to reopen
        } else {
            return true;
        }
    }

    String filename = getFilename(msgType);
    s_files[msgType] = LittleFS.open(filename, "w");
    if (!s_files[msgType]) {
        Serial.printf("[CAPTURE] Failed to create %s\n", filename.c_str());
        return false;
    }

    Serial.printf("[CAPTURE] Opened %s for writing\n", filename.c_str());
    s_fileOpen[msgType] = true;
    s_recordCounts[msgType] = 0;
    s_bytesWritten[msgType] = 0;
    return true;
}

// Write raw bytes to file
static bool writeRecord(uint8_t msgType, const void* data, size_t len) {
    if (!ensureFileOpen(msgType)) return false;

    if (!hasSpace()) {
        Serial.println("[CAPTURE] CAPTURE FULL - filesystem limit reached");
        s_state = CaptureState::FULL;
        return false;
    }

    size_t written = s_files[msgType].write((const uint8_t*)data, len);
    if (written != len) {
        Serial.printf("[CAPTURE] Write failed: %zu/%zu bytes\n", written, len);
        return false;
    }

    s_recordCounts[msgType]++;
    s_bytesWritten[msgType] += len;
    return true;
}

// Write a batch of records (for accel samples)
static bool writeBatch(uint8_t msgType, const void* data, size_t len, uint32_t recordCount) {
    if (!ensureFileOpen(msgType)) return false;

    if (!hasSpace()) {
        Serial.println("[CAPTURE] CAPTURE FULL - filesystem limit reached");
        s_state = CaptureState::FULL;
        return false;
    }

    File& f = s_files[msgType];
    size_t written = f.write((const uint8_t*)data, len);
    if (written != len) {
        int err = f.getWriteError();
        Serial.printf("[CAPTURE] Write failed: file=%u wrote=%zu/%zu err=%d valid=%d pos=%zu\n",
                      msgType, written, len, err, (bool)f, f.position());
        return false;
    }

    s_recordCounts[msgType] += recordCount;
    s_bytesWritten[msgType] += len;
    return true;
}

// Close all open files
static void closeAllFiles() {
    for (size_t i = 0; i < MAX_MSG_TYPES; i++) {
        if (s_fileOpen[i]) {
            s_files[i].close();
            s_fileOpen[i] = false;
        }
    }
}

// Delete all files in telemetry directory
static void deleteAllFiles() {
    File dir = LittleFS.open(TELEMETRY_DIR);
    if (!dir || !dir.isDirectory()) {
        Serial.println("[CAPTURE] No telemetry dir to clean");
        return;
    }

    int count = 0;
    File file = dir.openNextFile();
    while (file) {
        String path = String(TELEMETRY_DIR) + "/" + file.name();
        file.close();
        if (LittleFS.remove(path)) {
            count++;
        } else {
            Serial.printf("[CAPTURE] Failed to delete %s\n", path.c_str());
        }
        file = dir.openNextFile();
    }
    dir.close();
    if (count > 0) {
        Serial.printf("[CAPTURE] Deleted %d files\n", count);
    }
}

// Get human-readable name for message type
static const char* getMsgTypeName(uint8_t msgType) {
    switch (msgType) {
        case MSG_TELEMETRY: return "MSG_TELEMETRY";
        case MSG_ACCEL_SAMPLES: return "MSG_ACCEL_SAMPLES";
        case MSG_HALL_EVENT: return "MSG_HALL_EVENT";
        default: return "UNKNOWN";
    }
}

// Dump a file as CSV
static void dumpFileCSV(uint8_t msgType, File& file) {
    size_t fileSize = file.size();

    switch (msgType) {
        case MSG_ACCEL_SAMPLES: {
            size_t recordCount = fileSize / sizeof(AccelRecord);
            Serial.printf("=== FILE: %u.bin (%s, %zu records) ===\n",
                          msgType, getMsgTypeName(msgType), recordCount);
            Serial.println("timestamp_us,x,y,z");

            AccelRecord rec;
            while (file.read((uint8_t*)&rec, sizeof(rec)) == sizeof(rec)) {
                Serial.printf("%llu,%d,%d,%d\n", rec.timestamp, rec.x, rec.y, rec.z);
            }
            break;
        }

        case MSG_HALL_EVENT: {
            size_t recordCount = fileSize / sizeof(HallRecord);
            Serial.printf("=== FILE: %u.bin (%s, %zu records) ===\n",
                          msgType, getMsgTypeName(msgType), recordCount);
            Serial.println("timestamp_us,period_us");

            HallRecord rec;
            while (file.read((uint8_t*)&rec, sizeof(rec)) == sizeof(rec)) {
                Serial.printf("%llu,%lu\n", rec.timestamp, rec.period_us);
            }
            break;
        }

        case MSG_TELEMETRY: {
            size_t recordCount = fileSize / sizeof(TelemetryRecord);
            Serial.printf("=== FILE: %u.bin (%s, %zu records) ===\n",
                          msgType, getMsgTypeName(msgType), recordCount);
            Serial.println("timestamp_us,hall_avg_us,revolutions,not_rotating,skip,render");

            TelemetryRecord rec;
            while (file.read((uint8_t*)&rec, sizeof(rec)) == sizeof(rec)) {
                Serial.printf("%llu,%lu,%u,%u,%u,%u\n",
                              rec.timestamp, rec.hall_avg_us, rec.revolutions,
                              rec.notRotatingCount, rec.skipCount, rec.renderCount);
            }
            break;
        }

        default: {
            // Unknown type - dump as hex
            Serial.printf("=== FILE: %u.bin (UNKNOWN, %zu bytes) ===\n", msgType, fileSize);
            Serial.println("offset,hex");

            uint8_t buf[16];
            size_t offset = 0;
            while (size_t n = file.read(buf, sizeof(buf))) {
                Serial.printf("%04zu,", offset);
                for (size_t i = 0; i < n; i++) {
                    Serial.printf("%02X ", buf[i]);
                }
                Serial.println();
                offset += n;
            }
            break;
        }
    }
}

// Wait for any serial input
static void waitForKeypress() {
    Serial.println("\nPress any key to continue...");
    Serial.flush();

    // Clear any pending input
    while (Serial.available()) Serial.read();

    // Wait for new input
    while (!Serial.available()) {
        delay(10);
    }

    // Clear the input
    while (Serial.available()) Serial.read();
    Serial.println();
}

// Process a single message from the queue (runs in captureTask context)
static void processMessage(uint8_t msgType, const uint8_t* data, size_t len) {
    switch (msgType) {
        case MSG_ACCEL_SAMPLES: {
            // Validate minimum size: header (2 bytes)
            if (len < 2) return;
            const AccelSampleMsg* msg = reinterpret_cast<const AccelSampleMsg*>(data);

            // Validate sample_count bounds
            if (msg->sample_count == 0 || msg->sample_count > 16) return;

            // Validate length matches sample_count
            size_t expected = 2 + (msg->sample_count * sizeof(AccelSample));
            if (len < expected) return;

            // Build batch of records
            AccelRecord records[16];
            for (uint8_t i = 0; i < msg->sample_count; i++) {
                const AccelSample& s = msg->samples[i];
                records[i].timestamp = s.timestamp_us;
                records[i].x = s.x;
                records[i].y = s.y;
                records[i].z = s.z;
            }

            // Single batched write
            size_t totalBytes = msg->sample_count * sizeof(AccelRecord);
            writeBatch(MSG_ACCEL_SAMPLES, records, totalBytes, msg->sample_count);
            break;
        }

        case MSG_HALL_EVENT: {
            if (len < sizeof(HallEventMsg)) return;
            const HallEventMsg* msg = reinterpret_cast<const HallEventMsg*>(data);

            HallRecord rec;
            rec.timestamp = msg->timestamp_us;
            rec.period_us = msg->period_us;
            writeRecord(MSG_HALL_EVENT, &rec, sizeof(rec));
            break;
        }

        case MSG_TELEMETRY: {
            if (len < sizeof(TelemetryMsg)) return;
            const TelemetryMsg* msg = reinterpret_cast<const TelemetryMsg*>(data);

            TelemetryRecord rec;
            rec.timestamp = msg->timestamp_us;
            rec.hall_avg_us = msg->hall_avg_us;
            rec.revolutions = msg->revolutions;
            rec.notRotatingCount = msg->notRotatingCount;
            rec.skipCount = msg->skipCount;
            rec.renderCount = msg->renderCount;
            writeRecord(MSG_TELEMETRY, &rec, sizeof(rec));
            break;
        }

        default: {
            // Unknown message type - write raw bytes for future-proofing
            writeRecord(msgType, data, len);
            break;
        }
    }
}

// FreeRTOS task that processes capture queue (runs on Core 1)
static void captureTask(void* pvParameters) {
    (void)pvParameters;
    CaptureMessage msg;

    while (true) {
        // Block until message available
        if (xQueueReceive(s_captureQueue, &msg, portMAX_DELAY) == pdPASS) {
            if (s_state == CaptureState::RECORDING) {
                processMessage(msg.type, msg.data, msg.len);
            }
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

void captureInit() {
    if (!LittleFS.begin(true)) {  // true = format on fail
        Serial.println("[CAPTURE] LittleFS mount failed!");
        return;
    }

    // Create telemetry directory if needed
    if (!LittleFS.exists(TELEMETRY_DIR)) {
        LittleFS.mkdir(TELEMETRY_DIR);
    }

    // Initialize tracking arrays
    for (size_t i = 0; i < MAX_MSG_TYPES; i++) {
        s_fileOpen[i] = false;
        s_recordCounts[i] = 0;
        s_bytesWritten[i] = 0;
    }

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
    Serial.printf("[CAPTURE] Queue created: %p\n", (void*)s_captureQueue);

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
    Serial.println("[CAPTURE] Capture task started");

    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    Serial.printf("[CAPTURE] LittleFS ready: %zu/%zu bytes used\n", used, total);
}

void captureStart() {
    // Close any open files first
    closeAllFiles();

    // Delete all existing files
    deleteAllFiles();

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

    // Reset tracking
    for (size_t i = 0; i < MAX_MSG_TYPES; i++) {
        s_recordCounts[i] = 0;
        s_bytesWritten[i] = 0;
    }

    s_state = CaptureState::RECORDING;
    Serial.println("[CAPTURE] CAPTURE STARTED");
}

void captureStop() {
    if (s_state != CaptureState::RECORDING && s_state != CaptureState::FULL) {
        Serial.println("[CAPTURE] Not recording");
        return;
    }

    closeAllFiles();

    // Print summary
    Serial.println("[CAPTURE] CAPTURE STOPPED");
    Serial.println("--- Capture Summary ---");

    bool anyData = false;
    for (size_t i = 0; i < MAX_MSG_TYPES; i++) {
        if (s_recordCounts[i] > 0) {
            anyData = true;
            Serial.printf("  %s (%zu): %lu records, %lu bytes\n",
                          getMsgTypeName(i), i, s_recordCounts[i], s_bytesWritten[i]);
        }
    }

    if (!anyData) {
        Serial.println("  (no data captured)");
    }

    size_t used = LittleFS.usedBytes();
    size_t total = LittleFS.totalBytes();
    Serial.printf("Filesystem: %zu/%zu bytes used\n", used, total);

    s_state = CaptureState::STOPPED;
}

void capturePlay() {
    // Stop if currently recording
    if (s_state == CaptureState::RECORDING || s_state == CaptureState::FULL) {
        captureStop();
    }

    File dir = LittleFS.open(TELEMETRY_DIR);
    if (!dir || !dir.isDirectory()) {
        Serial.println("[CAPTURE] No capture data");
        return;
    }

    // Iterate through files
    bool anyFiles = false;
    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            // Extract message type from filename (e.g., "10.bin" -> 10)
            String name = file.name();
            int dotPos = name.indexOf('.');
            if (dotPos > 0) {
                uint8_t msgType = name.substring(0, dotPos).toInt();
                String fullPath = String(TELEMETRY_DIR) + "/" + name;
                file.close();

                // Reopen for reading
                File readFile = LittleFS.open(fullPath, "r");
                if (readFile) {
                    if (anyFiles) {
                        waitForKeypress();
                    }
                    dumpFileCSV(msgType, readFile);
                    readFile.close();
                    anyFiles = true;
                }
            } else {
                file.close();
            }
        } else {
            file.close();
        }
        file = dir.openNextFile();
    }
    dir.close();

    if (!anyFiles) {
        Serial.println("[CAPTURE] No capture data");
    } else {
        Serial.println("\n=== DUMP COMPLETE ===");
    }
}

void captureDelete() {
    closeAllFiles();
    deleteAllFiles();

    // Reset tracking
    for (size_t i = 0; i < MAX_MSG_TYPES; i++) {
        s_recordCounts[i] = 0;
        s_bytesWritten[i] = 0;
    }

    s_state = CaptureState::IDLE;
    Serial.println("[CAPTURE] Files deleted");
}

void captureWrite(uint8_t msgType, const uint8_t* data, size_t len) {
    // Called from ESP-NOW callback (WiFi task) - must be fast and non-blocking
    if (s_state != CaptureState::RECORDING) return;
    if (len > 226 || !s_captureQueue) return;  // Max AccelSampleMsg size

    CaptureMessage msg;
    msg.type = msgType;
    msg.len = static_cast<uint8_t>(len);
    memcpy(msg.data, data, len);

    // Non-blocking send (drop message if queue full)
    xQueueSend(s_captureQueue, &msg, 0);
}

CaptureState getCaptureState() {
    return s_state;
}

bool isCapturing() {
    return s_state == CaptureState::RECORDING;
}
