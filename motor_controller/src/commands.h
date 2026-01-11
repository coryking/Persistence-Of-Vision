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
//   16: EffectModeNext, 17: EffectModePrev
//   18: EffectParamUp, 19: EffectParamDown
//   20: CaptureRecord, 21: CaptureStop, 22: CapturePlay, 23: CaptureDelete

enum class Command : uint8_t {
    None = 0,
    Effect1, Effect2, Effect3, Effect4, Effect5,
    Effect6, Effect7, Effect8, Effect9, Effect10,
    BrightnessUp,
    BrightnessDown,
    PowerToggle,
    SpeedUp,
    SpeedDown,
    EffectModeNext,    // Cycle effect mode forward
    EffectModePrev,    // Cycle effect mode backward
    EffectParamUp,     // Effect parameter up
    EffectParamDown,   // Effect parameter down
    CaptureRecord,     // Start telemetry capture
    CaptureStop,       // Stop telemetry capture
    CapturePlay,       // Dump captured telemetry as CSV
    CaptureDelete,     // Delete captured telemetry files
};
