# ESP-NOW Communication Architecture

This document provides an overview of the ESP-NOW communication between the motor controller and LED display. For implementation details, refer to the source files.

## Message Flow

```
Motor Controller (ESP32-S3-Zero)       LED Display (ESP32-S3)
         │                                     │
         │  ─── SetEffectMsg ──────────────>   │
         │  ─── BrightnessUp/DownMsg ──────>   │
         │  ─── ResetRotorStatsMsg ─────────>  │
         │                                     │
         │  <─── RotorStatsMsg ────────────    │  (every 500ms)
         │  <─── AccelSamplesMsg ──────────    │  (when telemetry enabled)
         │  <─── HallEventMsg ─────────────    │  (when telemetry enabled)
         │                                     │
```

## Key Source Files

| Purpose | File |
|---------|------|
| Message definitions | `shared/messages.h` |
| Motor controller TX/RX | `motor_controller/src/espnow_comm.cpp` |
| LED display TX/RX | `led_display/src/ESPNowComm.cpp` |
| Effect/brightness state | `led_display/include/EffectManager.h` |
| Rotor diagnostics | `led_display/include/RotorDiagnosticStats.h` |

## State Ownership

The LED display owns effect and brightness state via `EffectManager`. Commands from the motor controller are sent as messages and queued for processing by the effect manager task.

See `EffectManager::getCommandQueue()` for the command queue pattern.

## ROTOR_STATS Parsing

The motor controller logs ROTOR_STATS to serial when receiving `RotorStatsMsg`. The Python CLI (`tools/pov_tools/rotor_stats.py`) can parse these lines to monitor display state.

Format:
```
ROTOR_STATS seq=N created=T updated=T hall=N ... effect=N brightness=N speedPreset=N pwm=N
```

The `speedPreset` (1-10) and `pwm` (0-255) fields are added by the motor controller from its local state.

See `motor_controller/src/espnow_comm.cpp:61` for the format string.
