# Project Structure

This is the authoritative reference for where to find things in the POV Display System codebase.

## Directory Overview

```
POV_Project/
├── shared/                     # Shared code between motor_controller and led_display
├── motor_controller/           # Motor controller firmware (ESP32-S3-Zero)
├── led_display/                # POV display firmware (ESP32-S3)
├── test_projects/              # Test and prototype projects
├── docs/                       # Documentation
└── tools/                      # Scripts and utilities
```

## Shared Code (`shared/`)

Headers used by both motor_controller and led_display for ESP-NOW communication:

```
shared/
├── messages.h          # MessageType enum and message structs (TelemetryMsg, SetEffectMsg, etc.)
└── espnow_config.h     # MAC addresses, WiFi channel, TX power settings
```

Both projects include this via `-I ../shared` in their platformio.ini build_flags.

**Usage:**
```cpp
#include "messages.h"
#include "espnow_config.h"
```

## Motor Controller (`motor_controller/`)

ESP32-S3-Zero firmware for motor speed control and IR remote reception.

```
motor_controller/
├── src/
│   ├── main.cpp              # Main loop, setup
│   ├── hardware_config.h     # Pin definitions (authoritative for motor controller hardware)
│   ├── motor_control.h/cpp   # PWM motor control via L298N
│   ├── encoder_control.h/cpp # Rotary encoder input
│   └── led_indicator.h/cpp   # RGB status LED
└── platformio.ini            # Build config (ESP32-S3-Zero, pioarduino platform)
```

**Hardware:** ESP32-S3-Zero, L298N motor driver, rotary encoder, VS1838B IR receiver

## LED Display (`led_display/`)

ESP32-S3 firmware for the spinning POV display.

```
led_display/
├── src/
│   ├── main.cpp              # Main render loop, FreeRTOS task setup
│   └── effects/              # Effect implementations
├── include/
│   ├── hardware_config.h     # Pin definitions, LED counts, timing constants
│   ├── Effect.h              # Base class for effects
│   ├── EffectRegistry.h      # Effect registration and switching
│   ├── RenderContext.h       # Data passed to effects each frame
│   ├── RevolutionTimer.h     # Hall sensor timing, rolling average calculation
│   ├── SlotTiming.h          # Render slot calculation, copyPixelsToStrip()
│   ├── RollingAverage.h      # Circular buffer for smoothing
│   └── types.h               # angle_t, interval_t, key constants
└── platformio.ini            # Build config (ESP32-S3, pioarduino platform)
```

**Hardware:** Seeed XIAO ESP32-S3, SK9822 LEDs (3 arms × 11 LEDs), hall effect sensor, ADXL345 accelerometer

## Test Projects (`test_projects/`)

Standalone test utilities, not part of main firmware:

```
test_projects/
├── ir_remote_test/           # IR remote button mapping utility (reference for IR code)
├── led_display_test/         # Hardware validation for LED strips and hall sensors
└── led_test/                 # Performance benchmarking (FastLED vs NeoPixelBus)
```

## Documentation (`docs/`)

```
docs/
├── PROJECT_STRUCTURE.md      # This file - authoritative structure reference
├── ir-control-spec.md        # IR remote control implementation spec
├── led_display/              # LED display-specific docs
│   ├── HARDWARE.md           # Physical hardware (power system, components, sensors)
│   ├── TIMING_ANALYSIS.md    # Jitter analysis, why NeoPixelBus matters
│   ├── POV_DISPLAY.md        # Art-first philosophy, polar coordinates
│   ├── PROJECT_COMPARISON.md # Why queue-based hall sensor works
│   ├── FREERTOS_INTEGRATION.md
│   └── sagetv_remote_mapping.json  # IR button codes
├── motor_controller/         # Motor controller docs
└── datasheets/               # Hardware datasheets
```

## Tools (`tools/`)

```
tools/
├── calibration/              # Motor calibration scripts
└── analysis/                 # Performance analysis tools
```

## Key Files Quick Reference

| What | Where |
|------|-------|
| Motor controller pins | `motor_controller/src/hardware_config.h` |
| LED display pins | `led_display/include/hardware_config.h` |
| ESP-NOW messages | `shared/messages.h` |
| MAC addresses | `shared/espnow_config.h` |
| Effect base class | `led_display/include/Effect.h` |
| Effect switching | `led_display/include/EffectRegistry.h` |
| IR button codes | `docs/led_display/sagetv_remote_mapping.json` |
| Timing constants | `led_display/include/types.h` |

## Build System

All projects use **uv + PlatformIO**:

```bash
cd <project_dir>
uv sync                 # Install dependencies (first time)
uv run pio run          # Build
uv run pio run -t upload  # Upload
```

See project-specific CLAUDE.md files for detailed build instructions.
