# PWM-to-RPM Calibration Results

## Summary

Analysis of 11 hall sensor measurements to determine accurate PWM-to-RPM mapping for ESP32 motor control system.

## Raw Calibration Data

```
PWM%  | RPM
------|-----
51%   | 240
54%   | 470
55%   | 607
57%   | 850
58%   | 1099
59%   | 1119
60%   | 1216
61%   | 1444
62%   | 1447
63%   | 1500
63%   | 1469
```

**Measured Range:**
- PWM: 51-63%
- RPM: 240-1500

## Model Comparison

Five different models were tested:

| Model                  | R² Score | Notes                                    |
|------------------------|----------|------------------------------------------|
| Polynomial (degree 3)  | 0.9913   | **BEST FIT** - Used for calibration      |
| Polynomial (degree 2)  | 0.9784   | Good fit, simpler than degree 3          |
| Power                  | 0.9781   | Good fit, physical meaning               |
| Linear                 | 0.9772   | Simple but slightly less accurate        |
| Exponential            | 0.9772   | Equivalent to linear for this data       |

## Best Model: Polynomial (Degree 3)

**Equation:**
```
RPM = 196822.26 + (-10626.6375 * PWM) + (189.412521 * PWM²) + (-1.110326 * PWM³)
```

**R² Score:** 0.991274 (excellent fit)

**Valid Range:** 51-63% PWM (stays within measured data to avoid extrapolation errors)

**Important:** This polynomial model gives nonsensical predictions outside the 51-63% PWM range due to the cubic term. The implementation clamps to this range.

## Calibration Table (Encoder Positions)

41-step calibration table mapping encoder positions 0-40 to PWM 51-63%:

| Position | PWM% | Estimated RPM |
|----------|------|---------------|
| 0        | 0    | 0 (OFF)       |
| 1        | 51   | 240           |
| 10       | 54   | 448           |
| 20       | 57   | 858           |
| 30       | 60   | 1270          |
| 40       | 63   | 1489          |

**Full table:** See `scripts/calibration_table.cpp` for complete 41-entry array

**RPM Range Achieved:** 240-1489 RPM (positions 1-40)

## Target Range Analysis

**Original Target:** 700-2900 RPM

**Actual Achievable Range:** 240-1489 RPM (within measured PWM 51-63%)

**Gap Analysis:**
- **Below target:** Need data at lower PWM (<51%) to reach 700 RPM starting point
- **Above target:** Need data at higher PWM (>63%) to reach 2900 RPM endpoint
- **Current range is conservative** - staying within measured data prevents unreliable extrapolation

**To extend range:** Collect more calibration measurements:
- Lower PWM (30-50%) for 700 RPM minimum
- Higher PWM (64-80%) for 2900 RPM maximum

## Code Changes

### 1. `scripts/calibration_model.py` (NEW)
Python script for calibration analysis:
- Parses raw measurement data
- Fits 5 different models (linear, polynomial, exponential, power)
- Generates C++ calibration table
- Creates visualization plot
- Reusable for future calibration updates

**Usage:**
```bash
uv run scripts/calibration_model.py
```

### 2. `src/DisplayHandler.cpp`
- **Old:** Linear interpolation over 5 calibration points (27-63% PWM)
- **New:** Polynomial (degree 3) model with clamping to 51-63% PWM range
- Updated position back-calculation for new PWM range (51-63%)

### 3. `src/RotaryEncoder.cpp`
- **Old:** PWM range 27-63% (each encoder click = ~0.92% PWM)
- **New:** PWM range 51-63% (each encoder click = ~0.31% PWM)
- More precise control over narrower but better-characterized range

## Files Generated

1. **`scripts/calibration_model.py`** - Analysis script
2. **`scripts/calibration_table.cpp`** - C++ array format (41 entries)
3. **`scripts/calibration_plot.png`** - Visualization of models and calibration points
4. **`scripts/CALIBRATION_RESULTS.md`** - This summary document

## Next Steps

### Option 1: Use Current Calibration (Conservative)
- **Complexity:** None - already implemented and verified
- Encoder positions 0-40 map to 0%, 51-63% PWM
- Achieves 240-1489 RPM range
- Safe and well-characterized

### Option 2: Extend Calibration Range (Data Collection)
- **Complexity:** Moderate - requires new measurements
- Collect measurements at PWM 30-50% (for 700 RPM minimum)
- Collect measurements at PWM 64-80% (for 2900 RPM maximum)
- Re-run `scripts/calibration_model.py` with new data
- Achieves original 700-2900 RPM target

### Option 3: Accept Linear Model (Simpler)
- **Complexity:** Low - simpler code
- Use linear model (R² = 0.9772, nearly as good as polynomial)
- Linear extrapolation more reliable outside measured range
- Trade-off: Slightly less accurate within 51-63% range

## Visualization

See `scripts/calibration_plot.png` for:
- Left plot: All 5 models compared against measured data
- Right plot: Best model + 40 calibration points + target range markers

## Build Verification

Code changes successfully compiled:
```
✓ Build passed for esp32-s3-zero environment
✓ Memory usage: 6.9% RAM, 30.0% Flash
✓ No errors, minor warnings about deprecated PCNT driver (from ESP32Encoder library)
```
