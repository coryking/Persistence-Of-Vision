# LED Display Rotor Hardware

Physical hardware documentation for the spinning POV display rotor.

**Pin assignments:** See `led_display/include/hardware_config.h`

## Physical Assembly

**Rotation direction:** Counter-clockwise when viewed from above (looking down at LEDs). This is software-configurable via motor controller.

**Rotor mass:** 152g (fully assembled spinning mass: Rotor Base + Rotor Lid + 3 screws + grub screw)

**Arm length:** 104.5mm from center to tip

### Enclosure Structure

The rotor enclosure consists of two 3D-printed parts (Onshape naming):

- **Rotor Base** (top part): LEDs mounted on top surface; internal cavity houses electronics (ESP32, IMU, buck converter, wireless power board) mounted upside-down. Has an 84mm diameter, ~3mm deep cutout that seats the RX coil.
- **Rotor Lid** (bottom part): Flat mating surface meets the Rotor Base, capturing the coil. Shaft cylinder below accepts the motor shaft.
- **Fasteners**: 3 flat-head hex screws through arm undersides; grub screw locks motor shaft in place.

```
ROTOR (spinning)
┌─────────────────────────────┐
│      ROTOR BASE (LEDs)      │  ← LEDs visible on top surface
│  ┌───────────────────────┐  │
│  │ Components mounted    │  │  ← ESP32, IMU, hall sensor, etc.
│  │ upside-down (chips    │  │     foam-taped to underside
│  │ face DOWN)            │  │
│  │                       │  │
│  │ ○ Hall sensor         │  │  ← Branded face DOWN, 52mm from center
│  │   (at coil plane)     │  │     radially outside wireless coil
│  │                       │  │
│  │ [  84mm coil cutout  ]│  │  ← ~3mm deep seat for RX coil
│  │ ═══ RX Coil ═══       │  │  ← 82mm diameter coil sits in cutout
│  └───────────────────────┘  │
│       ROTOR LID             │  ← Flat mating surface, 3 hex screws
│    ║ shaft cylinder ║       │  ← Accepts motor shaft (grub screw)
└─────────────────────────────┘
        ~6-7mm gap
┌─────────────────────────────┐
│ ═══ TX Coil ═══    ☐ Magnet │  ← Magnet at 52mm radius, S pole UP
│      ║ Motor Shaft ║        │
└──────╨─────────────╨────────┘
STATOR (stationary)
```

The hall sensor and trigger magnet are:
- At 52mm radius from rotation center
- Coplanar with the wireless coils (same horizontal level)
- Radially outside the coils (82mm diameter = 41mm radius)

### Component Positions

All angles measured counter-clockwise from hall sensor (0°), viewed from above looking down at LEDs.

| Component | Angle | Radius (mm) | Notes |
|-----------|-------|-------------|-------|
| Hall sensor | 0° | 52.0 | Reference point |
| IMU | 0° | 25.4 | Aligned with hall sensor |
| Arm 1 | 60° | 104.5 (tip) | |
| ESP32 | 140° | ~32.5 | Rough estimate |
| Arm 3 | 180° | 104.5 (tip) | |
| Buck converter | 283° | 28.2 | Center of mass |
| Arm 2 | 300° | 104.5 (tip) | |
| Wireless power board | ~0° | ~0 | Inductor coil centered on rotation axis |

## Microcontroller

- **Board**: Seeed XIAO ESP32S3
- **USB Port** (macOS): `/dev/cu.usbmodem2101`

**Why ESP32-S3?**
- Tiny form factor
- WiFi for programming/control
- Hardware SPI with DMA support

## Power System

Power is delivered wirelessly to the spinning rotor via inductive coupling.

- **Wireless power module**: Taidacent 12V 2.5A (XKT901-17)
  - 12V input/output, up to 2.5A at 5mm gap
  - TX coil: 30mm inner / 81mm outer diameter, 14µH
  - RX coil: 30mm inner / 81mm outer diameter, 14µH
  - Working distance: 5-20mm (output drops with distance)
  - Datasheet: `docs/datasheets/WirelessPower.txt`
  - **RX output cap**: 220µF electrolytic on 12V output (buffers wireless coupling latency)
- **Buck converter**: Mini MP1584EN
  - Input: 4.5-28V, Output: 0.8-20V adjustable
  - Max 3A output, 96% efficiency, 1.5MHz switching
  - Datasheet: `docs/datasheets/dc-buck-converter.txt`
  - **Input cap**: 0.1µF (104) ceramic on 12V input side (on-board or near converter)
- **Bulk capacitor**: 470µF 16V electrolytic on 5V rail output (TO BE TESTED)
  - Prevents brownouts during WiFi TX + LED current transients
  - Buffers buck converter control loop response time (~10-100µs)

## LEDs

- **Type**: SK9822 (APA102-compatible)
- **Quantity**: 33 total (3 arms × 11 LEDs each)
- **Color order**: BGR
- **Datasheet**: `docs/datasheets/SK9822 LED Datasheet.pdf`

**Why SK9822/APA102?**
- 4-wire clocked SPI protocol (unlike WS2812B's timing-sensitive 3-wire)
- Can drive up to 20-30MHz on hardware SPI
- Much more forgiving timing for high-speed updates
- Each arm's 11 LEDs can update in ~5.5μs at maximum SPI speed

## Sensors

### Hall Effect Sensor

Detects magnet on stationary frame once per revolution for timing reference.

- **Model**: Allegro A3144 (unipolar, active-low, south-pole activated)
  - Operate point (BOP): 35-450 Gauss (output turns ON, pulls low)
  - Release point (BRP): 25-430 Gauss (output turns OFF, returns high)
  - Hysteresis: 20-80 Gauss typical
  - Supply: 4.5-24V, open-collector output (internal pull-up enabled in firmware)
  - Note: Discontinued, A1104 is recommended substitute
  - Datasheet: `docs/datasheets/A3141-2-3-4-hall-effect-sensor-Datasheet.txt`

- **Trigger magnet** (on stator):
  - Type: 5x2mm neodymium disc (~2500-3000 Gauss at surface)
  - Air gap: ~3.5mm at closest approach
  - Radius from rotation center: 52mm

**Electrical timing** (from datasheet):

| Parameter | Typical | Maximum |
|-----------|---------|---------|
| Output fall time (tf) | 0.18μs | 2.0μs |
| Output rise time (tr) | 0.04μs | 2.0μs |

At 2800 RPM, 0.18μs = 0.003° of rotation. Electrical response is negligible.

**Trigger behavior:**

The firmware triggers on the **falling edge** (GPIO_INTR_NEGEDGE) which occurs when the magnetic field exceeds the operate threshold - i.e., when the sensor **enters** the magnet's detection zone, not when it leaves.

Due to magnetic field geometry, the sensor triggers approximately **6° before** reaching closest approach to the magnet. This creates a fixed angular offset between the ISR timestamp and the physical magnet position:

- `angle_deg = 0°` (hall timestamp) → sensor is ~6° before magnet
- `angle_deg ≈ 6°` → sensor at closest approach to magnet

For RPM calculation, this offset is irrelevant (cancels out). For correlating telemetry angles to physical positions on the rotor, subtract 6° from telemetry angles to get position relative to the magnet. See `POV_TELEMETRY_ANALYSIS_GUIDE.md` for calibration details.

### MPU-9250 IMU

9-axis IMU (gyroscope + accelerometer + magnetometer) for motion sensing.

- **Model**: InvenSense MPU-9250
- **Interface**: I2C at 400kHz (ADO=LOW for address 0x68, NCS=HIGH for I2C mode)
- **Sensors**:
  - Gyroscope: ±250/500/1000/2000 °/s, 16-bit ADC
  - Accelerometer: ±2/4/8/16g, 16-bit ADC
  - Magnetometer: ±4800µT (AK8963), 14-bit (not used)
- **Pin assignments**: See `led_display/include/hardware_config.h`
- **Datasheet**: `docs/datasheets/PS-MPU-9250A-01-v1.1.pdf`

**Mounting orientation:**

The IMU is mounted upside-down (chip facing floor), 25.4mm from rotation center:

| IMU Axis | Physical Direction | Notes |
|----------|-------------------|-------|
| **Z+** | DOWN (toward ground) | Aligned with gravity when stationary |
| **Y+** | Radial (outward) | Will saturate from centrifugal force |
| **X+** | Tangent to rotation | Toward heavy wireless power board |

Counter-clockwise rotation (viewed from above) means **GZ is negative** during normal operation.

**Telemetry configuration:**
- Accel range: ±16g (2048 LSB/g)
- Gyro range: ±2000°/s (16.4 LSB/°/s)
- DLPF mode 1: 184Hz bandwidth, 2.9ms delay
- Sample rate: 1kHz (divider=0)
- DATA_READY interrupt for timestamping

**Raw data conversion (in Python analysis):**
- Accel: `raw * (16.0 / 32768.0)` = g
- Gyro: `raw * (2000.0 / 32768.0)` = °/s
