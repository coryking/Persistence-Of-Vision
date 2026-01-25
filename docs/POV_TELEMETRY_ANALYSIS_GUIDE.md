# POV Display Telemetry Analysis Guide

This document describes the methodology for analyzing telemetry data from the POV display's IMU and Hall effect sensor to characterize rotational imbalance and determine optimal counterweight placement.

> **Hardware reference:** Physical assembly, sensor specs, and component positions are documented in `HARDWARE.md`. This guide focuses on analysis methodology.

---

## The Physics of Rotational Imbalance

Understanding the underlying physics is essential for interpreting telemetry data correctly. This section covers the theory; later sections cover the practical workflow.

### Imbalance Force

An imbalanced rotating mass creates centrifugal force proportional to the square of angular velocity:

```
F = m Ã— Ï‰Â² Ã— r

Where:
  m = imbalance mass (kg)
  Ï‰ = angular velocity (rad/s) = RPM Ã— 2Ï€/60
  r = radius of imbalance from rotation axis (m)
```

This Ï‰Â² relationship is fundamentalâ€”imbalance effects grow quadratically with speed. A rotor that wobbles slightly at 700 RPM will wobble 16Ã— more at 2800 RPM (4Â² = 16).

### Wobble Response

The imbalance force causes the disc to tilt and precess. The gyroscope's X and Y axes (perpendicular to the rotation axis) measure this wobble as angular rates:

```
wobble_magnitude = âˆš(gxÂ² + gyÂ²)   [Â°/s]
```

For a simple rigid rotor well below its critical speed, wobble magnitude scales with Ï‰Â²:

```
wobble = k Ã— RPMÂ² + offset

Where k is determined by imbalance mass, radius, and system stiffness.
```

A linear fit of wobble vs RPMÂ² with RÂ² > 0.9 confirms classic imbalance behavior.

### Critical Speed and Dynamic Response

Critical speed is the RPM where the rotor's spin frequency matches a natural resonance of the mechanical system. At critical speed, vibration amplitude spikes dramatically and the phase relationship between the heavy spot and measured response shifts.

The full dynamic response follows the Jeffcott rotor model:

```
A/e = fÂ² / âˆš[(1-fÂ²)Â² + (2Î¶f)Â²]

Where:
  A = vibration amplitude
  e = effective eccentricity (imbalance offset)
  f = Ï‰/Ï‰n = rotation frequency / natural frequency  
  Î¶ = damping ratio (typically 0.01-0.1)
```

This simplifies in different operating regimes:

| Regime | Frequency Ratio | Amplitude Behavior | Phase Lag |
|--------|-----------------|-------------------|-----------|
| Well below critical | f < 0.5 | wobble âˆ RPMÂ² | ~0Â° |
| Near critical | f â‰ˆ 0.7-1.3 | Amplified, non-linear | 45Â°-135Â° |
| At critical | f = 1 | Peak amplitude â‰ˆ e/(2Î¶) | 90Â° |
| Above critical | f > 1.5 | Amplitude approaches constant | ~180Â° |

The phase lag between heavy spot (cause) and measured high spot (response) is:

```
Ï†_lag = atan2(2Î¶f, 1-fÂ²)
```

**For this POV display:** The rotor mass is 152g with arm tips at 104.5mm radius. If you observe worse vibration at lower RPMs that improves at higher speeds, you may be operating above a critical speed. In that regime, the heavy spot is approximately 180Â° opposite from where you might naively expect based on raw phase measurements.

### From Measured Phase to Heavy Spot Location

The full phase correction from measured gyroscope data to actual heavy spot location:

```
Î¸_heavy = Î¸_measured - Ï†_lag + Î¸_sensor + Î¸_integration

Where:
  Î¸_measured   = phase from atan2(mean_gy, mean_gx) or sinusoid fit
  Ï†_lag        = speed-dependent lag (0Â° well below critical, ~180Â° well above)
  Î¸_sensor     = hall sensor trigger offset (~6Â° per HARDWARE.md)
  Î¸_integration = 90Â° for gyroscope (rate/velocity measurement)
```

For operation well above critical speed (where Ï†_lag â‰ˆ 180Â°):

```
Î¸_heavy â‰ˆ Î¸_measured - 180Â° + 6Â° + 90Â° = Î¸_measured - 84Â°
```

For operation well below critical speed (where Ï†_lag â‰ˆ 0Â°):

```
Î¸_heavy â‰ˆ Î¸_measured + 6Â° + 90Â° = Î¸_measured + 96Â°
```

The counterweight placement is always opposite the heavy spot:

```
Î¸_counterweight = Î¸_heavy + 180Â°
```

**Practical note:** These offsets should be verified empirically using trial weights. If your first counterweight attempt consistently misses in the same direction, adjust the offset accordingly.

### ISO Balance Quality Reference

ISO 21940-11 defines balance quality grades. The permissible residual unbalance:

```
U_per (gÂ·mm) = 9549 Ã— G Ã— m / n

Where:
  G = balance quality grade
  m = rotor mass (kg)
  n = maximum service speed (RPM)
```

For this POV display (152g rotor, 2800 RPM max, targeting G 6.3 for general machinery):

```
U_per = 9549 Ã— 6.3 Ã— 0.152 / 2800 = 3.3 gÂ·mm
```

This means acceptable balance is achieved with, for example, a 0.1g imbalance at 33mm radius, or 0.05g at 66mm radius. For reference, a US dime is 2.27gâ€”even small coins represent significant imbalance at the arm tips.

---

## Sensor Configuration

### IMU Mounting and Axes

The MPU-9250 IMU is mounted upside-down at 25.4mm radius from rotation center, aligned with the hall sensor at 0Â°. See `HARDWARE.md` for detailed orientation.

| IMU Axis | Physical Direction | During CCW Rotation |
|----------|-------------------|---------------------|
| **X+** | Radial outward | Saturates from centrifugal force |
| **Y+** | Tangent (toward 90Â°) | Does not saturate |
| **Z+** | Down (toward ground) | GZ negative during CCW rotation |

**Gyroscope axes for wobble analysis:**
- **GX and GY** measure wobble/precession (tilt rates perpendicular to rotation axis)
- **GZ** measures rotation rate but saturates above ~333 RPM (Â±2000Â°/s limit)

**Accelerometer axes:**
- **X** saturates from centrifugal acceleration at operating speedsâ€”ignore for analysis
- **Y and Z** remain unsaturated and can be used for phase analysis via angle binning

### Expected Saturation

At operating speeds (700-2800 RPM), expect:

| Sensor | Saturates? | Why |
|--------|------------|-----|
| GZ (gyro) | Yes | Rotation rate exceeds Â±2000Â°/s above 333 RPM |
| X accel | Yes | Centrifugal acceleration exceeds Â±16g |
| GX, GY (gyro) | No | Wobble rates are much smaller |
| Y, Z accel | No | Tangential and axial forces are manageable |

Saturation of GZ and X-accel is normal and expected. Use the pre-computed `rpm` column (derived from hall sensor timing) rather than attempting to calculate RPM from saturated GZ.

### Sampling Rate

The IMU samples at 8kHz, which is vastly above the Nyquist requirement:

| RPM | Rotation Freq | Nyquist Minimum | Actual | Margin |
|-----|---------------|-----------------|--------|--------|
| 700 | 11.7 Hz | 23 Hz | 8000 Hz | 350Ã— |
| 2800 | 46.7 Hz | 93 Hz | 8000 Hz | 86Ã— |

This high sample rate provides excellent resolution for angle binning (~170 samples per revolution at 2800 RPM) and robust noise rejection through averaging.

### Hall Effect Sensor Timing

The A3144 hall sensor triggers on the falling edge when entering the magnet's field, approximately 6Â° before closest approach to the magnet. This creates a fixed offset:

- `angle_deg = 0Â°` in telemetry â†’ sensor is ~6Â° before the magnet
- `angle_deg â‰ˆ 6Â°` â†’ sensor at closest approach

For imbalance analysis, this offset is constant and can be folded into the phase correction. For correlating specific angles to physical rotor positions, subtract 6Â° from telemetry angles.

---

## Data Acquisition

### Capture Command

```bash
pov telemetry test [OPTIONS]

Options:
  --settle / -s    Seconds to wait after speed change (default: 3.0)
  --record / -r    Seconds to record at each speed (default: 5.0)
  --port / -p      Serial port
  --output / -o    Output directory base
  --json           Output results as JSON
```

### Directory Structure

Telemetry captures produce a flat directory with per-step files:

```
telemetry/2026-01-24T11-50-05/
â”œâ”€â”€ MSG_ACCEL_SAMPLES_step_01_245rpm.csv
â”œâ”€â”€ MSG_HALL_EVENT_step_01_245rpm.csv
â”œâ”€â”€ MSG_ACCEL_SAMPLES_step_02_312rpm.csv
â”œâ”€â”€ MSG_HALL_EVENT_step_02_312rpm.csv
â”œâ”€â”€ ...
â”œâ”€â”€ MSG_ACCEL_SAMPLES_step_10_892rpm.csv
â”œâ”€â”€ MSG_HALL_EVENT_step_10_892rpm.csv
â””â”€â”€ manifest.json
```

### Data Format

**MSG_ACCEL_SAMPLES columns:**

| Column | Description | Units |
|--------|-------------|-------|
| `timestamp_us` | Microsecond timestamp | Âµs |
| `sequence_num` | Sample sequence number | â€” |
| `rotation_num` | Rotation count since start | â€” |
| `micros_since_hall` | Time since last hall trigger | Âµs |
| `angle_deg` | Pre-computed angular position | degrees (0-360) |
| `rpm` | Pre-computed RPM from hall timing | RPM |
| `x_g`, `y_g`, `z_g` | Accelerometer readings | g |
| `gx_dps`, `gy_dps`, `gz_dps` | Gyroscope readings | Â°/s |
| `gyro_wobble_dps` | Pre-computed âˆš(gxÂ² + gyÂ²) | Â°/s |
| `is_y_saturated`, `is_gz_saturated` | Saturation flags | bool |

**MSG_HALL_EVENT columns:**

| Column | Description |
|--------|-------------|
| `timestamp_us` | When hall sensor triggered |
| `period_us` | Time since previous trigger |
| `rotation_num` | Rotation count |

**manifest.json** contains run metadata including timing, sample counts, and measured RPM per step.

---

## Analysis Workflow

### Step 1: Load and Inspect Data

```python
import json
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
from scipy.optimize import curve_fit

base_dir = Path('/path/to/telemetry/run')

# Load manifest
with open(base_dir / 'manifest.json') as f:
    manifest = json.load(f)

# Load each speed step
results = []
for step in manifest['steps']:
    if not step['success']:
        continue
    
    pos = step['position']
    rpm = step['rpm']
    
    # Find matching files (filename includes RPM)
    accel_file = list(base_dir.glob(f'MSG_ACCEL_SAMPLES_step_{pos:02d}_*.csv'))[0]
    hall_file = list(base_dir.glob(f'MSG_HALL_EVENT_step_{pos:02d}_*.csv'))[0]
    
    df = pd.read_csv(accel_file)
    hall_df = pd.read_csv(hall_file)
    
    results.append({
        'position': pos,
        'rpm': rpm,
        'accel_df': df,
        'hall_df': hall_df,
    })
```

Quick sanity checks:
- Verify file sizes are reasonable (not truncated)
- Check that RPM values in manifest match pre-computed `rpm` column means
- Confirm expected number of rotations per step

### Step 2: Calculate Per-Step Metrics

For each speed step, calculate:

```python
def analyze_step(df, rpm):
    """Analyze a single speed step's telemetry data."""
    
    # Wobble magnitude (pre-computed, or calculate from raw)
    wobble_mean = df['gyro_wobble_dps'].mean()
    wobble_std = df['gyro_wobble_dps'].std()
    
    # Precession direction from gyro DC offset
    gx_mean = df['gx_dps'].mean()
    gy_mean = df['gy_dps'].mean()
    precession_dir = np.degrees(np.arctan2(gy_mean, gx_mean))
    
    return {
        'rpm': rpm,
        'rpm_squared': rpm ** 2,
        'wobble_mean': wobble_mean,
        'wobble_std': wobble_std,
        'gx_mean': gx_mean,
        'gy_mean': gy_mean,
        'precession_dir': precession_dir,
    }
```

### Step 3: Fit Wobble vs RPMÂ²

```python
def linear_model(x, slope, intercept):
    return slope * x + intercept

# Extract data for fitting
rpm_squared = [r['rpm_squared'] for r in step_results]
wobble = [r['wobble_mean'] for r in step_results]

# Fit
popt, pcov = curve_fit(linear_model, rpm_squared, wobble)
slope, intercept = popt

# Calculate RÂ²
residuals = np.array(wobble) - linear_model(np.array(rpm_squared), *popt)
ss_res = np.sum(residuals**2)
ss_tot = np.sum((np.array(wobble) - np.mean(wobble))**2)
r_squared = 1 - (ss_res / ss_tot)

print(f"Wobble = {slope:.2e} Ã— RPMÂ² + {intercept:.1f}")
print(f"RÂ² = {r_squared:.3f}")
```

**Interpreting the fit:**

| RÂ² Value | Interpretation |
|----------|----------------|
| > 0.95 | Excellentâ€”classic imbalance dominates |
| 0.9 - 0.95 | Goodâ€”imbalance is primary effect |
| 0.7 - 0.9 | Acceptableâ€”some non-ideal behavior present |
| < 0.7 | Investigateâ€”possible resonance, fixture issues, or multiple imbalance sources |

If RÂ² is poor, check for:
- Speed steps near a structural resonance (wobble spike at specific RPM)
- Mechanical issues (loose screws, rubbing, bearing problems)
- IMU mounting failure (foam tape letting go)

### Step 4: Phase Analysis

Two complementary approaches for determining heavy spot angle:

**Approach A: Precession Direction (simpler)**

The DC offset of GX and GY indicates the tilt direction:

```python
# Aggregate across all speed steps (weighted by RPMÂ² for emphasis on higher speeds)
weights = np.array([r['rpm_squared'] for r in step_results])
weights = weights / weights.sum()

gx_weighted = sum(r['gx_mean'] * w for r, w in zip(step_results, weights))
gy_weighted = sum(r['gy_mean'] * w for r, w in zip(step_results, weights))

precession_angle = np.degrees(np.arctan2(gy_weighted, gx_weighted))
```

Check consistency: precession direction should be stable across speeds (std < 10Â°) if operating in a consistent regime. Large shifts between speeds indicate resonance effects.

**Approach B: Sinusoid Fit to Angle-Binned Data (more robust)**

Bin samples by angular position and fit a sinusoid:

```python
def sinusoid(x, amp, phase, offset):
    return amp * np.sin(np.radians(x - phase)) + offset

def fit_phase(df, signal_col='gy_dps', bin_width=10):
    """Fit sinusoid to angle-binned signal to extract phase."""
    
    # Create angle bins
    bins = np.arange(0, 360 + bin_width, bin_width)
    df['angle_bin'] = pd.cut(df['angle_deg'], bins=bins, labels=bins[:-1])
    
    # Average signal in each bin
    binned = df.groupby('angle_bin')[signal_col].mean()
    angles = binned.index.astype(float) + bin_width/2  # bin centers
    values = binned.values
    
    # Initial guess
    amp_guess = (values.max() - values.min()) / 2
    phase_guess = angles[np.argmax(values)]
    offset_guess = values.mean()
    
    # Fit
    try:
        popt, _ = curve_fit(sinusoid, angles, values, 
                           p0=[amp_guess, phase_guess, offset_guess])
        amp, phase, offset = popt
        
        # Calculate RÂ²
        predicted = sinusoid(angles, *popt)
        ss_res = np.sum((values - predicted)**2)
        ss_tot = np.sum((values - values.mean())**2)
        r_squared = 1 - (ss_res / ss_tot)
        
        return {'amplitude': amp, 'phase': phase, 'offset': offset, 'r_squared': r_squared}
    except:
        return None
```

Quality thresholds for sinusoid fit RÂ²:

| RÂ² Value | Interpretation |
|----------|----------------|
| > 0.5 | Strong signalâ€”trust this phase estimate |
| 0.3 - 0.5 | Moderateâ€”usable but verify with multiple speeds |
| < 0.3 | Weakâ€”likely dominated by noise, use precession direction instead |

### Step 5: Determine Counterweight Placement

Combining the phase measurement with corrections:

```python
def calculate_counterweight_angle(measured_phase, operating_regime='above_critical'):
    """
    Calculate counterweight placement angle.
    
    Args:
        measured_phase: Phase from precession direction or sinusoid fit (degrees)
        operating_regime: 'below_critical', 'above_critical', or 'unknown'
    
    Returns:
        Recommended counterweight angle (degrees, 0-360)
    """
    
    HALL_OFFSET = 6.0       # Hall triggers 6Â° before magnet (from HARDWARE.md)
    GYRO_INTEGRATION = 90.0  # Gyroscope measures rate, not displacement
    
    if operating_regime == 'below_critical':
        phase_lag = 0.0
    elif operating_regime == 'above_critical':
        phase_lag = 180.0
    else:
        # Conservative: assume above critical if low-RPM vibration > high-RPM
        phase_lag = 180.0
    
    # Heavy spot location
    heavy_spot = measured_phase - phase_lag + HALL_OFFSET + GYRO_INTEGRATION
    
    # Counterweight goes opposite
    counterweight = heavy_spot + 180.0
    
    # Normalize to 0-360
    counterweight = counterweight % 360
    
    return counterweight, heavy_spot % 360
```

---

## The Influence Coefficient Method

Industrial balancers don't rely solely on theoretical phase corrections. Instead, they empirically calibrate the relationship between weight placement and vibration response. This approach implicitly captures all system dynamics without requiring accurate theoretical models.

### Procedure

1. **Baseline run:** Measure vibration vector Vâ‚€ (magnitude and phase) at a fixed test speed

2. **Trial weight run:** Add a known trial weight W_trial at a known angle, measure new vibration Vâ‚

3. **Calculate influence coefficient:**
   ```
   C = (Vâ‚ - Vâ‚€) / W_trial
   
   Where all quantities are complex vectors:
     V = magnitude Ã— e^(i Ã— phase)
     W_trial = mass Ã— radius Ã— e^(i Ã— angle)
   ```

4. **Calculate correction weight:**
   ```
   W_correction = -Vâ‚€ / C
   ```

5. **Apply and verify:** Remove trial weight, add correction weight at calculated angle, confirm vibration reduction

### Implementation

```python
def influence_coefficient_balance(v0_mag, v0_phase, v1_mag, v1_phase,
                                   trial_mass, trial_radius, trial_angle):
    """
    Calculate correction weight using influence coefficient method.
    
    Args:
        v0_mag, v0_phase: Baseline vibration (magnitude in Â°/s, phase in degrees)
        v1_mag, v1_phase: Vibration with trial weight
        trial_mass: Trial weight mass (grams)
        trial_radius: Trial weight radius from center (mm)
        trial_angle: Trial weight angle (degrees)
    
    Returns:
        correction_mass, correction_angle, expected_reduction
    """
    
    # Convert to complex vectors
    V0 = v0_mag * np.exp(1j * np.radians(v0_phase))
    V1 = v1_mag * np.exp(1j * np.radians(v1_phase))
    W_trial = (trial_mass * trial_radius) * np.exp(1j * np.radians(trial_angle))
    
    # Influence coefficient
    delta_V = V1 - V0
    C = delta_V / W_trial
    
    # Correction weight
    W_correction = -V0 / C
    
    correction_moment = np.abs(W_correction)  # gÂ·mm
    correction_angle = np.degrees(np.angle(W_correction)) % 360
    
    # Expected reduction (theoreticalâ€”actual will vary)
    expected_residual = np.abs(V0 + C * W_correction)
    expected_reduction = (v0_mag - expected_residual) / v0_mag * 100
    
    return {
        'correction_moment_gmm': correction_moment,
        'correction_angle': correction_angle,
        'expected_reduction_pct': expected_reduction,
        'influence_coefficient': C,
    }
```

### Why This Works

The influence coefficient captures everything:
- Phase lag from critical speed effects
- Sensor integration offsets
- Mounting geometry quirks
- Bearing and support characteristics

No theoretical corrections neededâ€”it's purely empirical. The tradeoff is requiring an extra test run with a trial weight.

### Trial Weight Guidelines

- Use a weight that produces measurable but not dangerous vibration change (aim for 20-50% increase)
- Place at a convenient known angle (e.g., at an arm position: 60Â°, 180Â°, or 300Â°)
- Use the same test speed for baseline, trial, and verification runs
- Ensure good repeatability between runs (let system stabilize)

---

## Counterweight Options

US coins as balancing weights:

| Coin | Mass | At 50mm radius | At 100mm radius |
|------|------|----------------|-----------------|
| Dime | 2.27g | 113.5 gÂ·mm | 227 gÂ·mm |
| Penny | 2.50g | 125 gÂ·mm | 250 gÂ·mm |
| Nickel | 5.00g | 250 gÂ·mm | 500 gÂ·mm |
| Quarter | 5.67g | 283.5 gÂ·mm | 567 gÂ·mm |

For reference, the ISO G 6.3 permissible unbalance for this rotor is 3.3 gÂ·mmâ€”even a dime at the arm tips represents ~70Ã— the "acceptable" imbalance for precision machinery. For a POV display, you're balancing for vibration reduction and bearing life, not precision grinding, so practical targets are less stringent.

### Tuning Strategy

Two adjustment axes:
1. **Mass:** Swap coin type (discrete steps)
2. **Radius:** Move coin in/out along arm (continuous adjustment)

Since moment = mass Ã— radius, moving a coin inward is equivalent to using a lighter coin. Fine-tuning radius is more precise than swapping coins.

### Practical Process

1. Establish baseline with no counterweight
2. Mark a reference line on the rotor at the calculated counterweight angle
3. Place initial weight (start with a dime or penny)
4. Capture telemetry, compare wobble to baseline
5. Adjust:
   - Wobble decreased â†’ good direction, fine-tune mass/radius
   - Wobble increased â†’ wrong angle or overcorrected, reassess
   - Wobble unchanged â†’ weight too small or wrong angle
6. Iterate until satisfied

---

## Interpreting Results

### Good Balance Indicators

- âœ… Wobble reduced by 50%+ from unbalanced baseline
- âœ… Precession direction stable (std < 10Â°) across speeds
- âœ… Wobble vs RPMÂ² maintains good RÂ² after correction
- âœ… No audible resonance or table shaking at operating speeds

### Warning Signs

- âš ï¸ Crossover behavior (weight helps at low RPM, hurts at high RPM) â†’ weight affecting balance in wrong plane, or near resonance
- âš ï¸ Precession direction shifts significantly between configs â†’ multiple imbalance sources
- âš ï¸ Poor RÂ² on wobble fit â†’ possible resonance, mechanical issues, or complex dynamics
- âš ï¸ Inconsistent phase estimates across speeds â†’ operating near critical speed

### Physical Checks

If data looks anomalous:
- Is the IMU still attached? (foam tape can fail under vibration)
- Is there mechanical rubbing or interference?
- Are all screws tight?
- Is the motor running smoothly?
- Did anything shift between runs?

---

## Reporting Format

### Per-Step Summary Table

| Step | RPM | Wobble (Â°/s) | GX mean | GY mean | Precession Dir |
|------|-----|--------------|---------|---------|----------------|
| 1 | 245 | 12.3 | -5.2 | 8.1 | 122Â° |
| 2 | 312 | 18.7 | -7.8 | 12.4 | 122Â° |
| ... | ... | ... | ... | ... | ... |

### Aggregate Summary

```
Wobble vs RPMÂ² fit:
  Slope: 2.34 Ã— 10â»âµ Â°/s per RPMÂ²
  Intercept: 3.2 Â°/s
  RÂ²: 0.967

Precession direction:
  Mean: 122.4Â°
  Std: 3.2Â°

Phase analysis (sinusoid fit):
  Weighted phase: 118.7Â°
  Mean RÂ²: 0.62

Balance recommendation:
  Operating regime: Above critical (inferred from low-RPM > high-RPM vibration)
  Measured phase: 120Â°
  Calculated heavy spot: ~36Â°
  Counterweight placement: ~216Â° from hall sensor
```

### A/B Comparison Format

```
                      Baseline    With Weight    Change
Wobble @ 2800 RPM:    45.2 Â°/s    18.7 Â°/s      -59%
Wobble slope:         2.34e-5     0.89e-5       -62%
Precession dir:       122Â°        125Â°          +3Â°
```

---

## Quick Reference

### Key Equations

| Quantity | Formula |
|----------|---------|
| Wobble magnitude | `âˆš(gxÂ² + gyÂ²)` |
| Precession direction | `atan2(gy_mean, gx_mean)` |
| Imbalance force | `F = m Ã— Ï‰Â² Ã— r` |
| Phase lag | `atan2(2Î¶f, 1-fÂ²)` where f = Ï‰/Ï‰n |
| Permissible unbalance | `U = 9549 Ã— G Ã— m / n` (gÂ·mm) |
| Influence coefficient | `C = Î”V / W_trial` |

### Phase Correction Quick Reference

| If operating... | Phase lag | Heavy spot â‰ˆ |
|-----------------|-----------|--------------|
| Well below critical | ~0Â° | measured + 96Â° |
| Well above critical | ~180Â° | measured - 84Â° |
| Unknown | Use influence coefficient method |

### Sensor Orientation Reminder

- **GX, GY:** Wobble/precessionâ€”the signals you care about
- **GZ:** Saturated above 333 RPMâ€”ignore for analysis
- **X accel:** Saturatedâ€”ignore
- **Y, Z accel:** Available for supplementary phase analysis

---

*Document version: 2026-01-24*
*For POV Display balancing telemetry analysis*
*Hardware reference: HARDWARE.md*
