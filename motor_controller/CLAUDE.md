# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Simple open-loop motor controller for POV display: IR remote → PWM → L298N → motor.

No PID, no display, no FreeRTOS - just the essentials.

**Hardware**:
- Waveshare ESP32-S3-Zero
- L298N motor driver
- IR receiver (SageTV RC5 remote)
- RGB LED status indicator
- See `src/hardware_config.h` for pinout

## Build Commands

```bash
# Build (using uv)
cd motor_controller
uv sync
uv run pio run

# Upload to ESP32-S3-Zero
uv run pio run -t upload

# Monitor serial output
uv run pio device monitor

# Upload and monitor
uv run pio run -t upload && uv run pio device monitor

# Clean build
uv run pio run -t clean
```

## Architecture

Simple modular design with IR remote control and ESP-NOW communication:
- `setup()` - Initialize motor, IR receiver, LED, ESP-NOW
- `loop()` - Poll IR remote, process commands, update motor speed

**Control Scheme**:
- POWER button → Toggle motor on/off
- REWIND/FAST_FWD buttons → Decrease/increase speed (10 steps)
- Number buttons 1-9, 0 → Effect control (sent to LED display via ESP-NOW)
- VOL_UP/VOL_DOWN → Brightness control (sent to LED display via ESP-NOW)

**PWM Configuration**: 18kHz, 8-bit (validated to eliminate audible whine)

**Speed Mapping**: Linear map from position 1-10 to PWM 65%-80%
- 10 speed positions (no OFF position)
- Power button enables/disables motor (sets PWM to 0 when disabled)
- Always starts at position 1 (65% PWM) when powered on
- PWM range limited to 65%-80% for safety and reliable starting

## Hardware Configuration

All pins and constants are defined in `src/hardware_config.h`:

**Motor Driver (L298N)**:
- ENA (PWM) → GPIO9 (green wire)
- IN1 (direction) → GPIO7 (blue wire)
- IN2 (direction) → GPIO8 (yellow wire)

**IR Receiver**:
- Data → GPIO2 (orange wire)

**RGB LED Status Indicator** (active LOW, common anode):
- Red → GPIO17 (4.7kΩ resistor)
- Green → GPIO16 (11kΩ resistor)
- Blue → GPIO21 (2.2kΩ resistor)

## Files

- `src/hardware_config.h` - Pin definitions and PWM constants
- `src/main.cpp` - Main Arduino sketch (initialization and main loop)
- `src/motor_control.{h,cpp}` - L298N motor driver interface
- `src/motor_speed.{h,cpp}` - Speed state management (10 positions, on/off)
- `src/remote_input.{h,cpp}` - IR receiver (SageTV RC5 remote decoding)
- `src/commands.h` - Command enum (effects, brightness, power, speed)
- `src/command_processor.{h,cpp}` - Routes commands to motor or ESP-NOW
- `src/led_indicator.{h,cpp}` - RGB LED status (stopped/running)
- `src/espnow_comm.{h,cpp}` - ESP-NOW communication with LED display
- `platformio.ini` - Build configuration
- `../shared/sagetv_buttons.h` - SageTV remote button codes
- `../shared/messages.h` - ESP-NOW message structures
- `../shared/espnow_config.h` - ESP-NOW MAC addresses

## Notes

**PWM Frequency**: 18kHz was experimentally determined to be optimal:
- Below 10kHz → audible whine from PWM switching
- 18kHz → above human hearing range, no whine, no overheating
- Above 40kHz → excessive switching losses in L298N

**ESP-NOW Communication**: Bidirectional communication with LED display controller:
- Motor → Display: Reserved for future telemetry (RPM, status)
- Display ← Motor: Effect switching, brightness control via IR remote

**Previous Complexity**: This project was originally built with FreeRTOS, PID control, hall effect sensor feedback, OLED display, and rotary encoder control. All removed - this is now pure open-loop control with IR remote for simplicity.
