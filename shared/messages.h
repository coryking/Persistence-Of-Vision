#ifndef POV_MESSAGES_H
#define POV_MESSAGES_H

#include <stdint.h>

// Message types for ESP-NOW communication between motor controller and display
enum MessageType : uint8_t {
    MSG_TELEMETRY = 1,       // Display -> Motor Controller
    MSG_BRIGHTNESS_UP = 2,   // Motor Controller -> Display
    MSG_BRIGHTNESS_DOWN = 3, // Motor Controller -> Display
    MSG_SET_EFFECT = 4,      // Motor Controller -> Display
};

// Display -> Motor Controller: Telemetry
// Sent every ROLLING_AVERAGE_SIZE revolutions
struct TelemetryMsg {
    uint8_t type = MSG_TELEMETRY;
    uint32_t timestamp_us;         // ESP timestamp (esp_timer_get_time())
    uint16_t hall_avg_us;          // Rolling average hall sensor period (microseconds)
    uint16_t revolutions;          // Revolutions since last message
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

#endif // POV_MESSAGES_H
