# POV Display System - Agent Guide

This is a unified mega-project for the POV (Persistence of Vision) display system.

## Project Structure

See **docs/PROJECT_STRUCTURE.md** for the complete, authoritative project layout.

**Quick overview:**

- **led_display/** - Main POV display firmware (ESP32-S3)
  - See led_display/AGENTS.md for LED-specific coding rules, pipeline architecture, what-not-to-break
  - Rotating LED arrays, effect system, FreeRTOS-based hall sensor processing

- **motor_controller/** - Motor controller firmware (ESP32-S3-Zero)
  - See motor_controller/AGENTS.md for motor-specific guidance
  - Simple open-loop PWM control, RGB LED status, IR receiver

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

## ESP-NOW Integration

The motor controller and LED display communicate bidirectionally via ESP-NOW:
- IR remote control forwarding (motor controller → LED display)
- Effect switching and brightness control
- Telemetry capture (IMU data from LED display → motor controller flash storage)

See **docs/ESP-NOW_ARCHITECTURE.md** for protocol details, **shared/messages.h** for message definitions, and **docs/ir-control-spec.md** for IR control.

## IMU Telemetry

See **docs/led_display/HARDWARE.md** for IMU mounting orientation and sensor configuration.

The Y axis (radial) saturates from centrifugal force at operating speeds. The X (tangent) and Z (axial) axes remain usable. The gyroscope X/Y axes measure wobble/precession.

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
