#ifndef DISPLAY_STATE_H
#define DISPLAY_STATE_H

#include <atomic>
#include <cmath>

// Cross-core safe state for display settings
// ESP-NOW callback runs on Core 0 (WiFi task)
// Render loop runs on Core 1
// Atomics provide lock-free synchronization
struct DisplayState {
    std::atomic<uint8_t> brightness{5};    // 0-10 scale, default mid
    std::atomic<uint8_t> effectNumber{1};  // 1-based effect number (for Phase 2)
};

// Global instance - defined in ESPNowComm.cpp
extern DisplayState g_displayState;

// Perceptual brightness mapping (gamma 2.2)
// Input: 0-10, Output: 0-255 for nscale8()
// Human vision perceives brightness logarithmically, so linear 50% feels
// much brighter than halfway. Gamma correction compensates for this.
inline uint8_t brightnessToScale(uint8_t brightness) {
    if (brightness == 0) return 0;
    if (brightness >= 10) return 255;
    // gamma 2.2: output = 255 * (input/10)^2.2
    float normalized = brightness / 10.0f;
    return static_cast<uint8_t>(255.0f * powf(normalized, 2.2f));
}

// Reference: brightnessToScale results
// 0 -> 0
// 1 -> 1
// 2 -> 5
// 3 -> 13
// 4 -> 25
// 5 -> 42
// 6 -> 65
// 7 -> 93
// 8 -> 128
// 9 -> 169
// 10 -> 255

#endif // DISPLAY_STATE_H
