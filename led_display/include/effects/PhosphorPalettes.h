#ifndef PHOSPHOR_PALETTES_H
#define PHOSPHOR_PALETTES_H

#include <FastLED.h>

/**
 * Phosphor Palette Generation
 *
 * Generates CRGBPalette256 palettes using actual phosphor decay physics.
 * See docs/effects/math-for-phosphor.md for the underlying math.
 * See docs/effects/authentic-ppi-radar-display-physics.md for color reference.
 *
 * Decay formulas:
 * - Exponential: I(t) = I₀ × exp(-t/τ)  (P1, P12)
 * - Inverse power law: I(t) = I₀ / (1 + t/τ)^n  (P7, P19)
 *
 * Phosphor colors (CHSV hue values):
 *   P7  = Cascade: blue-white → yellow-green (CRGB blend)
 *   P12 = Orange (590nm), hue 32
 *   P19 = Orange (595nm), hue 30
 *   P1  = Green (525nm), hue 96
 */

namespace PhosphorPalettes {

/**
 * Generate all phosphor palettes (4 types × 2 palettes each)
 *
 * @param blipPalettes   Output array of 4 palettes for blips (full brightness)
 * @param sweepPalettes  Output array of 4 palettes for sweep trail (~35% brightness)
 *
 * Palette indices match PhosphorType enum:
 *   0 = P7_BLUE_YELLOW
 *   1 = P12_ORANGE
 *   2 = P19_ORANGE_LONG
 *   3 = P1_GREEN
 */
void generateAll(CRGBPalette256 blipPalettes[4], CRGBPalette256 sweepPalettes[4]);

} // namespace PhosphorPalettes

#endif // PHOSPHOR_PALETTES_H
