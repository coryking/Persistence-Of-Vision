# POV Display System

A hobby art project creating beautiful spinning LED displays using Persistence of Vision.

[![Perlin Noise Effect](docs/photos/tiny-square-perlin-noise.gif)](docs/photos/perlin-noise-in-action.mp4)

## What is This?

A Persistence of Vision (POV) display creates images by spinning LEDs fast enough that your eye blends them into a continuous picture. This project combines:

- **Rotating LED arrays** - 3 arms with 11 LEDs each, creating a 33-row radial display
- **10+ visual effects** - Rainbow spirals, Perlin noise, test patterns, and more
- **Real-time rendering** - Calculates what to display based on rotation angle
- **Wireless power** - Inductive coil eliminates slip rings

## Hardware

### LED Display Controller (ESP32-S3)

| Component | Part |
|-----------|------|
| MCU | Seeed XIAO ESP32-S3 |
| LEDs | HD107S (3 arms × 11 LEDs) |
| IMU | MPU-9250 (accelerometer/gyroscope) |
| Position Sensor | A3144 Hall Effect Sensor |
| Power | 5V wireless inductive coil |

### Motor Controller (ESP32-S3-Zero)

| Component | Part |
|-----------|------|
| MCU | ESP32-S3-Zero |
| Motor Driver | L298N H-Bridge |
| Input | VS1838B IR Receiver |
| Status | RGB LED indicator |
| Communication | ESP-NOW (bidirectional with LED display) |

## CAD Model

Full mechanical design available on OnShape:

**[View CAD Model](https://cad.onshape.com/documents/9121dfb4ae185f6f80ac6826/w/f29456fd14a9541dc030c003/e/1b6eccbacd2d33295284267a)**

## Quick Start

Both projects use **uv + PlatformIO**:

```bash
# LED Display
cd led_display
uv sync
uv run pio run -e seeed_xiao_esp32s3
uv run pio run -e seeed_xiao_esp32s3 -t upload

# Motor Controller
cd motor_controller
uv sync
uv run pio run
uv run pio run -t upload
```

### CLI Tools

```bash
# From project root
uv sync
pov telemetry status    # Check motor controller state
pov telemetry dump      # Download telemetry CSVs
```

## Project Status

| System | Status | Operating Range |
|--------|--------|-----------------|
| LED Display | Operational | 700-2800 RPM |
| Motor Controller | Operational | 240-1489 RPM |
| ESP-NOW Integration | Operational | IR forwarding, telemetry exchange |

## Documentation

- [Project Structure](docs/PROJECT_STRUCTURE.md) - Codebase layout and key files
- [LED Display Docs](docs/led_display/) - Architecture, timing analysis, effect system
- [Motor Controller Docs](docs/motor_controller/) - Calibration, datasheets
- [IR Control Spec](docs/ir-control-spec.md) - Remote control implementation

## Photos

![POV Display Wireless Power Coil](docs/photos/POV%20Display%20Wireless%20Coil.jpeg)

![Old Display Insides](docs/photos/Old%20Display%20Insides.jpeg)

![Old POV Insides](docs/photos/Old%20POV%20Insides.jpeg)
