#ifndef POV_MESSAGES_H
#define POV_MESSAGES_H

#include <stdint.h>
#include <cstddef>
#include "types.h"

// Message types for ESP-NOW communication between motor controller and display
enum MessageType : uint8_t {
    // DEPRECATED: MSG_TELEMETRY = 1 - replaced by MSG_ROTOR_STATS
    MSG_BRIGHTNESS_UP = 2,     // Motor Controller -> Display
    MSG_BRIGHTNESS_DOWN = 3,   // Motor Controller -> Display
    MSG_SET_EFFECT = 4,        // Motor Controller -> Display
    MSG_EFFECT_RIGHT = 5,      // Motor Controller -> Display (IR RIGHT button)
    MSG_EFFECT_LEFT = 6,       // Motor Controller -> Display (IR LEFT button)
    MSG_EFFECT_UP = 7,         // Motor Controller -> Display (IR UP button)
    MSG_EFFECT_DOWN = 8,       // Motor Controller -> Display (IR DOWN button)
    MSG_EFFECT_ENTER = 9,      // Motor Controller -> Display (IR ENTER button)
    MSG_STATS_TOGGLE = 15,     // Motor Controller -> Display (IR INFO button - toggle stats overlay)
    MSG_NEXT_EFFECT = 16,      // Motor Controller -> Display (IR CH_UP button - cycle forward)
    MSG_PREV_EFFECT = 17,      // Motor Controller -> Display (IR CH_DOWN button - cycle backward)
    // Calibration messages (Display -> Motor Controller)
    MSG_ACCEL_SAMPLES = 10,    // Batched accelerometer samples
    MSG_HALL_EVENT = 11,       // Individual hall trigger event
    // Diagnostic stats (Display -> Motor Controller)
    MSG_ROTOR_STATS = 12,      // Periodic diagnostic statistics
    // Commands (Motor Controller -> Display)
    MSG_RESET_ROTOR_STATS = 13, // Reset diagnostic stats counters
    MSG_DISPLAY_POWER = 14,     // Motor Controller -> Display (power on/off)
};

// =============================================================================
// Diagnostic Stats Messages (Display -> Motor Controller)
// Timer-based diagnostics for debugging hall sensor and ESP-NOW issues
// =============================================================================

// Display -> Motor Controller: Rotor diagnostic statistics
// Sent every 500ms by RotorDiagnosticStats timer
struct RotorStatsMsg {
    uint8_t type = MSG_ROTOR_STATS;  // 1 byte
    uint32_t reportSequence;          // 4 bytes - increments each send
    timestamp_t created_us;           // 8 bytes - when stats were reset
    timestamp_t lastUpdated_us;       // 8 bytes - most recent update

    // Hall sensor stats
    uint32_t hallEventsTotal;         // 4 bytes - total since reset
    period_t hallAvg_us;              // 4 bytes - smoothed period (for RPM calc)

    // Enhanced outlier tracking (separate counters by rejection reason)
    uint32_t outliersTooFast;         // 4 bytes - intervals < MIN_REASONABLE_INTERVAL
    uint32_t outliersTooSlow;         // 4 bytes - intervals > MAX_INTERVAL_RATIO * avg
    uint32_t outliersRatioLow;        // 4 bytes - intervals < MIN_INTERVAL_RATIO * avg
    uint32_t lastOutlierInterval_us;  // 4 bytes - most recent rejected interval
    uint8_t lastOutlierReason;        // 1 byte - 0=none, 1=too_fast, 2=too_slow, 3=ratio_low

    // ESP-NOW stats
    uint32_t espnowSendAttempts;      // 4 bytes
    uint32_t espnowSendFailures;      // 4 bytes

    // Render pipeline stats (delta since last report)
    uint16_t renderCount;             // 2 bytes - successful renders
    uint16_t skipCount;               // 2 bytes - skipped (behind schedule)
    uint16_t notRotatingCount;        // 2 bytes - loop exited early

    // Current state
    uint8_t effectNumber;             // 1 byte
    uint8_t brightness;               // 1 byte
} __attribute__((packed));
// Size: 62 bytes (well under ESP-NOW limit)

// Motor Controller -> Display: Reset diagnostic stats
// Zeros all counters and updates created_us
struct ResetRotorStatsMsg {
    uint8_t type = MSG_RESET_ROTOR_STATS;
} __attribute__((packed));
// Size: 1 byte

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

// Motor Controller -> Display: IR RIGHT button
struct EffectRightMsg {
    uint8_t type = MSG_EFFECT_RIGHT;
} __attribute__((packed));

// Motor Controller -> Display: IR LEFT button
struct EffectLeftMsg {
    uint8_t type = MSG_EFFECT_LEFT;
} __attribute__((packed));

// Motor Controller -> Display: IR UP button
struct EffectUpMsg {
    uint8_t type = MSG_EFFECT_UP;
} __attribute__((packed));

// Motor Controller -> Display: IR DOWN button
struct EffectDownMsg {
    uint8_t type = MSG_EFFECT_DOWN;
} __attribute__((packed));

// Motor Controller -> Display: IR ENTER button
struct EffectEnterMsg {
    uint8_t type = MSG_EFFECT_ENTER;
} __attribute__((packed));

// Motor Controller -> Display: IR INFO button (toggle stats overlay)
struct StatsToggleMsg {
    uint8_t type = MSG_STATS_TOGGLE;
} __attribute__((packed));

// Motor Controller -> Display: IR CH_UP button (cycle to next effect)
struct NextEffectMsg {
    uint8_t type = MSG_NEXT_EFFECT;
} __attribute__((packed));

// Motor Controller -> Display: IR CH_DOWN button (cycle to previous effect)
struct PrevEffectMsg {
    uint8_t type = MSG_PREV_EFFECT;
} __attribute__((packed));

// Motor Controller -> Display: Power on/off control
struct DisplayPowerMsg {
    uint8_t type = MSG_DISPLAY_POWER;
    uint8_t enabled;  // 1 = on, 0 = off
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

// AccelSampleMsg header size (type + sample_count + base_timestamp + start_sequence)
static constexpr size_t ACCEL_MSG_HEADER_SIZE =
    sizeof(uint8_t) + sizeof(uint8_t) + sizeof(timestamp_t) + sizeof(sequence_t);

// Maximum samples per batch - auto-computed to fill ESP-NOW v2.0 payload
// Adjusts automatically if AccelSampleWire or header size changes
static constexpr size_t ACCEL_SAMPLES_MAX_BATCH =
    (ESPNOW_MAX_PAYLOAD_V2 - ACCEL_MSG_HEADER_SIZE) / sizeof(AccelSampleWire);

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
