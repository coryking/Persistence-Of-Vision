# Motor Controller — Agent Guide

Simple open-loop motor controller for the POV display: IR remote → PWM → L298N → motor.

For project-wide philosophy and structure, see the root AGENTS.md.

## Hardware

- Waveshare ESP32-S3-Zero
- L298N motor driver
- IR receiver (SageTV RC5 remote)
- RGB LED status indicator
- See `src/hardware_config.h` for all pin assignments

## Build

```bash
cd motor_controller
uv sync
uv run pio run                    # Build
uv run pio run -t upload          # Upload
uv run pio device monitor         # Monitor
```

## Architecture

Simple modular design — `setup()` initializes everything, `loop()` polls IR and processes commands.

**Control Scheme**:
- POWER → Toggle motor on/off
- REWIND/FAST_FWD → Decrease/increase speed (10 presets, 65%-80% PWM)
- Number buttons 1-9, 0 → Effect control (sent to LED display via ESP-NOW)
- VOL_UP/VOL_DOWN → Brightness control (sent to LED display via ESP-NOW)

**PWM**: 18kHz, 8-bit — experimentally determined to eliminate audible whine without excessive switching losses.

## Files

- `src/hardware_config.h` — Pin definitions and PWM constants
- `src/main.cpp` — Initialization and main loop
- `src/motor_control.{h,cpp}` — L298N motor driver interface
- `src/motor_speed.{h,cpp}` — Speed state management (10 presets, on/off)
- `src/remote_input.{h,cpp}` — IR receiver (SageTV RC5 remote decoding)
- `src/commands.h` — Command enum (effects, brightness, power, speed)
- `src/command_processor.{h,cpp}` — Routes commands to motor or ESP-NOW
- `src/led_indicator.{h,cpp}` — RGB LED status (stopped/running)
- `src/espnow_comm.{h,cpp}` — ESP-NOW communication with LED display
- `src/telemetry_capture.{h,cpp}` — Telemetry capture to flash partitions (FreeRTOS task)

## ESP-NOW Communication

Bidirectional with LED display controller:
- Motor → Display: Effect switching, brightness control via IR remote
- Display → Motor: Reserved for future telemetry (RPM, status)

## Telemetry Capture

Captures high-rate IMU data from the LED display to dedicated flash partitions for offline analysis. Uses raw partition writes (bypassing filesystem overhead) for 8kHz IMU data rates.

**Control**: IR remote (RECORD/STOP/DELETE buttons), serial commands, or `pov telemetry <command>` CLI.

**Important**: Must call DELETE before START — erase (~5s) is separate from start (instant).

See `docs/motor_controller/telemetry_capture.md` for architecture and protocol details.

## History

This project was originally built with FreeRTOS tasks, PID control, hall effect sensor feedback, OLED display, and rotary encoder control. All removed in favor of pure open-loop control with IR remote.
