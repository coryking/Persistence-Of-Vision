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
#define SAGETV_BTN_POWER        0x70C   // Red power button
#define SAGETV_BTN_TV           0x721
#define SAGETV_BTN_GUIDE        0x722
#define SAGETV_BTN_SEARCH       0x723
#define SAGETV_BTN_HOME         0x720
#define SAGETV_BTN_MUSIC        0x724
#define SAGETV_BTN_PHOTOS       0x725
#define SAGETV_BTN_VIDEOS       0x726
#define SAGETV_BTN_ONLINE       0x727

// Number pad
#define SAGETV_BTN_1            0x701
#define SAGETV_BTN_2            0x702
#define SAGETV_BTN_3            0x703
#define SAGETV_BTN_4            0x704
#define SAGETV_BTN_5            0x705
#define SAGETV_BTN_6            0x706
#define SAGETV_BTN_7            0x707
#define SAGETV_BTN_8            0x708
#define SAGETV_BTN_9            0x709
#define SAGETV_BTN_0            0x700
#define SAGETV_BTN_DASH         0x70A   // Hyphen/dash
#define SAGETV_BTN_ABC_123      0x70B   // Text input toggle

// Channel and volume
#define SAGETV_BTN_PREV_CH      0x729
#define SAGETV_BTN_AUDIO        0x728
#define SAGETV_BTN_MUTE         0x70D
#define SAGETV_BTN_CH_UP        0x712
#define SAGETV_BTN_CH_DOWN      0x713
#define SAGETV_BTN_VOL_UP       0x710
#define SAGETV_BTN_VOL_DOWN     0x711

// Navigation
#define SAGETV_BTN_OPTIONS      0x72A
#define SAGETV_BTN_INFO         0x715
#define SAGETV_BTN_BACK         0x714
#define SAGETV_BTN_UP           0x718
#define SAGETV_BTN_DOWN         0x71A
#define SAGETV_BTN_LEFT         0x71B
#define SAGETV_BTN_RIGHT        0x719
#define SAGETV_BTN_ENTER        0x71C

// Special function buttons
#define SAGETV_BTN_FAVORITE     0x717   // "F" symbol
#define SAGETV_BTN_WATCHED      0x716   // "M" symbol

// Playback control
#define SAGETV_BTN_PLAY         0x735
#define SAGETV_BTN_PAUSE        0x730
#define SAGETV_BTN_STOP         0x736
#define SAGETV_BTN_RECORD       0x737   // Red dot

// Seek and skip
#define SAGETV_BTN_SKIP_BACK    0x71D
#define SAGETV_BTN_REWIND       0x732
#define SAGETV_BTN_FAST_FWD     0x734
#define SAGETV_BTN_SKIP_FWD     0x71E
#define SAGETV_BTN_PREV_CHAPTER 0x72B   // |<<
#define SAGETV_BTN_SKIP_BACK2   0x72B   // Same as prev chapter
#define SAGETV_BTN_SKIP_FWD2    0x72C
#define SAGETV_BTN_DOT_TITLE    0x71F
#define SAGETV_BTN_NEXT_CHAPTER 0x71E   // >>| (same as skip forward)

// Additional functions
#define SAGETV_BTN_DELETE       0x72D
#define SAGETV_BTN_ASPECT       0x72E
#define SAGETV_BTN_VIDEO_OUT    0x72F
#define SAGETV_BTN_DVD_MENU     0x73A
#define SAGETV_BTN_DVD_RETURN   0x73B

#endif // SAGETV_BUTTONS_H
