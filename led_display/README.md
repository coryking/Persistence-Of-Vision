# POV LED Display Firmware

ESP32-S3 firmware for the spinning POV display. Renders visual effects in real-time based on rotation position.

## Hardware

| Component | Part | Purpose |
|-----------|------|---------|
| MCU | Seeed XIAO ESP32-S3 | Main processor |
| LEDs | HD107S × 33 | 3 arms, 11 LEDs each |
| IMU | MPU-9250 | Vibration/balance telemetry |
| Position | A3144 Hall Sensor | Rotation timing |

Pin assignments: See `include/hardware_config.h`

## Features

- **10+ visual effects** - Rainbow, Perlin noise, test patterns, and more
- **Operating range** - 700-2800 RPM
- **FreeRTOS architecture** - Hall sensor ISR with queue-based processing
- **ESP-NOW communication** - Receives IR commands from motor controller

## Build

```bash
uv sync
uv run pio run -e seeed_xiao_esp32s3
uv run pio run -e seeed_xiao_esp32s3 -t upload
```

## Architecture

```
src/
├── main.cpp              # Main render loop, FreeRTOS setup
├── TelemetryTask.cpp     # IMU sampling task
└── effects/              # Effect implementations

include/
├── hardware_config.h     # Pin definitions, LED counts
├── Effect.h              # Effect base class
├── EffectRegistry.h      # Effect switching
├── RevolutionTimer.h     # Hall sensor timing
├── SlotTiming.h          # Render slot calculation
└── types.h               # angle_t, interval_t
```

## Documentation

- [HARDWARE.md](../docs/led_display/HARDWARE.md) - Physical hardware details
- [TIMING_ANALYSIS.md](../docs/led_display/TIMING_ANALYSIS.md) - Why timing matters
- [POV_DISPLAY.md](../docs/led_display/POV_DISPLAY.md) - Design philosophy
- [BUILD.md](../docs/led_display/BUILD.md) - Build system details

## Development Notes

This firmware uses FastLED for color math but NeoPixelBus for SPI transfer. This combination is critical for timing - previous attempts with FastLED's SPI failed due to jitter. See [PROJECT_COMPARISON.md](../docs/led_display/PROJECT_COMPARISON.md) for the full analysis.
