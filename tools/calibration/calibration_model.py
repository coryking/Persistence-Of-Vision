#!/usr/bin/env python3
"""
PWM-to-RPM Calibration Model
Analyzes hall sensor measurements to determine optimal PWM→RPM mapping
for ESP32 motor control system.
"""

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from sklearn.metrics import r2_score
from sklearn.preprocessing import PolynomialFeatures
from sklearn.linear_model import LinearRegression
from scipy.optimize import curve_fit

# Raw calibration data: PWM% -> measured RPM
# Previous measurements (11 points)
raw_data_old = """
63%, 1500
63%, 1469
57%, 850
60%, 1216
61%, 1444
62%, 1447
51%, 240
54%, 470
58%, 1099
59%, 1119
55%, 607
"""

# New measurements (14 points)
raw_data_new = """
67%, 1985
70%, 2265
79%, 3006
60%, 1143
64%, 1653
68%, 1974
67%, 1942
66%, 1897
69%, 1793
64%, 1798
64%, 1728
63%, 1652
73%, 2535
76%, 2783
"""

# Combined dataset (25 total measurements)
raw_data = raw_data_old + raw_data_new

def parse_calibration_data(data_str):
    """Parse raw calibration string into DataFrame."""
    lines = [line.strip() for line in data_str.strip().split('\n') if line.strip()]
    pwm_values = []
    rpm_values = []

    for line in lines:
        pwm_str, rpm_str = line.split(',')
        pwm = float(pwm_str.strip().replace('%', ''))
        rpm = float(rpm_str.strip())
        pwm_values.append(pwm)
        rpm_values.append(rpm)

    df = pd.DataFrame({'PWM': pwm_values, 'RPM': rpm_values})
    return df.sort_values('PWM')

def linear_model(X, y):
    """Fit linear model."""
    model = LinearRegression()
    model.fit(X.reshape(-1, 1), y)
    y_pred = model.predict(X.reshape(-1, 1))
    r2 = r2_score(y, y_pred)

    # y = mx + b
    equation = f"RPM = {model.coef_[0]:.2f} * PWM + {model.intercept_:.2f}"

    return {
        'name': 'Linear',
        'model': model,
        'r2': r2,
        'equation': equation,
        'predict': lambda pwm: model.predict(np.array(pwm).reshape(-1, 1))
    }

def polynomial_model(X, y, degree):
    """Fit polynomial model."""
    poly = PolynomialFeatures(degree=degree)
    X_poly = poly.fit_transform(X.reshape(-1, 1))
    model = LinearRegression()
    model.fit(X_poly, y)
    y_pred = model.predict(X_poly)
    r2 = r2_score(y, y_pred)

    # Build equation string
    coeffs = model.coef_
    intercept = model.intercept_
    terms = [f"{intercept:.2f}"]
    for i in range(1, len(coeffs)):
        if coeffs[i] != 0:
            if i == 1:
                terms.append(f"{coeffs[i]:.4f}*PWM")
            else:
                terms.append(f"{coeffs[i]:.6f}*PWM^{i}")
    equation = f"RPM = {' + '.join(terms)}"

    return {
        'name': f'Polynomial (degree {degree})',
        'model': model,
        'poly': poly,
        'r2': r2,
        'equation': equation,
        'predict': lambda pwm: model.predict(poly.transform(np.array(pwm).reshape(-1, 1)))
    }

def linear_with_threshold_model(X, y):
    """Fit linear model with static friction threshold: RPM = max(0, a*(PWM - threshold) + b)"""
    def threshold_func(x, a, threshold, b):
        return np.maximum(0, a * (x - threshold) + b)

    # Initial guess: threshold around 40-50%, positive slope
    p0 = [50, 45, 0]

    try:
        params, _ = curve_fit(threshold_func, X, y, p0=p0, maxfev=10000)
        y_pred = threshold_func(X, *params)
        r2 = r2_score(y, y_pred)

        a, threshold, b = params
        equation = f"RPM = max(0, {a:.2f}*(PWM - {threshold:.2f}) + {b:.2f})"

        return {
            'name': 'Linear with Threshold',
            'model': params,
            'r2': r2,
            'equation': equation,
            'predict': lambda pwm: threshold_func(np.array(pwm), *params)
        }
    except RuntimeError:
        return None

def generate_calibration_table(best_model, num_positions=41, target_rpm_min=700, target_rpm_max=2900):
    """
    Generate calibration table for encoder positions 0-40.
    Position 0 = OFF (0% PWM, 0 RPM)
    Positions 1-40 = evenly spaced RPM from target_rpm_min to target_rpm_max

    Uses inverse of model to find required PWM for each target RPM.
    """
    calibration = []

    # Position 0: motor off
    calibration.append({
        'position': 0,
        'pwm_percent': 0,
        'estimated_rpm': 0
    })

    # Positions 1-40: evenly spaced RPM values within target range
    rpm_steps = np.linspace(target_rpm_min, target_rpm_max, num_positions - 1)

    for i, target_rpm in enumerate(rpm_steps, start=1):
        # Find PWM that produces target_rpm using inverse mapping
        # Try PWM values from 0-100% and find closest match
        pwm_range = np.linspace(0, 100, 1000)
        rpm_predictions = best_model['predict'](pwm_range)

        # Find PWM that gives closest RPM to target
        idx = np.argmin(np.abs(rpm_predictions - target_rpm))
        pwm = pwm_range[idx]
        actual_rpm = rpm_predictions[idx]

        calibration.append({
            'position': i,
            'pwm_percent': pwm,
            'estimated_rpm': actual_rpm
        })

    return pd.DataFrame(calibration)

def format_cpp_array(calibration_df):
    """Format calibration table as C++ array."""
    lines = []
    lines.append("// PWM-to-RPM Calibration Table")
    lines.append("// Generated by scripts/calibration_model.py")
    lines.append(f"// Encoder positions 0-40 mapped to target RPM range 700-2900")
    lines.append("// Format: {encoder_position, pwm_percent, estimated_rpm}")
    lines.append("")
    lines.append("struct CalibrationPoint {")
    lines.append("    uint8_t encoder_position;")
    lines.append("    uint8_t pwm_percent;")
    lines.append("    uint16_t estimated_rpm;")
    lines.append("};")
    lines.append("")
    lines.append(f"const CalibrationPoint CALIBRATION_TABLE[{len(calibration_df)}] = {{")

    for _, row in calibration_df.iterrows():
        pos = int(row['position'])
        pwm = int(round(row['pwm_percent']))
        rpm = int(round(row['estimated_rpm']))
        lines.append(f"    {{{pos:2d}, {pwm:3d}, {rpm:4d}}},  // pos {pos:2d}: {pwm:3d}% PWM -> ~{rpm:4d} RPM")

    lines.append("};")
    return '\n'.join(lines)

def plot_calibration(df, models, best_model, calibration_df):
    """Create visualization of models, calibration points, and residuals."""
    fig = plt.figure(figsize=(18, 10))
    gs = fig.add_gridspec(2, 3, hspace=0.3, wspace=0.3)

    ax1 = fig.add_subplot(gs[0, 0])  # All models comparison
    ax2 = fig.add_subplot(gs[0, 1])  # Best model + calibration
    ax3 = fig.add_subplot(gs[0, 2])  # Residuals
    ax4 = fig.add_subplot(gs[1, :])  # Full range extrapolation check

    # Plot 1: All models comparison
    ax1.scatter(df['PWM'], df['RPM'], color='red', s=100, alpha=0.6,
                label='Measured Data', zorder=5)

    pwm_range = np.linspace(df['PWM'].min() - 5, df['PWM'].max() + 5, 200)

    colors = ['blue', 'green', 'orange', 'purple', 'brown']
    for i, model in enumerate(models):
        if model is None:
            continue
        try:
            rpm_pred = model['predict'](pwm_range)
            ax1.plot(pwm_range, rpm_pred, '--', color=colors[i % len(colors)],
                    label=f"{model['name']} (R²={model['r2']:.4f})", linewidth=2)
        except:
            pass

    ax1.set_xlabel('PWM (%)', fontsize=12)
    ax1.set_ylabel('RPM', fontsize=12)
    ax1.set_title('Model Comparison', fontsize=14, fontweight='bold')
    ax1.legend(fontsize=9)
    ax1.grid(True, alpha=0.3)

    # Plot 2: Best model with calibration points
    ax2.scatter(df['PWM'], df['RPM'], color='red', s=100, alpha=0.6,
                label='Measured Data', zorder=5)

    # Best fit curve (limited to measured range)
    pwm_curve = np.linspace(df['PWM'].min(), df['PWM'].max(), 500)
    rpm_curve = best_model['predict'](pwm_curve)
    ax2.plot(pwm_curve, rpm_curve, 'b-', linewidth=2,
            label=f"Best Fit: {best_model['name']}")

    # Calibration points
    cal_pwm = calibration_df['pwm_percent'].values
    cal_rpm = calibration_df['estimated_rpm'].values
    ax2.scatter(cal_pwm[1:], cal_rpm[1:], color='green', s=50, alpha=0.7,
               marker='x', label='Calibration Points (pos 1-40)', zorder=4)

    # Highlight target range
    ax2.axhline(y=700, color='gray', linestyle=':', alpha=0.5, label='Target Range 700-2900 RPM')
    ax2.axhline(y=2900, color='gray', linestyle=':', alpha=0.5)

    ax2.set_xlabel('PWM (%)', fontsize=12)
    ax2.set_ylabel('RPM', fontsize=12)
    ax2.set_title(f'Best Model + Calibration Table\nR² = {best_model["r2"]:.4f}',
                 fontsize=14, fontweight='bold')
    ax2.legend(fontsize=9)
    ax2.grid(True, alpha=0.3)

    # Plot 3: Residual plot
    y_pred = best_model['predict'](df['PWM'].values)
    residuals = df['RPM'].values - y_pred

    ax3.scatter(y_pred, residuals, color='blue', s=80, alpha=0.6)
    ax3.axhline(y=0, color='red', linestyle='--', linewidth=2, label='Zero residual')
    ax3.set_xlabel('Predicted RPM', fontsize=12)
    ax3.set_ylabel('Residual (Actual - Predicted)', fontsize=12)
    ax3.set_title('Residual Plot\n(should be random scatter)', fontsize=14, fontweight='bold')
    ax3.legend(fontsize=9)
    ax3.grid(True, alpha=0.3)

    # Plot 4: Full range extrapolation check (0-100% PWM)
    pwm_full = np.linspace(0, 100, 500)
    rpm_full = best_model['predict'](pwm_full)

    ax4.scatter(df['PWM'], df['RPM'], color='red', s=100, alpha=0.6,
                label='Measured Data', zorder=5)
    ax4.plot(pwm_full, rpm_full, 'b-', linewidth=2,
            label=f"Model: {best_model['name']}")

    # Highlight measured range
    ax4.axvspan(df['PWM'].min(), df['PWM'].max(), alpha=0.2, color='green',
               label='Measured PWM Range')

    # Target RPM range
    ax4.axhline(y=700, color='orange', linestyle=':', linewidth=2, alpha=0.7, label='Target 700-2900 RPM')
    ax4.axhline(y=2900, color='orange', linestyle=':', linewidth=2, alpha=0.7)

    # Safe PWM limit
    ax4.axvline(x=85, color='red', linestyle='--', linewidth=2, alpha=0.7, label='Safe PWM Limit (85%)')

    ax4.set_xlabel('PWM (%)', fontsize=12)
    ax4.set_ylabel('RPM', fontsize=12)
    ax4.set_title('Full Range Extrapolation Check (0-100% PWM)', fontsize=14, fontweight='bold')
    ax4.legend(fontsize=10, loc='upper left')
    ax4.grid(True, alpha=0.3)
    ax4.set_xlim(0, 100)
    ax4.set_ylim(0, max(3500, rpm_full.max() * 1.1) if rpm_full.max() > 0 else 3500)

    return fig

def main():
    print("=" * 80)
    print("PWM-to-RPM Calibration Analysis")
    print("=" * 80)

    # Parse data
    df = parse_calibration_data(raw_data)
    print(f"\nParsed {len(df)} calibration measurements:")
    print(df.to_string(index=False))
    print(f"\nPWM Range: {df['PWM'].min():.1f}% - {df['PWM'].max():.1f}%")
    print(f"RPM Range: {df['RPM'].min():.0f} - {df['RPM'].max():.0f}")

    # Fit ONLY physically grounded models
    X = df['PWM'].values
    y = df['RPM'].values

    print("\n" + "=" * 80)
    print("Testing Physically Grounded Models")
    print("=" * 80)
    print("1. Linear: RPM = a*PWM + b")
    print("   Physical basis: DC motor equation V = I*R + k*ω, approximately linear")
    print("\n2. Linear with Threshold: RPM = max(0, a*(PWM - threshold) + b)")
    print("   Physical basis: Static friction must be overcome before motion")
    print("\n3. Quadratic: RPM = a*PWM² + b*PWM + c")
    print("   Physical basis: Air resistance proportional to speed²")
    print("\nNOT testing cubic/exponential - no physical basis for DC motor!")

    models = [
        linear_model(X, y),
        linear_with_threshold_model(X, y),
        polynomial_model(X, y, degree=2),
    ]

    # Filter out failed models
    models = [m for m in models if m is not None]

    # Find best model by R² (but consider physical plausibility!)
    best_model = max(models, key=lambda m: m['r2'])

    print("\n" + "=" * 80)
    print("Model Comparison (sorted by R² score)")
    print("=" * 80)
    sorted_models = sorted(models, key=lambda m: m['r2'], reverse=True)
    for model in sorted_models:
        marker = "★ BEST" if model == best_model else ""
        print(f"\n{model['name']:25s} R² = {model['r2']:.6f}  {marker}")
        print(f"  {model['equation']}")

    # Generate calibration table for target RPM range
    TARGET_RPM_MIN = 700
    TARGET_RPM_MAX = 2900

    print("\n" + "=" * 80)
    print("Generating 40-step calibration table")
    print("=" * 80)
    print(f"Target RPM range: {TARGET_RPM_MIN}-{TARGET_RPM_MAX} RPM")
    print("Finding required PWM for each target RPM using inverse model...")

    calibration_df = generate_calibration_table(best_model, num_positions=41,
                                                target_rpm_min=TARGET_RPM_MIN,
                                                target_rpm_max=TARGET_RPM_MAX)

    # Show sample of table
    print("\nCalibration Table (first 10 and last 5 entries):")
    sample = pd.concat([calibration_df.head(10), calibration_df.tail(5)])
    print(sample.to_string(index=False))

    pwm_min_required = calibration_df['pwm_percent'].iloc[1]
    pwm_max_required = calibration_df['pwm_percent'].iloc[-1]
    rpm_min_actual = calibration_df['estimated_rpm'].iloc[1]
    rpm_max_actual = calibration_df['estimated_rpm'].iloc[-1]

    print(f"\n" + "=" * 80)
    print("PWM Range Analysis")
    print("=" * 80)
    print(f"To achieve {TARGET_RPM_MIN}-{TARGET_RPM_MAX} RPM:")
    print(f"  Required PWM range: {pwm_min_required:.1f}% - {pwm_max_required:.1f}%")
    print(f"  Actual RPM range:   {rpm_min_actual:.0f} - {rpm_max_actual:.0f} RPM")

    # Safety check
    PWM_SAFE_MAX = 85.0
    if pwm_max_required > PWM_SAFE_MAX:
        print(f"\n⚠️  WARNING: Max PWM {pwm_max_required:.1f}% exceeds safe limit of {PWM_SAFE_MAX}%!")
        print(f"    Target RPM of {TARGET_RPM_MAX} may not be achievable safely.")
        print(f"    Consider lowering target RPM or accepting higher PWM risk.")
    else:
        print(f"\n✓ PWM range is SAFE (max {pwm_max_required:.1f}% < {PWM_SAFE_MAX}% limit)")

    # Measured range check
    pwm_min_measured = df['PWM'].min()
    pwm_max_measured = df['PWM'].max()
    print(f"\nMeasured PWM range: {pwm_min_measured:.1f}% - {pwm_max_measured:.1f}%")

    if pwm_min_required < pwm_min_measured:
        print(f"⚠️  Extrapolating below measured range for low RPM (down to {pwm_min_required:.1f}%)")
    if pwm_max_required > pwm_max_measured:
        print(f"⚠️  Extrapolating above measured range for high RPM (up to {pwm_max_required:.1f}%)")
    if pwm_min_required >= pwm_min_measured and pwm_max_required <= pwm_max_measured:
        print(f"✓ Entire calibration range within measured data - no extrapolation needed")

    # Generate C++ code
    print("\n" + "=" * 80)
    print("C++ Calibration Array")
    print("=" * 80)
    cpp_code = format_cpp_array(calibration_df)
    print(cpp_code)

    # Save C++ code to file
    cpp_output_path = '/Users/coryking/projects/POV_IS_COOL/scripts/calibration_table.cpp'
    with open(cpp_output_path, 'w') as f:
        f.write(cpp_code)
    print(f"\n✓ Saved to: {cpp_output_path}")

    # Generate plot
    fig = plot_calibration(df, models, best_model, calibration_df)
    plot_path = '/Users/coryking/projects/POV_IS_COOL/scripts/calibration_plot.png'
    fig.savefig(plot_path, dpi=150, bbox_inches='tight')
    print(f"✓ Plot saved to: {plot_path}")

    print("\n" + "=" * 80)
    print("Analysis Complete")
    print("=" * 80)
    print(f"\nBest Model: {best_model['name']}")
    print(f"R² Score: {best_model['r2']:.6f}")
    print(f"Equation: {best_model['equation']}")
    print(f"\nPhysical Interpretation:")
    if 'Linear with Threshold' in best_model['name']:
        print(f"  Motor has static friction threshold - needs minimum PWM to start")
        print(f"  Once moving, RPM increases linearly with PWM (classic DC motor)")
    elif best_model['name'] == 'Linear':
        print(f"  Classic DC motor behavior: RPM proportional to average voltage")
        print(f"  Minimal static friction effects (already overcome at measured PWM range)")
    elif 'Polynomial (degree 2)' in best_model['name']:
        print(f"  Quadratic term suggests air resistance becomes significant at higher RPM")
        print(f"  Drag force proportional to speed² affects power-to-speed relationship")

    print(f"\nCalibration Mapping:")
    print(f"  Encoder Position 0: OFF (0% PWM, 0 RPM)")
    print(f"  Encoder Positions 1-40: {TARGET_RPM_MIN}-{TARGET_RPM_MAX} RPM in equal steps")
    print(f"  Required PWM range: {pwm_min_required:.1f}% - {pwm_max_required:.1f}%")

    print(f"\nNext Steps:")
    print(f"  1. Review residual plot - should show random scatter, not patterns")
    print(f"  2. Check extrapolation plot - does model behave reasonably outside measured range?")
    print(f"  3. Update DisplayHandler.cpp with new estimateRPM() function")
    print(f"  4. Update RotaryEncoder.cpp with PWM_MIN={pwm_min_required:.1f}, PWM_MAX={pwm_max_required:.1f}")

    return calibration_df, best_model

if __name__ == "__main__":
    calibration_df, best_model = main()
