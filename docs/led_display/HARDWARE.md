# LED Display Rotor Hardware

Physical hardware documentation for the spinning POV display rotor.

**Pin assignments:** See `led_display/include/hardware_config.h`

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

- **Model**: Allegro A3144 (unipolar, active-low)
  - Operate point: 35-450 Gauss
  - Release point: 25-430 Gauss
  - Supply: 4.5-24V, open-collector output (needs pull-up)
  - Note: Discontinued, A1104 is recommended substitute
  - Datasheet: `docs/datasheets/A3144-hall-effect-sensor-datasheet.txt`

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

**Telemetry configuration:**
- Accel range: ±16g (2048 LSB/g)
- Gyro range: ±2000°/s (16.4 LSB/°/s)
- DLPF mode 1: 184Hz bandwidth, 2.9ms delay
- Sample rate: 1kHz (divider=0)
- DATA_READY interrupt for timestamping

**Raw data conversion (in Python analysis):**
- Accel: `raw * (16.0 / 32768.0)` = g
- Gyro: `raw * (2000.0 / 32768.0)` = °/s
