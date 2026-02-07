# POV LED Display Firmware

ESP32-S3 firmware for a spinning Persistence of Vision display. Three LED arms rotate at 700-2800 RPM, rendering visual effects in real-time based on rotation position.

## Hardware

| Component | Part | Purpose |
|-----------|------|---------|
| MCU | Seeed XIAO ESP32-S3 | Main processor (dual-core, 240MHz) |
| LEDs | HD107S × 40 | 3 arms (14 + 13 + 13), SPI interface |
| IMU | MPU-9250 | Vibration/balance telemetry |
| Position | A3144 Hall Sensor | Revolution timing |

Pin assignments: `include/hardware_config.h`

## Effects

13 visual effects, switchable via IR remote:

| Effect | Description |
|--------|-------------|
| NoiseField | Perlin noise with palette cycling (12 palettes) |
| Radar | CRT radar simulation with phosphor decay physics |
| PolarGlobe | Planet textures rendered from polar projections |
| CartesianGrid | Grid pattern overlay in Cartesian space |
| VirtualBlobs | Floating color blobs that span all arms |
| PerArmBlobs | Blobs constrained to individual arms |
| PulseChaser | Radial pulses chasing outward |
| MomentumFlywheel | Energy-responsive color wheel |
| SolidArms | Solid color patterns per arm |
| RpmArc | Arc width scales with RPM |
| ProjectionTest | Banded projection test pattern |
| ArmAlignment | Arm alignment calibration utility |
| CalibrationEffect | LED ring identification utility |

## Build

```bash
uv sync
uv run pio run -e seeed_xiao_esp32s3           # Build
uv run pio run -e seeed_xiao_esp32s3 -t upload  # Upload
```

## Architecture

Dual-core FreeRTOS pipeline:
- **Core 1 (Render)** — Hall sensor timing → angular resolution → effect rendering
- **Core 0 (Output)** — Double-buffer swap → gamma correction → SPI DMA to LEDs

Uses FastLED for color math and NeoPixelBus for SPI output. See [POV_PROJECT_ARCHITECTURE_LESSONS.md](../docs/led_display/POV_PROJECT_ARCHITECTURE_LESSONS.md) for why this split matters.

## Documentation

Full index: [docs/README.md](../docs/README.md)

Key docs:
- [EFFECT_SYSTEM_DESIGN.md](../docs/led_display/EFFECT_SYSTEM_DESIGN.md) — Effect API reference
- [HARDWARE.md](../docs/led_display/HARDWARE.md) — Physical hardware details
- [TIMING_ANALYSIS.md](../docs/led_display/TIMING_ANALYSIS.md) — Why timing matters
- [COORDINATE_SYSTEMS.md](../docs/led_display/COORDINATE_SYSTEMS.md) — Polar math and projections
