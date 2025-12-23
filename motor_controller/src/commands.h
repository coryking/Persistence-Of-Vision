#pragma once
#include <cstdint>

enum class Command : uint8_t {
    None = 0,
    Effect1, Effect2, Effect3, Effect4, Effect5,
    Effect6, Effect7, Effect8, Effect9, Effect10,
    BrightnessUp,
    BrightnessDown,
    PowerToggle,
    SpeedUp,
    SpeedDown,
};
