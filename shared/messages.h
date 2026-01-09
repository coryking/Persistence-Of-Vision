#ifndef POV_MESSAGES_H
#define POV_MESSAGES_H

#include <stdint.h>
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

// Single accelerometer sample with absolute timestamp
struct AccelSample {
    timestamp_t timestamp_us;    // Absolute timestamp (esp_timer_get_time()) - 64-bit
    accel_raw_t x;               // Raw X axis (256 LSB/g in full resolution)
    accel_raw_t y;               // Raw Y axis
    accel_raw_t z;               // Raw Z axis
} __attribute__((packed));       // 14 bytes per sample

// Display -> Motor Controller: Batched accelerometer samples
// Sent periodically during calibration (~40 batches/sec at 400Hz sampling)
struct AccelSampleMsg {
    uint8_t type = MSG_ACCEL_SAMPLES;
    uint8_t sample_count;           // Actual samples in this batch (1-16)
    AccelSample samples[16];        // Up to 16 samples per batch
} __attribute__((packed));
// Size: 1 + 1 + (16 * 14) = 226 bytes max (under 250-byte ESP-NOW limit)

// Display -> Motor Controller: Hall sensor trigger event
// Sent for each hall trigger during calibration (~20-47/sec at 1200-2800 RPM)
struct HallEventMsg {
    uint8_t type = MSG_HALL_EVENT;
    timestamp_t timestamp_us;    // Hall trigger time (same clock as accel samples) - 64-bit
    period_t period_us;          // Time since previous hall trigger
} __attribute__((packed));
// Size: 13 bytes

#endif // POV_MESSAGES_H
