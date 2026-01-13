# POV Display Telemetry Analysis Guide

## Overview

This document describes the methodology for analyzing telemetry data from the POV display's IMU (accelerometer + gyroscope) and Hall effect sensor to:
1. Characterize rotational imbalance (wobble)
2. Determine optimal counterweight placement
3. Compare before/after balance adjustments

---

## Data Acquisition Workflow

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

### Input: Telemetry Directory

You'll be given a path like:
```
/Users/coryking/projects/POV_Project/telemetry/2026-01-12T16-17-47
```

### Directory Structure (Per-Step Capture)

Each speed position gets its own subdirectory with separate recordings:

```
telemetry/2026-01-12T16-17-47/
├── speed_01/
│   ├── MSG_ACCEL_SAMPLES.csv
│   ├── MSG_HALL_EVENT.csv
│   └── MSG_TELEMETRY.csv
├── speed_02/
│   └── ...
├── ...
├── speed_10/
│   └── ...
└── manifest.json
```

### Expected Files

| File | Description |
|------|-------------|
| `speed_XX/MSG_ACCEL_SAMPLES.csv` | IMU data (accel + gyro) with pre-computed engineering units for speed position XX |
| `speed_XX/MSG_HALL_EVENT.csv` | Hall effect sensor triggers with period/RPM for speed position XX |
| `manifest.json` | Run metadata (timing, sample counts, RPM per step) |

### Step 1: Copy Files to Claude's Environment

```
Use Filesystem:copy_file_user_to_claude for each CSV file
Files will appear in /mnt/user-data/uploads/
```

### Step 2: Quick Data Inspection

Check file sizes, column headers, and sample values before full analysis.

---

## Data Format (Current)

### MSG_ACCEL_SAMPLES.csv Columns

| Column | Description | Units |
|--------|-------------|-------|
| `timestamp_us` | Microsecond timestamp | μs |
| `sequence_num` | Sample sequence number | - |
| `rotation_num` | Rotation count since start | - |
| `micros_since_hall` | Time since last hall trigger | μs |
| `angle_deg` | **Pre-computed** angular position | degrees (0-360) |
| `rpm` | **Pre-computed** RPM from gyro | RPM |
| `x_g`, `y_g`, `z_g` | Accelerometer in g's | g |
| `gx_dps`, `gy_dps`, `gz_dps` | Gyroscope in degrees/second | °/s |
| `gyro_wobble_dps` | **Pre-computed** wobble magnitude | °/s |
| `is_y_saturated` | Y-axis accel saturation flag | bool |
| `is_gz_saturated` | Z-axis gyro saturation flag | bool |

### MSG_HALL_EVENT.csv Columns

| Column | Description |
|--------|-------------|
| `timestamp_us` | When hall sensor triggered |
| `period_us` | Time since previous trigger |
| `rotation_num` | Rotation count |

### manifest.json Structure

```json
{
  "settle_time": 3.0,
  "record_time": 5.0,
  "speeds_captured": [1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
  "started_at": "2026-01-12T16:17:47",
  "completed_at": "2026-01-12T16:30:22",
  "aborted": false,
  "error": null,
  "steps": [
    {
      "position": 1,
      "accel_samples": 4500,
      "hall_packets": 150,
      "rpm": 450.0,
      "success": true
    },
    ...
  ]
}
```

| Field | Description |
|-------|-------------|
| `settle_time` | Seconds waited after speed change before recording |
| `record_time` | Seconds of recording at each speed |
| `speeds_captured` | List of successfully captured speed positions |
| `steps[].accel_samples` | IMU samples collected during recording |
| `steps[].hall_packets` | Hall events during recording |
| `steps[].rpm` | RPM at end of recording |

---

## Test Structure

### Per-Step Capture Protocol

For each speed position (1-10):
1. **Delete** telemetry files on device (fresh start)
2. **Speed up** to target position
3. **Settle** for configurable time (default 3s) - motor reaches steady-state
4. **Record** for configurable time (default 5s) - capture pure steady-state data
5. **Stop** recording and **dump** to speed-specific subdirectory

**Key advantage**: Each speed's data is captured after the motor has fully stabilized at that speed, eliminating the spin-up transient that contaminates continuous-capture data.

### Steady-State Data

With per-step capture, the entire recording is steady-state data (the settle time handles spin-up). No windowing is needed - use all samples in each file.

---

## Sensor Orientation & Known Limitations

> **Canonical hardware reference:** See `docs/led_display/HARDWARE.md` for physical assembly and sensor specs.

**Rotation direction:** Counter-clockwise when viewed from above (software-configurable).

### IMU Mounting

- **Mounting method**: Double-sided foam tape, upside-down (chip facing floor)
- **Alignment**: Approximately aligned, not precision-mounted
- **Z+**: Points DOWN (toward ground)
- **Y+**: Points radially outward (saturates from centrifugal force)
- **X+**: Tangent to rotation

### Expected Saturation (NORMAL!)

| Axis | Saturation Expected? | Why |
|------|---------------------|-----|
| **GZ (gyro)** | ✅ YES | Main rotation axis, ±2000°/s limit exceeded above ~333 RPM. **Negative** during CCW rotation. |
| **Y accel** | ✅ YES | Points radially outward, normal to the axis of rotation, centripetal acceleration exceeds ±16g |
| GX, GY (gyro) | ❌ No | Wobble/precession axes |
| X, Z accel | ❌ No | Tangential and axial, lower forces |

### What This Means

- **Ignore GZ for RPM calculation** at speeds above ~333 RPM (use Hall sensor or pre-computed `rpm` column)
- **GX and GY are the money** - they show wobble/precession
- **Y accel saturation is fine** - we use X and Z for phase analysis

---

## The Physics & Math

### Imbalance Force

An imbalanced rotating mass creates centrifugal force:

```
F = m × ω² × r

Where:
  m = imbalance mass (kg)
  ω = angular velocity (rad/s) = RPM × 2π/60
  r = radius of imbalance from rotation axis (m)
```

**Key insight**: Force scales with **ω²** (RPM squared), so imbalance effects grow quadratically with speed.

### Wobble Response

The imbalance force causes the disc to tilt/precess. The gyroscope's X and Y axes (perpendicular to rotation) measure this wobble:

```
wobble_magnitude = sqrt(gx² + gy²)  [°/s]

Expected relationship:
  wobble ∝ RPM²
  
Fit model:
  wobble = slope × RPM² + intercept
```

A good fit (R² > 0.9) with positive slope confirms classic imbalance.

### Precession Direction

The **DC offset** of GX and GY indicates the tilt direction:

```python
precession_direction = atan2(mean(gy), mean(gx))  # degrees
```

This direction should be **consistent across speeds**å

The precession axis is related to (but not exactly equal to) the heavy spot location.

### Phase Analysis

To find where the heavy spot is relative to the Hall sensor:

1. Bin samples by `angle_deg` (e.g., 10° bins)
2. Average sensor values in each bin
3. Fit a sinusoid: `y = A × sin(angle - φ) + offset`
4. The phase φ indicates the heavy spot position

```python
def sinusoid(x, amp, phase, offset):
    return amp * np.sin(np.radians(x - phase)) + offset
```

**Quality metric**: R² of the sinusoid fit
- R² > 0.3: Good, trust this estimate
- R² 0.1-0.3: Weak but usable
- R² < 0.1: Too noisy, discard

### Counterweight Calculation

```
Counterweight moment = mass × radius

To balance:
  m_counterweight × r_counterweight = m_imbalance × r_imbalance

Placement:
  angle_counterweight = angle_heavy_spot + 180°
```

---

## Counterweight Options

Cory is using coins as balancing weights.  We want to find the correct location for the correct weight.

### Available Coins (US Currency)

| Coin | Mass |
|------|------|
| Dime | 2.27g |
| Penny | 2.50g |
| Nickel | 5.00g |
| Quarter | 5.67g |

### Tuning Strategy

**Two adjustment axes**:

1. **Mass**: Swap coin type (discrete steps)
2. **Radius**: Move coin in/out (continuous adjustment)

Since `moment = mass × radius`:
- Moving a nickel **inward by 10%** reduces moment by 10%
- This is more precise than swapping between coin types

### Typical Process

1) Determine what angle to use relative to the hall effect sensor and establish a baseline with no counterbalance applied
2) Place a weight somewhere along that angle
3) Run the test, collect the data
4) Compare against baseline
5) Make adjustments by moving the current coin in or out radialially or changing the coin for a lighter or heavier one.
6) Goto step three until happy with result.

### Notes

* It's a pain to adjust the weight, it's held on by painters tape.  This is why i'd like to first establish a fixed angle along the top of the rotor to place weights.  I'll draw it in with a permanent marker or something.

---

## Analysis Sequence

### For a Single Dataset (Per-Step Capture)

```python
import json
from pathlib import Path

# 1. Load manifest
base_dir = Path('/mnt/user-data/uploads/telemetry_run')
with open(base_dir / 'manifest.json') as f:
    manifest = json.load(f)

# 2. Load data from each speed directory
results = []
for step in manifest['steps']:
    if not step['success']:
        continue
    pos = step['position']
    step_dir = base_dir / f'speed_{pos:02d}'

    df = pd.read_csv(step_dir / 'MSG_ACCEL_SAMPLES.csv')
    hall_df = pd.read_csv(step_dir / 'MSG_HALL_EVENT.csv')

    # All samples are steady-state (no windowing needed)
    results.append({
        'position': pos,
        'rpm': step['rpm'],
        'accel_df': df,
        'hall_df': hall_df,
    })

# 3. Per-step analysis (on full dataset, no windowing):
#    - Mean RPM (from manifest or df['rpm'].mean())
#    - Mean wobble (gyro_wobble_dps)
#    - Mean GX, GY
#    - Precession direction = atan2(gy_mean, gx_mean)
#    - Phase analysis via sinusoid fit

# 4. Aggregate analysis:
#    - Wobble vs RPM² fit (expect R² > 0.9)
#    - Precession direction consistency (expect std < 5°)
#    - Weighted phase estimate for counterweight placement

# 5. Generate plots:
#    - Wobble vs RPM with quadratic fit
#    - Precession direction by step
#    - GX vs GY (should be linear, ~45° line)
#    - Angle-binned sensor data at different speeds
#    - Polar plot of phase estimates
```

### For A/B Comparison (With vs Without Weight)

```python
# 1. Analyze both datasets independently

# 2. Compare:
#    - Wobble at each speed step (% change)
#    - Precession direction shift
#    - Wobble slope (×10⁻⁵) change

# 3. Interpret:
#    - Wobble increased without weight → weight was helping
#    - Crossover behavior (helps at low RPM, hurts at high) → weight too heavy or slightly misplaced
#    - Precession direction stable → intrinsic imbalance direction unchanged
```

---

## Key Metrics to Report

### Per-Step Table

| Step | RPM | Wobble (°/s) | GX | GY | Prec. Dir |
|------|-----|--------------|----|----|-----------|

### Aggregate Summary

```
Wobble vs RPM² fit:
  - Slope: X.XX × 10⁻⁵
  - R²: 0.XXX
  
Precession direction:
  - Mean: XX.X°
  - Std: X.X°
  
Balance recommendation:
  - Heavy spot: ~XX° from hall sensor
  - Counterweight: ~XX° from hall sensor
```

### For Comparisons

```
                    Config A    Config B    Delta
Wobble slope:       X.XX        X.XX        ±X.XX
Precession dir:     XX.X°       XX.X°       ±X.X°
Avg wobble change:  -           -           ±XX.X%
```

## Calibration Info

> **Canonical hardware reference:** See `docs/led_display/HARDWARE.md` for full hall effect sensor specs.

### From ISR Timestamp to Physical Position

The telemetry timestamp chain:

```
Physical event          Electrical signal       Software capture
─────────────────────────────────────────────────────────────────
Magnet field exceeds  → A3144 output falls   → GPIO_INTR_NEGEDGE  → esp_timer_get_time()
operate threshold       (0.18μs typical)        ISR fires            records timestamp
```

**Timing delays in this chain:**

| Stage | Delay | Angular error @ 2800 RPM |
|-------|-------|--------------------------|
| Electrical (A3144 fall time) | 0.18μs typ, 2.0μs max | 0.003° typ, 0.034° max |
| ISR latency (software jitter) | 1-2μs typical | 0.017-0.034° |
| **Total electrical + software** | **~2μs** | **~0.03°** |

The electrical and software timing are **negligible**. The dominant timing factor is the magnetic field geometry.

### Hall Effect Sensor Angular Offset

The hall sensor triggers **before** reaching the magnet due to magnetic field geometry.

**Physical setup:**
- Sensor/magnet radius: 52mm from rotation center
- Air gap at closest approach: 3.5mm
- Magnet: 5x2mm neodymium disc (~2500-3000 Gauss at surface)
- A3144 operate threshold: 35-450 Gauss (wide tolerance, ~50 Gauss assumed)

**Calculated offset:**

| Parameter | Value |
|-----------|-------|
| Estimated field at 3.5mm gap | ~300 Gauss |
| Assumed operate threshold | ~50 Gauss |
| Trigger distance (total) | ~6.4mm |
| Arc distance at trigger | ~5.3mm |
| **Angular offset** | **~6°** |

**What this means:**
- `angle_deg = 0°` in telemetry = sensor is ~6° before the magnet (still approaching)
- `angle_deg ≈ 6°` in telemetry = sensor at closest approach to magnet
- The magnet position corresponds to **+6°** in the telemetry reference frame

**For counterweight placement:**
```
physical_angle_from_magnet = telemetry_angle - 6°
```

If analysis says "place counterweight at 180°" (from hall reference), the physical placement is ~174° from the magnet.

**Uncertainty:** ±2-3° due to:
- A3144 operate point variation (35-450 Gauss range)
- Magnetic field estimation
- Air gap measurement tolerance

### Verifying the Offset Empirically

To verify this offset:
1. Mark a visible reference point on the rotor at a known angle from the hall sensor
2. Capture telemetry at low RPM (where timing is most accurate)
3. Look for any signal anomaly (vibration, accelerometer spike) at the reference point
4. Compare expected vs measured angle in telemetry

## Interpretation Guidelines

### Good Balance Indicators

- ✅ Low wobble magnitude across all speeds
- ✅ Precession direction std < 5°
- ✅ Wobble vs RPM² has high R² (system is predictable)
- ✅ Counterweight reduces wobble at ALL speeds (no crossover)

### Warning Signs

- ⚠️ Crossover behavior (weight helps at low RPM, hurts at high) → adjust counterweight moment
- ⚠️ Precession direction shifts significantly between configs → weight affecting balance axis
- ⚠️ Low R² on wobble fit → possible resonance or mechanical issues
- ⚠️ Inconsistent phase estimates across speeds → sensor noise or complex dynamics

### Physical Checks

If data looks weird:
- Is the IMU still attached? (tape can fail)
- Is there mechanical rubbing or interference?
- Are all screws tight?
- Is the motor running smoothly?

---

## Quick Reference: Analysis Command

When given a telemetry directory, the analysis flow is:

1. **Copy files** using `Filesystem:copy_file_user_to_claude`
2. **Quick inspection** of file sizes and headers
3. **Run step-windowed analysis** (last 3s of each 5s step)
4. **Calculate per-step metrics**: RPM, wobble, GX/GY means, precession direction
5. **Fit wobble vs RPM²** to confirm imbalance signature
6. **Phase analysis** to find heavy spot angle
7. **Generate comparison plots** if doing A/B test
8. **Report recommendations** for counterweight adjustment

---

## Appendix: Example Python Imports

```python
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy.optimize import curve_fit
from scipy.stats import linregress
import warnings
warnings.filterwarnings('ignore')
```

---

*Document created: 2026-01-12*
*For POV Display balancing telemetry analysis*
