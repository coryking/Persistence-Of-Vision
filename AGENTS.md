# POV Display System - Agent Guide

This is a unified mega-project for the POV (Persistence of Vision) display system.

## Project Structure

See **docs/PROJECT_STRUCTURE.md** for the complete, authoritative project layout.

**Quick overview:**

- **led_display/** - Main POV display firmware (ESP32-S3)
  - See led_display/AGENTS.md for LED display-specific guidance
  - Rotating LED arrays, effect system, FreeRTOS-based hall sensor processing

- **motor_controller/** - Motor controller firmware (ESP32-S3-Zero)
  - Simple open-loop PWM control, rotary encoder input, RGB LED status, IR receiver

- **shared/** - Shared headers for ESP-NOW communication between controllers

- **test_projects/** - Test and prototype projects

- **docs/** - Unified documentation (see PROJECT_STRUCTURE.md for details)

- **tools/** - Python CLI tools (`pov` command for telemetry capture, etc.)

## Build System

Both projects use **uv + PlatformIO**:

```bash
# LED display (ESP32-S3)
cd led_display
uv sync
uv run pio run -e seeed_xiao_esp32s3

# Motor controller (ESP32-S3-Zero)
cd motor_controller
uv sync
uv run pio run
```

## CLI Tools

The `pov` CLI provides telemetry capture and analysis tools:

```bash
# From project root
uv sync                     # Install CLI
pov telemetry status        # Check motor controller state
pov telemetry dump          # Download telemetry CSVs
```

All CLI commands support `--json` for structured output (useful for LLM integration).

## Future Integration

The motor controller and LED display communicate bidirectionally via ESP-NOW:
- IR remote control forwarding
- Telemetry exchange (RPM, status)
- Effect switching

See **docs/PROJECT_STRUCTURE.md** for shared message definitions and **docs/ir-control-spec.md** for IR control implementation.

## IMU Telemetry

The MPU-9250 IMU is mounted ~27mm from rotation center with double-stick foam tape.

**Intended orientation** (verify with measurements):
- **Y+**: Radial (outward from rotation center) - will saturate from centrifugal force
- **Z+**: Down toward motor shaft (axial, along rotation axis) - sees wobble
- **X+**: Roughly tangent to rotation, toward heavy wireless power board

The MPU-9250 provides both accelerometer (±16g) and gyroscope (±2000°/s) data. The Y axis (radial) will saturate from centrifugal force above ~720 RPM at 27mm radius. The X (tangent) and Z (axial) axes should remain usable throughout the operating range. The gyroscope provides rotation rate data that can complement the accelerometer for balance analysis.

See **docs/ROTOR_BALANCING.md** for balancing theory and physics.

## Philosophy

This is a hobby art project. The goal is to create beautiful spinning LED displays.

- Ship cool visual effects first
- Respect timing constraints (see docs/led_display/TIMING_ANALYSIS.md)
- Refactor when patterns emerge (rule of three)
- Keep it simple - this isn't enterprise software

For project-specific guidance, see:
- led_display/AGENTS.md - LED display development philosophy
- motor_controller/AGENTS.md - Motor controller development philosophy
