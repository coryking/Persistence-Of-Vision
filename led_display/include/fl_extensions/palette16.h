#pragma once

#include "fl_extensions/crgb16.h"
#include <FastLED.h>

/**
 * @brief 16-bit palette lookup functions
 *
 * These functions read 8-bit palette entries (CRGBPalette16, CRGBPalette256),
 * promote them to 16-bit, and interpolate with 16-bit precision.
 *
 * Palette declarations don't change - DEFINE_GRADIENT_PALETTE and friends
 * are still 8-bit. These functions provide the 16-bit output path.
 *
 * Part of fl_extensions - FastLED extensions designed as future PR candidates.
 */

/**
 * @brief Get 16-bit color from 16-entry palette
 *
 * @param pal Palette (8-bit entries)
 * @param index 16-bit index:
 *              - High 4 bits = palette entry (0-15)
 *              - Low 12 bits = blend fraction (0-4095)
 * @param brightness Scale factor (0-255), default 255
 * @param blendType LINEARBLEND or NOBLEND
 * @return CRGB16 color (16-bit precision)
 */
CRGB16 ColorFromPalette16(const CRGBPalette16& pal, uint16_t index,
                          uint8_t brightness = 255,
                          TBlendType blendType = LINEARBLEND);

/**
 * @brief Get 16-bit color from 256-entry palette
 *
 * @param pal Palette (8-bit entries)
 * @param index 16-bit index:
 *              - High 8 bits = palette entry (0-255)
 *              - Low 8 bits = blend fraction (0-255)
 * @param brightness Scale factor (0-255), default 255
 * @param blendType LINEARBLEND or NOBLEND
 * @return CRGB16 color (16-bit precision)
 */
CRGB16 ColorFromPalette16(const CRGBPalette256& pal, uint16_t index,
                          uint8_t brightness = 255,
                          TBlendType blendType = LINEARBLEND);
