#ifndef PHOSPHOR_PALETTES_H
#define PHOSPHOR_PALETTES_H

#include <FastLED.h>

/**
 * Phosphor Palette Generation
 *
 * Generates CRGBPalette256 palettes using actual phosphor decay physics.
 * See docs/effects/math-for-phosphor.md for the underlying math.
 *
 * Decay formulas:
 * - Exponential: I(t) = I₀ × exp(-t/τ)  (P1, P12)
 * - Inverse power law: I(t) = I₀ / (1 + t/τ)^n  (P7, P19)
 *
 * DEBUG MODE: Using distinct hues to verify decay physics:
 *   P7  = Cyan (128)    - authentic: blue→yellow cascade
 *   P12 = Magenta (192) - authentic: orange
 *   P19 = Lime (96)     - authentic: orange
 *   P1  = Red (0)       - authentic: green
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
