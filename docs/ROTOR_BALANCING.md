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

**At center (r=0):**
- No centrifugal acceleration
- Gravity rotates through X/Y once per revolution (useful for low-speed RPM and direction detection)
- Z-axis wobble from disc tilt

**Away from center (r>0):**
- Constant centrifugal acceleration (ω²r) pointing outward in rotating frame
- Gravity signal gets swamped by centrifugal at high RPM
- Z-axis wobble still visible

The wobble signal comes from gravity's projection changing as the disc tilts—this is roughly constant across the disc surface. Mount the accelerometer wherever is mechanically convenient; perfect centering is unnecessary.

### What Accelerometers Measure

Accelerometers measure acceleration, not velocity or position. You poll (or interrupt-read) instantaneous acceleration vectors. Dead reckoning (integrating to velocity/position) accumulates drift and is not useful here.

For balancing, we're looking for:

1. **Frequency** of the periodic Z signal (equals rotation frequency)
2. **Phase** of Z signal relative to Hall trigger (indicates heavy spot angle)
3. **Amplitude** of Z oscillation (indicates severity of wobble, qualitatively)

No integration or coordinate transforms required.

## Hardware Selection: ADXL345

### Capabilities

- 3-axis MEMS accelerometer
- 13-bit resolution, ±2g to ±16g selectable range
- Output data rates from 0.1 Hz to 3200 Hz
- I2C and SPI interfaces
- Two programmable interrupt outputs
- 32-level FIFO buffer
- Operating voltage: 2.5-3.6V (modules typically have onboard regulator for 5V input)

### Communication Interface Decision: I2C

**Why not SPI:** The ESP32-S3 SPI bus is already used for LED data (SK9822). While the ESP32-S3 has multiple SPI peripherals, adding the accelerometer to I2C keeps the buses separate and avoids any potential timing conflicts with the timing-critical LED updates.

**I2C considerations:**

- Standard mode (100kHz) or fast mode (400kHz) available
- Practical throughput: ~400-500 samples/second achievable with basic I2C code on ESP32
- Reading 6 bytes (X, Y, Z as 16-bit values) per sample
- FIFO allows burst reads to improve efficiency

**Bandwidth adequacy:** At 2800 RPM (~47 Hz rotation), 500 samples/second provides ~10 samples per revolution. Nyquist requires >94 samples/second to capture a 47 Hz signal. 500 Hz is more than sufficient for phase detection.

**Resolution:** With ~10 samples per revolution, phase can be determined to within ~36° (360°/10). Since arms are 120° apart, this is adequate to identify which arm (or region between arms) contains the heavy spot.

### Interrupt Usage

The ADXL345 provides DATA_READY interrupt capability. Rather than polling, wire INT1 to a GPIO and handle data reads in an ISR or flag-based approach.

For the calibration/balancing measurement, this doesn't need to be high-priority. The ESP32's dual-core architecture allows:

- Core 1: Timing-critical LED rendering (existing)
- Core 0: Accelerometer reads, ESP-NOW communication (non-critical timing)

Alternatively, run a dedicated "calibration mode" that disables LED rendering entirely and just logs accelerometer + Hall data for post-processing.

### Wiring Notes

For I2C operation:

- CS tied to VCC selects I2C mode
- SDO tied to GND sets I2C address to 0x53
- INT1 available for DATA_READY interrupt
- INT2 can be left floating if unused

### Mounting

Foam double-stick tape is acceptable for initial experiments. The wobble signal is a low-frequency periodic signal (~47 Hz), not sharp transients. Foam may introduce slight damping but won't filter out the primary signal. Upgrade to rigid mounting if data looks noisy.

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

The accelerometer approach enables finding the imbalance angle through a single calibration run, converting an iterative 2D search into a 1D search along a fixed radial line. The ADXL345 over I2C provides adequate sample rates for phase detection at operating RPM. The sensor mounts on the spinning rotor, detecting wobble via Z-axis oscillation correlated with Hall sensor timing. This integrates with the existing ESP32-S3 dual-core architecture without impacting timing-critical LED rendering.
