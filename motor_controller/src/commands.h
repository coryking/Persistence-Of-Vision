#pragma once
#include <cstdint>

// BUTTON serial command reference:
// Use "BUTTON <n>" to trigger commands programmatically via serial.
// Values correspond to enum ordinals below (e.g., "BUTTON 10" = Effect10).
//
//   1-10: Effect1 through Effect10 (10 = calibration)
//   11: BrightnessUp, 12: BrightnessDown
//   13: PowerToggle (use MOTOR_ON/MOTOR_OFF for idempotent control instead)
//   14: SpeedUp, 15: SpeedDown
//   16: EffectRight, 17: EffectLeft
//   18: EffectUp, 19: EffectDown
// 20: EffectEnter
//   21: CaptureRecord, 22: CaptureStop, 23: CapturePlay, 24: CaptureDelete
//   25: StatsToggle
//   26: NextEffect, 27: PrevEffect

enum class Command : uint8_t {
    None = 0,
    Effect1, Effect2, Effect3, Effect4, Effect5,
    Effect6, Effect7, Effect8, Effect9, Effect10,
    BrightnessUp,
    BrightnessDown,
    PowerToggle,
    SpeedUp,
    SpeedDown,
    EffectRight,       // IR RIGHT button
    EffectLeft,        // IR LEFT button
    EffectUp,          // IR UP button
    EffectDown,        // IR DOWN button
    EffectEnter,       // IR ENTER button
    CaptureRecord,     // Start telemetry capture
    CaptureStop,       // Stop telemetry capture
    CapturePlay,       // Dump captured telemetry as CSV
    CaptureDelete,     // Delete captured telemetry files
    StatsToggle,       // Toggle stats overlay on display
    NextEffect,        // IR CH_UP button - cycle to next effect
    PrevEffect,        // IR CH_DOWN button - cycle to previous effect
};
