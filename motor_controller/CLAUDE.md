# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Simple open-loop motor controller for POV display: rotary encoder → PWM → L298N → motor.

No PID, no display, no FreeRTOS - just the essentials.

**Hardware**:
- Seeed Studio XIAO RP2040
- L298N motor driver
- Rotary encoder
- See `src/hardware_config.h` and `docs/xiao-rp2040.webp` for pinout

## Build Commands

```bash
# Build
pio run

# Upload to RP2040
pio run -t upload

# Monitor serial output
pio device monitor

# Upload and monitor
pio run -t upload && pio device monitor

# Clean build
pio run -t clean
```

## Architecture

Dead simple Arduino sketch (~80 lines total):
- `setup()` - Configure pins, set PWM to 25kHz
- `loop()` - Poll encoder, map position to PWM, drive motor

**PWM Configuration**: 25kHz, 8-bit (validated to eliminate audible whine without overheating L298N)

**Encoder Mapping**: Linear map from position 0-40 to PWM 0%-77.9%
- Position 0 = motor OFF (0% PWM)
- Positions 1-40 = ~700-2900 RPM (based on calibration data)

**Calibration Model** (for reference):
```
RPM = -8170.97 + 205.2253*PWM - 0.809611*PWM²
R² = 0.9797 (excellent fit)
Valid range: 51-79% PWM → 240-3006 RPM
```

This quadratic model accounts for air resistance (drag ∝ speed²).

## Hardware Configuration

All pins and constants are defined in `src/hardware_config.h`:

**Motor Driver (L298N)**:
- ENA (PWM) → D10 (green wire)
- IN1 (direction) → D8 (blue wire)
- IN2 (direction) → D9 (yellow wire)

**Rotary Encoder**:
- CLK → D6 (green wire)
- DT → D5 (blue wire)
- SW → D4 (yellow wire, unused)

See `docs/xiao-rp2040.webp` for XIAO RP2040 pinout diagram.

## Files

- `src/hardware_config.h` - Pin definitions and calibration constants
- `src/main.cpp` - Main Arduino sketch
- `platformio.ini` - Build configuration
- `docs/xiao-rp2040.webp` - Pinout diagram

## Notes

**PWM Frequency**: 25kHz was experimentally determined to be optimal:
- Below 10kHz → audible whine from PWM switching
- 25kHz → above human hearing range, no whine, no overheating
- Above 40kHz → excessive switching losses in L298N

**Previous Complexity**: This project was originally built with FreeRTOS, PID control, hall effect sensor feedback, and OLED display. All removed - this is now pure open-loop control for simplicity.
