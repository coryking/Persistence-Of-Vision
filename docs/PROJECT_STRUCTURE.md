# Project Structure

Authoritative reference for where to find things in the POV Display System codebase.

## Directory Overview

```
POV_Project/
├── shared/                     # Shared code between motor_controller and led_display
├── motor_controller/           # Motor controller firmware (ESP32-S3-Zero)
├── led_display/                # POV display firmware (ESP32-S3)
├── test_projects/              # Test and prototype projects
├── docs/                       # Documentation (see docs/README.md for full index)
└── tools/                      # Python CLI tools and analysis scripts
```

## Shared Code (`shared/`)

Headers used by both motor_controller and led_display for ESP-NOW communication:

```
shared/
├── messages.h          # MessageType enum and message structs (TelemetryMsg, SetEffectMsg, etc.)
├── espnow_config.h     # MAC addresses, WiFi channel, TX power settings
├── sagetv_buttons.h    # SageTV remote button codes (RC5)
└── types.h             # Shared type definitions
```

Both projects include this via `-I ../shared` in their platformio.ini build_flags.

## LED Display (`led_display/`)

ESP32-S3 firmware for the spinning POV display.

```
led_display/
├── src/
│   ├── main.cpp              # Entry point, FreeRTOS task creation
│   ├── RenderTask.cpp        # Core 1: effect rendering, angular resolution
│   ├── OutputTask.cpp        # Core 0: gamma correction, DMA output to LEDs
│   ├── BufferManager.cpp     # Double-buffer coordination between render/output
│   ├── HallEffectDriver.cpp  # Hall sensor ISR and timing
│   ├── HallSimulator.cpp     # Fake hall sensor for bench testing
│   ├── ESPNowComm.cpp        # ESP-NOW receive handler
│   ├── Imu.cpp               # MPU-9250 IMU driver
│   ├── TelemetryTask.cpp     # IMU sampling task
│   ├── FrameProfiler.cpp     # Pipeline timing instrumentation
│   ├── RotorDiagnosticStats.cpp  # Rotor health statistics
│   └── effects/              # 13 visual effects (see EFFECT_SYSTEM_DESIGN.md)
├── include/
│   ├── hardware_config.h     # Pin definitions, LED counts, timing constants
│   ├── Effect.h              # Effect base class
│   ├── EffectManager.h       # Effect switching and command routing
│   ├── RenderContext.h       # Data passed to effects each frame
│   ├── RenderTask.h          # Render task interface
│   ├── OutputTask.h          # Output task interface
│   ├── BufferManager.h       # Double-buffer management
│   ├── RevolutionTimer.h     # Hall sensor timing, rolling average
│   ├── SlotTiming.h          # Render slot calculation
│   ├── HallEffectDriver.h    # Hall sensor driver
│   ├── HallSimulator.h       # Bench testing fake hall
│   ├── ESPNowComm.h          # ESP-NOW interface
│   ├── Imu.h                 # IMU interface
│   ├── geometry.h            # RadialGeometry: ring positions, arm stagger
│   ├── polar_helpers.h       # Angle math, noise helpers, coordinate utilities
│   ├── pixel_utils.h         # Pixel manipulation helpers
│   ├── types.h               # angle_t, interval_t, key constants
│   └── effects/              # Effect headers
├── data/
│   └── sagetv_remote_mapping.json  # IR button code reference
└── platformio.ini            # Build config (ESP32-S3, pioarduino platform)
```

**Hardware:** Seeed XIAO ESP32-S3, HD107S LEDs (3 arms: 14 + 13 + 13 = 40 logical rings), A3144 hall sensor, MPU-9250 IMU

## Motor Controller (`motor_controller/`)

ESP32-S3-Zero firmware for motor speed control and IR remote reception.

```
motor_controller/
├── src/
│   ├── main.cpp              # Main loop, setup
│   ├── hardware_config.h     # Pin definitions (authoritative for motor controller hardware)
│   ├── motor_control.h/cpp   # PWM motor control via L298N
│   ├── motor_speed.h/cpp     # Speed presets and adjustment
│   ├── remote_input.h/cpp    # IR remote input handling (SageTV RC5)
│   ├── commands.h            # Command enum (effects, brightness, power, speed)
│   ├── command_processor.h/cpp  # Routes commands to motor or ESP-NOW
│   ├── led_indicator.h/cpp   # RGB status LED
│   ├── espnow_comm.h/cpp     # ESP-NOW communication with LED display
│   ├── serial_command.h/cpp  # Serial command interface
│   └── telemetry_capture.h/cpp  # High-rate IMU data capture to flash
├── partitions.csv            # Custom partition table with telemetry storage
└── platformio.ini            # Build config (ESP32-S3-Zero, pioarduino platform)
```

**Hardware:** Waveshare ESP32-S3-Zero, L298N motor driver, VS1838B IR receiver, RGB LED

## Test Projects (`test_projects/`)

Standalone test utilities, not part of main firmware:

```
test_projects/
├── hall_effect_test/         # LED strip + hall sensor hardware validation
├── ir_remote_test/           # IR remote button code dumper
├── accelerometer_test/       # IMU sensor validation
├── hd_gamma_test/            # FastLED HD gamma mode testing
├── led_test/                 # LED performance benchmarking (FastLED vs NeoPixelBus)
├── basic_led_test/           # Minimal LED strip test
└── mac_address_test/         # ESP32 MAC address discovery for ESP-NOW
```

## Tools (`tools/`)

```
tools/
├── pov_tools/                # `pov` CLI package
│   ├── cli.py                # Click CLI entry point
│   ├── telemetry/            # pov telemetry {status,dump,start,stop,delete,...}
│   ├── analysis/             # pov analyze — IMU data analysis pipeline
│   ├── calibration/          # pov calibrate — motor speed calibration
│   └── serial_comm.py        # Serial communication utilities
├── analysis/                 # Standalone analysis scripts and reports
├── calibration/              # Calibration data, model, generated lookup table
└── convert_to_polar.py       # Equirectangular → polar texture converter
```

## Documentation (`docs/`)

See **docs/README.md** for the complete document index with descriptions.

## Key Files Quick Reference

| What | Where |
|------|-------|
| Motor controller pins | `motor_controller/src/hardware_config.h` |
| LED display pins | `led_display/include/hardware_config.h` |
| ESP-NOW messages | `shared/messages.h` |
| MAC addresses | `shared/espnow_config.h` |
| Effect base class | `led_display/include/Effect.h` |
| Effect switching | `led_display/include/EffectManager.h` |
| IR button codes | `led_display/data/sagetv_remote_mapping.json` |
| Timing constants | `led_display/include/types.h` |
| Ring geometry | `led_display/include/geometry.h` |
| Polar math helpers | `led_display/include/polar_helpers.h` |

## ESP-NOW Communication

| What | Where |
|------|-------|
| Message types and structs | `shared/messages.h` |
| MAC addresses / channel | `shared/espnow_config.h` |
| Motor controller ESP-NOW | `motor_controller/src/espnow_comm.cpp` |
| LED display ESP-NOW | `led_display/src/ESPNowComm.cpp` |
| Architecture overview | `docs/ESP-NOW_ARCHITECTURE.md` |

## Build System

All projects use **uv + PlatformIO**:

```bash
cd <project_dir>
uv sync                 # Install dependencies (first time)
uv run pio run          # Build
uv run pio run -t upload  # Upload
```

See project-specific AGENTS.md files for detailed build instructions.
