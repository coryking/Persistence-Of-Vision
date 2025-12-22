# POV Display System

A Persistence of Vision (POV) display system with rotating LED arrays and motor control.

## System Components

### LED Display (ESP32-S3)
- Rotating LED arrays (3 arms, 11 LEDs per arm)
- Hall effect sensor for rotation tracking
- Effect system with 10+ visual effects
- FreeRTOS-based architecture
- See led_display/README.md

### Motor Controller (RP2040)
- L298N motor driver control
- Rotary encoder for speed adjustment
- RGB LED status indicator
- PWM-based speed control
- See motor_controller/CLAUDE.md

## Documentation

- docs/led_display/ - LED display architecture, timing analysis, effect system
- docs/motor_controller/ - Motor control, calibration, datasheets
- docs/integration/ - Future integration planning

## Build Instructions

Both projects use uv + PlatformIO. See AGENTS.md for build commands.

## Project Status

- LED Display: Operational, 10+ effects, 700-2800 RPM range
- Motor Controller: Operational, 240-1489 RPM range
- Integration: Planned (IR communication, telemetry exchange)
