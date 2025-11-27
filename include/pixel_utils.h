#ifndef PIXEL_UTILS_H
#define PIXEL_UTILS_H

#include <cstdint>
#include <algorithm>

// ========== Low-Level Primitives ==========

/**
 * Direct buffer write - bypasses NeoPixelBus overhead
 * Buffer format (DotStarBgrFeature): [brightness][B][G][R] per LED (4 bytes each)
 */
inline void setPixelColorDirect(uint8_t* buffer, uint16_t index,
                                uint8_t r, uint8_t g, uint8_t b) {
    uint8_t* pixel = buffer + (index * 4);
    pixel[0] = 0xFF;  // Full brightness (0xE0 | 31)
    pixel[1] = b;     // Blue (BGR order for DotStarBgrFeature)
    pixel[2] = g;     // Green
    pixel[3] = r;     // Red
}

/**
 * Read pixel color from buffer
 */
inline void getPixelColorDirect(const uint8_t* buffer, uint16_t index,
                                uint8_t& r, uint8_t& g, uint8_t& b) {
    const uint8_t* pixel = buffer + (index * 4);
    b = pixel[1];
    g = pixel[2];
    r = pixel[3];
}

// ========== Higher-Level Operations (Rule of 3 patterns) ==========

/**
 * Clear all pixels to black
 * Used by: All effects (initialization)
 */
inline void clearBuffer(uint8_t* buffer, uint16_t numLeds = 30) {
    // DotStar format: 4 bytes per LED, set all to 0xFF 00 00 00 (black at full brightness)
    for (uint16_t i = 0; i < numLeds; i++) {
        uint8_t* pixel = buffer + (i * 4);
        pixel[0] = 0xFF;  // Brightness
        pixel[1] = 0;     // B
        pixel[2] = 0;     // G
        pixel[3] = 0;     // R
    }
}

/**
 * Fill a range of pixels with solid color
 * Used by: SolidArms (fills each arm), potentially others
 */
inline void fillRange(uint8_t* buffer, uint16_t start, uint16_t count,
                      uint8_t r, uint8_t g, uint8_t b) {
    for (uint16_t i = 0; i < count; i++) {
        setPixelColorDirect(buffer, start + i, r, g, b);
    }
}

/**
 * Fill an arm (10 LEDs) with solid color
 * Used by: SolidArms (3 calls per frame)
 */
inline void fillArm(uint8_t* buffer, uint16_t armStart,
                    uint8_t r, uint8_t g, uint8_t b) {
    fillRange(buffer, armStart, 10, r, g, b);
}

/**
 * Additive color blend with saturation
 * Used by: VirtualBlobs (3 arms × ~10 LEDs), PerArmBlobs (3 arms × ~10 LEDs)
 * Found 6 times in codebase as inline RGB addition
 */
inline void blendAdditive(uint8_t& dstR, uint8_t& dstG, uint8_t& dstB,
                          uint8_t srcR, uint8_t srcG, uint8_t srcB) {
    dstR = std::min(255, dstR + srcR);
    dstG = std::min(255, dstG + srcG);
    dstB = std::min(255, dstB + srcB);
}

#endif
