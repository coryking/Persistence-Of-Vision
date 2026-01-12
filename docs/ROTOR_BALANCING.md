# POV Display Rotor Balancing

## The Problem

The POV display rotor exhibits significant vibration at high speed (2300-2800 RPM). The rotor weighs approximately 154g total and consists of three arms arranged 120° apart, each carrying 11 SK9822 LEDs. Balancing is currently done manually by taping a nickel (counterweight) to the disc surface, but finding the correct position requires trial and error across two dimensions: angle and radius.

## Approaches Explored

### Load Cell Approach (Rejected)

**Concept:** Place three load cells at 120° intervals, rest each arm on a cell, measure weight distribution. Unequal readings indicate which arm is heavy.

**Hardware evaluated:** 50kg half-bridge load cells (bathroom scale type) with HX711 24-bit ADC amplifier. These are 3-wire cells (~1000Ω internal resistance) that require either external resistors or multiple cells wired together to form a full Wheatstone bridge.

**Why it doesn't work for this application:**

- **Resolution problem:** 50kg capacity with ~51g per arm means operating at 0.1% of full scale. Real-world precision on these cells is approximately ±10g, which is at or above the magnitude of imbalance we're trying to detect.

- **Cell-to-cell variation:** Each load cell has its own sensitivity. Even with individual calibration, systematic differences between cells would mask small imbalances.

- **Static vs dynamic:** Load cells measure static weight distribution, which catches center-of-mass offset (static imbalance). This is probably sufficient for a thin planar disc where dynamic imbalance (mass distribution along the rotation axis) is minimal. However, the resolution problem makes this moot.

**Verdict:** Wrong tool for the job. These cells are designed for 50-200kg human scales, not gram-level differential measurements on a 154g object.

### Knife-Edge Static Balancing (Viable but Limited)

**Concept:** Mount rotor on a low-friction pivot through its center axis. Heavy spot rotates to bottom under gravity. Add counterweight opposite to the heavy spot.

**Advantages:** No electronics, instant feedback, no power required.

**Limitations:** Requires building a suitable jig with very low friction pivot. Still leaves you searching for the correct counterweight radius by trial and error.

### Accelerometer-Based Phase Detection (Selected Approach)

**Concept:** Mount an accelerometer on the spinning rotor. Detect the wobble signature and correlate it with rotational position (via Hall sensor) to determine the angular location of the imbalance.

**Key insight:** This approach doesn't tell you the magnitude of imbalance in absolute terms (gram-centimeters), but it tells you the *angle* of the imbalance. This collapses a 2D search problem (angle + radius) into a 1D search (radius only along a known line).

## Telemetry Data Fields

Accelerometer samples and hall events are captured separately and correlated in post-processing.

**Wire format (ESP-NOW):** Uses delta timestamps for efficiency (8 bytes/sample vs 28 originally). Motor controller expands to absolute timestamps when storing.

**CSV format after `pov telemetry dump`:**

| Field | Type | Description |
|-------|------|-------------|
| `timestamp_us` | u64 | Absolute microsecond timestamp (from DATA_READY interrupt) |
| `sequence_num` | u16 | Monotonic counter - gaps indicate dropped samples |
| `rotation_num` | u16 | Which revolution this sample is from (computed in post-processing) |
| `micros_since_hall` | u64 | Microseconds since last hall trigger (computed in post-processing) |
| `x`, `y`, `z` | i16 | Accelerometer raw values (±16g range, 3.9mg/LSB) |

**Post-processing:** The `pov telemetry dump` command automatically enriches the accel CSV by correlating timestamps with hall events to compute `rotation_num` and `micros_since_hall`.

**Computing phase:**
```python
phase = accel['micros_since_hall'] / hall['period_us']  # 0.0 to 1.0
angle_deg = phase * 360  # 0° to 360°
```

Hall events include `rotation_num` which is used during post-processing to link samples to rotations.

## Accelerometer Physics

### Reference Frame Matters

An accelerometer on a stationary mount (motor housing) sees different signals than one mounted on the spinning rotor.

**Stationary frame:** Sees vibration transmitted through the bearings. The imbalance creates a rotating centrifugal force that wobbles the entire assembly. Classic industrial balancing equipment works this way.

**Rotating frame (on-rotor):** The imbalance is stationary relative to the sensor. Centrifugal forces from imbalance don't oscillate—they're constant in magnitude and direction relative to the rotor.

### Why On-Rotor Still Works: Z-Axis Wobble

If the rotor physically tilts/wobbles as it spins (disc plane oscillating relative to the spin axis), the Z-axis of an on-rotor accelerometer sees this as a periodic signal. Gravity's projection onto the Z-axis changes as the disc tilts back and forth.

The phase of this Z-axis oscillation relative to a rotational reference point (Hall sensor trigger) indicates where the heavy spot is located.

### Accelerometer Placement

The accelerometer does not need to be precisely centered on the rotation axis.

**Current mounting:** ~27mm from center, double-stick foam tape, intentional orientation (see Mounting section above).

**Axis expectations at speed:**
- **Y axis (radial)**: Points outward from rotation axis. Sees full centrifugal force - will saturate at operating RPM.
- **X axis (tangent)**: Perpendicular to centrifugal force - should stay within range.
- **Z axis (axial)**: Along rotation axis - should stay within range, sees wobble signal.

**Centrifugal force reality check** (at ~27mm radius, ±16g range):

| RPM | Centrifugal (g) | Y axis |
|-----|-----------------|--------|
| 480 | ~7g | OK |
| 680 | ~14g | OK |
| 720 | ~16g | Saturates |
| 1000 | ~30g | Saturated |
| 2800 | ~237g | Saturated |

The Y axis will saturate above ~720 RPM. The X and Z axes should remain usable for wobble detection throughout the operating range.

The wobble signal comes from gravity's projection changing as the disc tilts—this appears on the Z axis (and possibly X). Mount the accelerometer wherever is mechanically convenient; perfect centering is unnecessary.

### What Accelerometers Measure

Accelerometers measure acceleration, not velocity or position. You poll (or interrupt-read) instantaneous acceleration vectors. Dead reckoning (integrating to velocity/position) accumulates drift and is not useful here.

For balancing, we're looking for:

1. **Frequency** of the periodic Z signal (equals rotation frequency)
2. **Phase** of Z signal relative to Hall trigger (indicates heavy spot angle)
3. **Amplitude** of Z oscillation (indicates severity of wobble, qualitatively)

No integration or coordinate transforms required.

## Hardware Selection: MPU-9250

### Capabilities

- 9-axis IMU (gyroscope + accelerometer + magnetometer)
- **Gyroscope**: ±250/500/1000/2000 °/s, 16-bit ADCs, up to 8kHz sample rate
- **Accelerometer**: ±2/4/8/16g, 16-bit ADCs, up to 4kHz sample rate
- **Magnetometer**: ±4800µT (AK8963), 14-bit, up to 100Hz
- I2C (400kHz) or SPI (1MHz registers, 20MHz sensor data) interfaces
- Programmable interrupt outputs
- 512-byte FIFO buffer
- Operating voltage: 2.4-3.6V
- Datasheet: `docs/datasheets/PS-MPU-9250A-01-v1.1.pdf`

### Communication Interface Decision: I2C

**Why not SPI:** The ESP32-S3 SPI bus is already used for LED data (SK9822). While the ESP32-S3 has multiple SPI peripherals, adding the IMU to I2C keeps the buses separate and avoids any potential timing conflicts with the timing-critical LED updates.

**I2C considerations:**

- Fast mode (400kHz) used
- Up to 1kHz sample rate via DATA_READY interrupt
- Reading 6 bytes (X, Y, Z as 16-bit values) per sensor per sample
- 512-byte FIFO available for future burst reads

**Target configuration:**
- Accel ODR: 1kHz
- Accel Range: ±16g (2048 LSB/g)
- Gyro Range: ±2000°/s (16.4 LSB/°/s)
- Sampling: Interrupt-driven via DATA_READY on INT

**Bandwidth adequacy:** At 2800 RPM (~47 Hz rotation), 1000 samples/second provides ~21 samples per revolution.

**Resolution:** With ~21 samples per revolution, phase can be determined to within ~17° (360°/21). Since arms are 120° apart, this is adequate to identify which arm (or region between arms) contains the heavy spot.

### Interrupt Usage

DATA_READY interrupt is connected: INT → GPIO D9 (brown wire).

**Implementation (decoupled FreeRTOS task):**
1. MPU-9250 fires DATA_READY interrupt when new sample ready
2. ISR captures `esp_timer_get_time()` timestamp and queues it (64-slot buffer)
3. TelemetryTask (dedicated FreeRTOS task on Core 0) drains queue, reads samples
4. Samples batched with delta timestamps and sent via ESP-NOW to motor controller
5. Motor controller expands deltas to absolute timestamps when storing

This architecture decouples telemetry from the LED render loop, preventing sample drops when render() skips or busy-waits.

This gives ~1° timestamp accuracy vs ~34° error from polling at 2800 RPM.

The ESP32's dual-core architecture allows:
- Core 1: Timing-critical LED rendering (existing)
- Core 0: IMU reads, ESP-NOW communication (non-critical timing)

### Wiring Notes

Pin assignments are in `led_display/include/hardware_config.h`.

For I2C operation:
- NCS tied HIGH selects I2C mode
- AD0 sets I2C address (0=0x68, 1=0x69)
- INT available for DATA_READY interrupt
- FSYNC not connected

### Mounting

**Position:** ~27mm from rotation center, mounted with foam double-stick tape.

**Intended orientation:**
- **Y+ axis**: Radial (outward from rotation center) - will saturate from centrifugal force
- **Z+ axis**: Axial, points down toward motor shaft (along rotation axis) - sees wobble signal
- **X+ axis**: Roughly tangent to rotation, pointing away from ESP toward the heavy wireless power board

This orientation was intentional but alignment accuracy is unknown until verified with measurements. The Z-axis should capture disc tilt/wobble as a periodic signal (gravity projection changes as disc tilts), while X sees tangential forces.

Foam tape is acceptable for initial experiments. The wobble signal is a low-frequency periodic signal (~47 Hz), not sharp transients. Foam may introduce slight damping but won't filter out the primary signal.

## Calibration Workflow

### One-Time Phase Calibration

1. Enter calibration mode (LED rendering disabled)
2. Spin up to operating RPM
3. Log Hall trigger timestamps and accelerometer Z values with timestamps
4. Collect data for N revolutions (10-20 sufficient)
5. Post-process: find phase offset between Hall pulse and Z-axis peak
6. Convert phase to angle: if Hall triggers at 0°, and Z peaks at time offset Δt within one rotation period T, angle = (Δt/T) × 360°
7. Heavy spot is at that angle (or 180° opposite, depending on wobble mechanics)
8. Mark the disc permanently at the counterweight angle (180° from heavy spot)

### Iterative Radius Adjustment

Once the counterweight angle is established:

1. Place nickel on the marked radial line
2. Spin up, observe wobble severity (accelerometer amplitude, or just feel/sound)
3. If still bad: power down, move nickel in or out along the same line
4. Repeat until acceptable

This is a 1D binary search: too much wobble → move weight, test again. The angle never changes unless the disc structure is physically modified.

## Additional Accelerometer Use Cases

### Low-Speed RPM Detection

At low RPM, the Hall sensor pulses are infrequent and centrifugal forces are weak. The accelerometer provides an alternative: gravity rotates through the X/Y axes once per revolution. Counting zero-crossings or peaks gives rotation rate even at very low speeds.

### Spin Direction Detection

Which way gravity rotates through X/Y axes (phase relationship between X and Y) indicates clockwise vs counterclockwise rotation.

### Quantitative Balancing (Future)

For more precise balancing (knowing exactly how much weight at what radius), a trial-weight method could be implemented:

1. Measure baseline wobble
2. Add known trial weight at known position
3. Measure new wobble (magnitude and phase change)
4. Calculate correction vector mathematically

This is how industrial single-plane balancing works. Probably overkill for this application where "iterate until it feels smooth" is sufficient.

## Key Equations

**Centrifugal force:** F = mω²r (mass × angular velocity squared × radius)

**Static balance condition:** m₁r₁ = m₂r₂ (imbalance mass×radius equals counterweight mass×radius)

**Nyquist criterion:** Sample rate must be >2× signal frequency

**Phase to angle:** θ = (Δt / T) × 360° where Δt is time offset and T is rotation period

## Summary

The accelerometer approach enables finding the imbalance angle through a single calibration run, converting an iterative 2D search into a 1D search along a fixed radial line. The MPU-9250 over I2C provides adequate sample rates for phase detection at operating RPM, plus gyroscope data that can directly measure rotation rate and wobble. The sensor mounts on the spinning rotor, detecting wobble via Z-axis oscillation correlated with Hall sensor timing. This integrates with the existing ESP32-S3 dual-core architecture without impacting timing-critical LED rendering.
