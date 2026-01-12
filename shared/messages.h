#ifndef POV_MESSAGES_H
#define POV_MESSAGES_H

#include <stdint.h>
#include <cstddef>
#include "types.h"

// Message types for ESP-NOW communication between motor controller and display
enum MessageType : uint8_t {
    MSG_TELEMETRY = 1,         // Display -> Motor Controller
    MSG_BRIGHTNESS_UP = 2,     // Motor Controller -> Display
    MSG_BRIGHTNESS_DOWN = 3,   // Motor Controller -> Display
    MSG_SET_EFFECT = 4,        // Motor Controller -> Display
    MSG_EFFECT_MODE_NEXT = 5,  // Motor Controller -> Display (cycle effect's internal mode forward)
    MSG_EFFECT_MODE_PREV = 6,  // Motor Controller -> Display (cycle effect's internal mode backward)
    MSG_EFFECT_PARAM_UP = 7,   // Motor Controller -> Display (effect's secondary parameter up)
    MSG_EFFECT_PARAM_DOWN = 8, // Motor Controller -> Display (effect's secondary parameter down)
    // Calibration messages (Display -> Motor Controller)
    MSG_ACCEL_SAMPLES = 10,    // Batched accelerometer samples
    MSG_HALL_EVENT = 11,       // Individual hall trigger event
};

// Display -> Motor Controller: Telemetry
// Sent every ROLLING_AVERAGE_SIZE revolutions
// All counters are reset after each send (delta since last telemetry)
struct TelemetryMsg {
    uint8_t type = MSG_TELEMETRY;
    timestamp_t timestamp_us;      // ESP timestamp (esp_timer_get_time()) - 64-bit
    period_t hall_avg_us;          // Rolling average hall sensor period (microseconds)
    uint16_t revolutions;          // Revolutions since last message
    // Debug counters for strobe diagnosis (reset each telemetry send)
    uint16_t notRotatingCount;     // Times handleNotRotating was called
    uint16_t skipCount;            // Times we skipped (behind schedule)
    uint16_t renderCount;          // Times we actually rendered + Show()
} __attribute__((packed));

// Motor Controller -> Display: Increment brightness
struct BrightnessUpMsg {
    uint8_t type = MSG_BRIGHTNESS_UP;
} __attribute__((packed));

// Motor Controller -> Display: Decrement brightness
struct BrightnessDownMsg {
    uint8_t type = MSG_BRIGHTNESS_DOWN;
} __attribute__((packed));

// Motor Controller -> Display: Set effect by number
struct SetEffectMsg {
    uint8_t type = MSG_SET_EFFECT;
    uint8_t effect_number;  // 1-10 (1-based, matches remote buttons)
} __attribute__((packed));

// Motor Controller -> Display: Cycle effect mode forward
struct EffectModeNextMsg {
    uint8_t type = MSG_EFFECT_MODE_NEXT;
} __attribute__((packed));

// Motor Controller -> Display: Cycle effect mode backward
struct EffectModePrevMsg {
    uint8_t type = MSG_EFFECT_MODE_PREV;
} __attribute__((packed));

// Motor Controller -> Display: Effect parameter up (effect-specific, e.g., palette)
struct EffectParamUpMsg {
    uint8_t type = MSG_EFFECT_PARAM_UP;
} __attribute__((packed));

// Motor Controller -> Display: Effect parameter down (effect-specific, e.g., palette)
struct EffectParamDownMsg {
    uint8_t type = MSG_EFFECT_PARAM_DOWN;
} __attribute__((packed));

// =============================================================================
// Calibration Messages (Display -> Motor Controller)
// Used by CalibrationEffect for rotor balancing data collection
// =============================================================================

// ESP-NOW v2.0 payload limit (ESP-IDF 5.4+)
// Both devices are ESP32-S3 on ESP-IDF 5.5, so v2.0 is available.
// Note: esp_now.h defines ESP_NOW_MAX_DATA_LEN (250) and ESP_NOW_MAX_DATA_LEN_V2 (1470)
static constexpr size_t ESPNOW_MAX_PAYLOAD_V2 = 1470;

// Individual sample for wire transmission (delta timestamps for efficiency)
// 14 bytes per sample: accel (6) + gyro (6) + delta (2)
struct AccelSampleWire {
    uint16_t delta_us;           // 2 bytes - offset from batch base_timestamp (max 65ms)
    accel_raw_t x, y, z;         // 6 bytes (3 × int16_t) - accelerometer
    gyro_raw_t gx, gy, gz;       // 6 bytes (3 × int16_t) - gyroscope
} __attribute__((packed));

// Maximum samples per batch
// At 1kHz, 50 samples = 50ms of data = ~20 packets/sec
// Header: 12 bytes, Per sample: 14 bytes (accel + gyro)
// Max with v2.0: (1470 - 12) / 14 = 104 samples, but 50 provides reasonable latency
static constexpr size_t ACCEL_SAMPLES_MAX_BATCH = 50;

// AccelSampleMsg header size (type + sample_count + base_timestamp + start_sequence)
static constexpr size_t ACCEL_MSG_HEADER_SIZE =
    sizeof(uint8_t) + sizeof(uint8_t) + sizeof(timestamp_t) + sizeof(sequence_t);

// Display -> Motor Controller: Batched accelerometer samples with delta timestamps
// Motor controller expands deltas to absolute timestamps when writing to file
struct AccelSampleMsg {
    uint8_t type = MSG_ACCEL_SAMPLES;
    uint8_t sample_count;                           // Actual samples in this batch
    timestamp_t base_timestamp;                     // Absolute time of first sample
    sequence_t start_sequence;                      // Sequence number of first sample
    AccelSampleWire samples[ACCEL_SAMPLES_MAX_BATCH];
} __attribute__((packed));

// Verify message fits in ESP-NOW v2.0 payload
static_assert(sizeof(AccelSampleMsg) <= ESPNOW_MAX_PAYLOAD_V2,
              "AccelSampleMsg exceeds ESP-NOW v2.0 payload limit");

// Display -> Motor Controller: Hall sensor trigger event
// Sent for each hall trigger during calibration (~20-47/sec at 1200-2800 RPM)
struct HallEventMsg {
    uint8_t type = MSG_HALL_EVENT;
    timestamp_t timestamp_us;    // Hall trigger time (same clock as accel samples) - 64-bit
    period_t period_us;          // Time since previous hall trigger
    rotation_t rotation_num;     // Revolution counter (links to AccelSample.rotation_num)
} __attribute__((packed));
// Size: 15 bytes

#endif // POV_MESSAGES_H
