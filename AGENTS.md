# POV Display System - Agent Guide

This is a unified mega-project for the POV (Persistence of Vision) display system.

## Project Structure

- **led_display/** - Main POV display firmware (ESP32-S3)
  - See led_display/AGENTS.md for LED display-specific guidance
  - Rotating LED arrays, effect system, FreeRTOS-based hall sensor processing

- **motor_controller/** - Motor controller firmware (RP2040)
  - See motor_controller/AGENTS.md for motor controller-specific guidance
  - Simple open-loop PWM control, rotary encoder input, RGB LED status

- **test_projects/** - Test and prototype projects
  - ir_remote_test/ - IR remote button mapping utility
  - led_display_test/ - Hardware validation for LED strips and hall sensors
  - led_test/ - Performance benchmarking (FastLED vs NeoPixelBus)

- **docs/** - Unified documentation
  - led_display/ - LED display-specific docs (timing, effects, architecture)
  - motor_controller/ - Motor controller docs (calibration, datasheets)
  - datasheets/ - Shared hardware datasheets
  - integration/ - Future: IR communication, telemetry protocols

- **tools/** - Shared scripts and utilities
  - calibration/ - Motor calibration scripts
  - analysis/ - Performance analysis tools

## Build System

Both projects use **uv + PlatformIO**:

```bash
# LED display (ESP32-S3)
cd led_display
uv sync
uv run pio run -e seeed_xiao_esp32s3

# Motor controller (RP2040)
cd motor_controller
uv sync
uv run pio run
```

## Future Integration

The motor controller and LED display will communicate bidirectionally:
- IR communication for control
- Telemetry exchange (RPM, status)
- Playlist synchronization

See docs/integration/ for integration planning (TBD).

## Philosophy

This is a hobby art project. The goal is to create beautiful spinning LED displays.

- Ship cool visual effects first
- Respect timing constraints (see docs/led_display/TIMING_ANALYSIS.md)
- Refactor when patterns emerge (rule of three)
- Keep it simple - this isn't enterprise software

For project-specific guidance, see:
- led_display/AGENTS.md - LED display development philosophy
- motor_controller/AGENTS.md - Motor controller development philosophy
