/**
 * @file sagetv_buttons.h
 * @brief SageTV RC5 remote button codes with toggle bit stripped
 *
 * These constants represent the RC5 command codes from the SageTV remote
 * with the toggle bit (bit 11, 0x800) already stripped. The original codes
 * alternate between 0x7xx and 0xFxx on successive presses; these constants
 * use the masked value (code & 0x07FF) for consistent comparison.
 *
 * Generated from: docs/led_display/sagetv_remote_mapping.json
 */

#ifndef SAGETV_BUTTONS_H
#define SAGETV_BUTTONS_H

#include <stdint.h>

// RC5 toggle bit mask - bit 11 toggles between button presses
#define RC5_TOGGLE_MASK 0x0800

// Mask to strip toggle bit and extract address + command
#define RC5_CODE_MASK 0x07FF

/**
 * @brief Strip the RC5 toggle bit from a received code
 * @param code The raw RC5 code from the decoder
 * @return The code with toggle bit cleared (for comparison)
 */
static inline uint16_t rc5StripToggleBit(uint16_t code) {
    return code & RC5_CODE_MASK;
}

// =============================================================================
// SageTV Remote Button Codes (toggle bit stripped)
// =============================================================================

// Power and Mode buttons
#define SAGETV_BTN_POWER        0x30C   // Red power button
#define SAGETV_BTN_TV           0x321
#define SAGETV_BTN_GUIDE        0x322
#define SAGETV_BTN_SEARCH       0x323
#define SAGETV_BTN_HOME         0x320
#define SAGETV_BTN_MUSIC        0x324
#define SAGETV_BTN_PHOTOS       0x325
#define SAGETV_BTN_VIDEOS       0x326
#define SAGETV_BTN_ONLINE       0x327

// Number pad
#define SAGETV_BTN_1            0x301
#define SAGETV_BTN_2            0x302
#define SAGETV_BTN_3            0x303
#define SAGETV_BTN_4            0x304
#define SAGETV_BTN_5            0x305
#define SAGETV_BTN_6            0x306
#define SAGETV_BTN_7            0x307
#define SAGETV_BTN_8            0x308
#define SAGETV_BTN_9            0x309
#define SAGETV_BTN_0            0x300
#define SAGETV_BTN_DASH         0x30A   // Hyphen/dash
#define SAGETV_BTN_ABC_123      0x30B   // Text input toggle

// Channel and volume
#define SAGETV_BTN_PREV_CH      0x329
#define SAGETV_BTN_AUDIO        0x328
#define SAGETV_BTN_MUTE         0x30D
#define SAGETV_BTN_CH_UP        0x312
#define SAGETV_BTN_CH_DOWN      0x313
#define SAGETV_BTN_VOL_UP       0x310
#define SAGETV_BTN_VOL_DOWN     0x311

// Navigation
#define SAGETV_BTN_OPTIONS      0x32A
#define SAGETV_BTN_INFO         0x315
#define SAGETV_BTN_BACK         0x314
#define SAGETV_BTN_UP           0x318
#define SAGETV_BTN_DOWN         0x31A
#define SAGETV_BTN_LEFT         0x31B
#define SAGETV_BTN_RIGHT        0x319
#define SAGETV_BTN_ENTER        0x31C

// Special function buttons
#define SAGETV_BTN_FAVORITE     0x317   // "F" symbol
#define SAGETV_BTN_WATCHED      0x316   // "M" symbol

// Playback control
#define SAGETV_BTN_PLAY         0x335
#define SAGETV_BTN_PAUSE        0x330
#define SAGETV_BTN_STOP         0x336
#define SAGETV_BTN_RECORD       0x337   // Red dot

// Seek and skip
#define SAGETV_BTN_SKIP_BACK    0x31D
#define SAGETV_BTN_REWIND       0x332
#define SAGETV_BTN_FAST_FWD     0x334
#define SAGETV_BTN_SKIP_FWD     0x31E
#define SAGETV_BTN_PREV_CHAPTER 0x32B   // |<<
#define SAGETV_BTN_SKIP_BACK2   0x32B   // Same as prev chapter
#define SAGETV_BTN_SKIP_FWD2    0x32C
#define SAGETV_BTN_DOT_TITLE    0x31F
#define SAGETV_BTN_NEXT_CHAPTER 0x31E   // >>| (same as skip forward)

// Additional functions
#define SAGETV_BTN_DELETE       0x32D
#define SAGETV_BTN_ASPECT       0x32E
#define SAGETV_BTN_VIDEO_OUT    0x32F
#define SAGETV_BTN_DVD_MENU     0x33A
#define SAGETV_BTN_DVD_RETURN   0x33B

#endif // SAGETV_BUTTONS_H
