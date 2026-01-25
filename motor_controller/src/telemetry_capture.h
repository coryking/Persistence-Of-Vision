#pragma once
#include <cstddef>
#include <cstdint>
#include "types.h"

// Flash sector size for ESP32 (writes must be sector-aligned for efficiency)
static constexpr size_t FLASH_SECTOR_SIZE = 4096;

// Partition subtypes (must match partitions.csv)
static constexpr uint8_t PARTITION_SUBTYPE_ACCEL = 0x80;
static constexpr uint8_t PARTITION_SUBTYPE_HALL = 0x81;
static constexpr uint8_t PARTITION_SUBTYPE_STATS = 0x82;

// Magic number for telemetry header ("TELM" in little-endian)
static constexpr uint32_t TELEMETRY_MAGIC = 0x4D4C4554;
static constexpr uint32_t TELEMETRY_VERSION = 1;

enum class CaptureState : uint8_t {
    IDLE,       // Ready for RECORD (also used after STOP)
    RECORDING,  // Actively writing to flash
    FULL        // Partition limit reached (still recording, just can't write more)
};

// =============================================================================
// Binary Record Formats (optimized for flash storage)
// All structs are packed and 4-byte aligned for efficient flash writes
// =============================================================================

// Header at start of each partition (32 bytes, 4-byte aligned)
struct TelemetryHeader {
    uint32_t magic;           // 0x54454C4D "TELM"
    uint32_t version;         // Format version (1)
    uint64_t base_timestamp;  // First sample absolute time (microseconds)
    uint32_t start_sequence;  // First sample sequence number
    uint32_t sample_count;    // Total samples written
    uint32_t reserved[2];     // Padding for future use
} __attribute__((packed));

static_assert(sizeof(TelemetryHeader) == 32, "TelemetryHeader must be 32 bytes");

// Accel sample for flash storage (16 bytes, 4-byte aligned)
// Uses delta timestamp from header's base_timestamp for compactness
// 256 samples exactly fill one 4KB sector
struct AccelSampleRaw {
    uint32_t delta_us;        // Offset from base_timestamp (max ~71 minutes)
    int16_t x, y, z;          // Accelerometer raw values
    int16_t gx, gy, gz;       // Gyroscope raw values
} __attribute__((packed));

static_assert(sizeof(AccelSampleRaw) == 16, "AccelSampleRaw must be 16 bytes");
static_assert(FLASH_SECTOR_SIZE % sizeof(AccelSampleRaw) == 0, "Sector must hold whole samples");

// Hall event for flash storage (12 bytes, 4-byte aligned)
struct HallRecordRaw {
    uint32_t delta_us;        // Offset from base_timestamp
    uint32_t period_us;       // Time since previous trigger
    uint32_t rotation_num;    // Revolution counter (expanded from uint16_t)
} __attribute__((packed));

static_assert(sizeof(HallRecordRaw) == 12, "HallRecordRaw must be 12 bytes");

// Rotor stats for flash storage (52 bytes, from RotorStatsMsg minus type byte)
// Kept as-is since it's infrequently written (~2/sec)
struct RotorStatsRecord {
    uint32_t reportSequence;          // Report sequence number
    timestamp_t created_us;           // When stats were reset
    timestamp_t lastUpdated_us;       // Most recent update

    // Hall sensor stats
    uint32_t hallEventsTotal;         // Total hall events since reset
    uint32_t hallOutliersFiltered;    // Rejected events
    uint32_t lastOutlierInterval_us;  // Most recent bad interval
    period_t hallAvg_us;              // Smoothed period

    // ESP-NOW stats
    uint32_t espnowSendAttempts;
    uint32_t espnowSendFailures;

    // Render pipeline stats
    uint16_t renderCount;
    uint16_t skipCount;
    uint16_t notRotatingCount;

    // Current state
    uint8_t effectNumber;
    uint8_t brightness;

    // Motor controller state (added by motor controller when storing)
    uint8_t speedPreset;      // Motor speed preset (1-10)
    uint8_t pwmValue;         // Actual PWM output (0-255)
} __attribute__((packed));

static_assert(sizeof(RotorStatsRecord) == 54, "RotorStatsRecord must be 54 bytes");

// =============================================================================
// Public API
// =============================================================================

void captureInit();       // Call from setup() - finds partitions
void captureStart();      // RECORD: erase partitions, start recording (prints OK/ERR)
void captureStop();       // STOP: flush buffers, print summary (prints OK/ERR)
void capturePlay();       // PLAY: dump all partitions as CSV (interactive, for IR remote)
void captureDelete();     // DELETE: erase all telemetry partitions (prints OK)
void captureErase();      // Erase partitions (called internally by captureStart, can be called separately)

// Serial command interface (script-friendly output)
void captureStatus();     // Print state: IDLE, RECORDING, or FULL
void captureList();       // Print files: filename<TAB>records<TAB>bytes per line
void captureDump();       // Dump all partitions with >>> markers, no prompts

// Serial command wrappers (print OK/ERR instead of verbose output)
void captureStartSerial();   // START command - prints OK or ERR:
void captureStopSerial();    // STOP command - prints OK or ERR:
void captureDeleteSerial();  // DELETE command - prints OK

// Write telemetry data - queues for background task
// msgType: the MessageType enum value (determines partition)
// data: raw ESP-NOW message bytes (including type byte)
// len: number of bytes
// Note: For MSG_ACCEL_SAMPLES, unpacks batch and writes each sample
void captureWrite(uint8_t msgType, const uint8_t* data, size_t len);

CaptureState getCaptureState();
bool isCapturing();       // Convenience: returns true if RECORDING
bool isDumpInProgress();  // True during DUMP command (suppresses debug output)
