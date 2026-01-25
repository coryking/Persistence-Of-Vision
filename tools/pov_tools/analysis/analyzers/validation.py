"""Validation analyzer: cross-validation and derived insights."""

import matplotlib.pyplot as plt
import numpy as np
from scipy import stats

from ..types import AnalysisContext, AnalysisResult


# Constants
GYRO_SATURATION_DPS = 2000.0  # ±2000°/s gyro range
GYRO_SATURATION_RPM = GYRO_SATURATION_DPS / 6.0  # ~333 RPM
IMU_RADIUS_M = 0.0254  # 25.4mm from rotation center
ACCEL_SATURATION_G = 16.0  # ±16g accelerometer range


def validation_analysis(ctx: AnalysisContext) -> AnalysisResult:
    """Cross-validation and derived insights from telemetry data."""
    plots = []
    findings = []
    metrics: dict = {
        "gyro_vs_hall": {},
        "spin_direction": {},
        "rpm_stability": {},
        "centrifugal": {},
        "phase_consistency": {},
    }

    enriched = ctx.enriched.dropna(subset=["rpm"])
    enriched = enriched[enriched["rpm"] < 10000]  # Remove glitch rotations

    # A. Gyro vs Hall RPM Comparison
    gyro_hall_plot = _gyro_vs_hall_rpm(ctx, enriched, metrics, findings)
    if gyro_hall_plot:
        plots.append(gyro_hall_plot)

    # B. Spin Direction Detection
    _spin_direction_detection(enriched, metrics, findings)

    # C. RPM Stability Per Position
    if ctx.speed_log is not None and "speed_preset" in enriched.columns:
        stability_plot = _rpm_stability_analysis(ctx, enriched, metrics, findings)
        if stability_plot:
            plots.append(stability_plot)

    # D. Centrifugal Force Validation
    _centrifugal_validation(enriched, metrics, findings)

    # E. Phase Consistency (requires phase analysis results - placeholder)
    # This would need access to phase results from other analyzers

    return AnalysisResult(
        name="validation", metrics=metrics, plots=plots, findings=findings
    )


def _gyro_vs_hall_rpm(ctx, enriched, metrics, findings):
    """Compare gyro-derived RPM to hall-derived RPM at low speeds."""
    if "gz_dps" not in enriched.columns:
        return None

    # Filter to low speed samples where gz isn't saturated
    low_speed = enriched[
        (enriched["rpm"] < GYRO_SATURATION_RPM * 0.9)  # 90% of saturation
        & (~enriched["is_gz_saturated"])
    ].copy()

    if len(low_speed) < 100:
        findings.append(
            f"Insufficient low-speed samples ({len(low_speed)}) for gyro vs hall RPM comparison"
        )
        return None

    # Compute gyro-derived RPM: rpm = abs(gz_dps) / 6
    low_speed["rpm_gyro"] = np.abs(low_speed["gz_dps"]) / 6.0

    # Compare to hall-derived RPM
    rpm_hall = low_speed["rpm"].values
    rpm_gyro = low_speed["rpm_gyro"].values

    # Correlation
    correlation = np.corrcoef(rpm_hall, rpm_gyro)[0, 1]
    mean_error = np.mean(rpm_gyro - rpm_hall)
    mean_abs_error = np.mean(np.abs(rpm_gyro - rpm_hall))

    metrics["gyro_vs_hall"]["correlation"] = round(float(correlation), 3)
    metrics["gyro_vs_hall"]["mean_error_rpm"] = round(float(mean_error), 2)
    metrics["gyro_vs_hall"]["mean_abs_error_rpm"] = round(float(mean_abs_error), 2)
    metrics["gyro_vs_hall"]["n_samples"] = len(low_speed)

    if correlation > 0.95:
        findings.append(
            f"Gyro vs Hall RPM: excellent agreement (r={correlation:.3f}, MAE={mean_abs_error:.1f} RPM)"
        )
    elif correlation > 0.8:
        findings.append(
            f"Gyro vs Hall RPM: good agreement (r={correlation:.3f}, MAE={mean_abs_error:.1f} RPM)"
        )
    else:
        findings.append(
            f"Gyro vs Hall RPM: poor agreement (r={correlation:.3f}) - check gyro calibration or mounting"
        )

    # Plot
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

    # Scatter plot
    ax1.scatter(rpm_hall, rpm_gyro, alpha=0.3, s=5)
    min_rpm = min(rpm_hall.min(), rpm_gyro.min())
    max_rpm = max(rpm_hall.max(), rpm_gyro.max())
    ax1.plot([min_rpm, max_rpm], [min_rpm, max_rpm], "r--", label="1:1 line")
    ax1.set_xlabel("Hall RPM")
    ax1.set_ylabel("Gyro RPM")
    ax1.set_title(f"Gyro vs Hall RPM (r={correlation:.3f})")
    ax1.legend()
    ax1.grid(True, alpha=0.3)

    # Error histogram
    errors = rpm_gyro - rpm_hall
    ax2.hist(errors, bins=50, edgecolor="black", alpha=0.7)
    ax2.axvline(x=0, color="r", linestyle="--")
    ax2.axvline(x=mean_error, color="g", linestyle="-", label=f"Mean: {mean_error:.1f}")
    ax2.set_xlabel("Error (Gyro - Hall RPM)")
    ax2.set_ylabel("Count")
    ax2.set_title("RPM Estimation Error Distribution")
    ax2.legend()

    plt.tight_layout()
    plot_path = ctx.output_dir / "plots" / "gyro_vs_hall_rpm.png"
    plt.savefig(plot_path, dpi=150)
    plt.close()

    return plot_path


def _spin_direction_detection(enriched, metrics, findings):
    """Detect spin direction from gz_dps sign at low speed."""
    if "gz_dps" not in enriched.columns:
        return

    # Use low speed samples before saturation
    low_speed = enriched[
        (enriched["rpm"] < GYRO_SATURATION_RPM * 0.8) & (~enriched["is_gz_saturated"])
    ]

    if len(low_speed) < 50:
        findings.append("Insufficient low-speed samples to detect spin direction")
        return

    mean_gz = low_speed["gz_dps"].mean()

    if mean_gz < -10:
        direction = "clockwise (viewed from above, -Z)"
        metrics["spin_direction"]["direction"] = "CW"
    elif mean_gz > 10:
        direction = "counter-clockwise (viewed from above, +Z)"
        metrics["spin_direction"]["direction"] = "CCW"
    else:
        direction = "indeterminate (gz near zero)"
        metrics["spin_direction"]["direction"] = "unknown"

    metrics["spin_direction"]["mean_gz_dps"] = round(float(mean_gz), 1)
    findings.append(f"Spin direction: {direction} (mean gz={mean_gz:.1f}°/s)")


def _rpm_stability_analysis(ctx, enriched, metrics, findings):
    """Analyze RPM stability per speed preset."""
    positions = enriched["speed_preset"].dropna().unique()
    positions = sorted([int(p) for p in positions if not np.isnan(p)])

    if len(positions) < 2:
        return None

    stability_results = []

    for pos in positions:
        pos_data = enriched[enriched["speed_preset"] == pos].copy()

        # Exclude first 2 seconds (transition)
        if "timestamp_us" in pos_data.columns and len(pos_data) > 100:
            t0 = pos_data["timestamp_us"].min()
            pos_data = pos_data[pos_data["timestamp_us"] > t0 + 2_000_000]

        if len(pos_data) < 50:
            continue

        rpm_values = pos_data["rpm"].values
        timestamps = pos_data["timestamp_us"].values

        mean_rpm = np.mean(rpm_values)
        std_rpm = np.std(rpm_values)
        rpm_range = np.max(rpm_values) - np.min(rpm_values)

        # Linear fit for drift
        if len(timestamps) > 10:
            time_seconds = (timestamps - timestamps.min()) / 1e6
            slope, _, _, _, _ = stats.linregress(time_seconds, rpm_values)
            drift_rpm_per_sec = float(slope)
        else:
            drift_rpm_per_sec = 0.0

        stability_results.append(
            {
                "position": pos,
                "mean_rpm": round(mean_rpm, 1),
                "std_rpm": round(std_rpm, 2),
                "rpm_range": round(rpm_range, 1),
                "drift_rpm_per_sec": round(drift_rpm_per_sec, 3),
                "n_samples": len(pos_data),
            }
        )

    if not stability_results:
        return None

    metrics["rpm_stability"]["by_position"] = stability_results

    # Summary statistics
    avg_std = np.mean([r["std_rpm"] for r in stability_results])
    max_std = np.max([r["std_rpm"] for r in stability_results])

    metrics["rpm_stability"]["avg_std_rpm"] = round(float(avg_std), 2)
    metrics["rpm_stability"]["max_std_rpm"] = round(float(max_std), 2)

    if max_std < 5:
        findings.append(f"RPM stability: excellent (max std={max_std:.1f} RPM)")
    elif max_std < 15:
        findings.append(f"RPM stability: good (max std={max_std:.1f} RPM)")
    else:
        findings.append(f"RPM stability: poor (max std={max_std:.1f} RPM) - check motor control")

    # Plot: RPM by position with error bars
    fig, ax = plt.subplots(figsize=(10, 6))

    positions_plot = [r["position"] for r in stability_results]
    means = [r["mean_rpm"] for r in stability_results]
    stds = [r["std_rpm"] for r in stability_results]

    ax.errorbar(positions_plot, means, yerr=stds, fmt="o-", capsize=5, capthick=2)
    ax.set_xlabel("Speed Preset")
    ax.set_ylabel("RPM")
    ax.set_title(f"RPM Stability by Speed Preset\n(Error bars = 1 std, avg std={avg_std:.1f} RPM)")
    ax.grid(True, alpha=0.3)

    # Add annotations for drift
    for r in stability_results:
        if abs(r["drift_rpm_per_sec"]) > 0.1:
            ax.annotate(
                f"drift: {r['drift_rpm_per_sec']:+.2f}/s",
                (r["position"], r["mean_rpm"]),
                textcoords="offset points",
                xytext=(0, 10),
                ha="center",
                fontsize=8,
            )

    plt.tight_layout()
    plot_path = ctx.output_dir / "plots" / "rpm_stability.png"
    plt.savefig(plot_path, dpi=150)
    plt.close()

    return plot_path


def _centrifugal_validation(enriched, metrics, findings):
    """Validate X-axis acceleration against expected centrifugal force."""
    if "x_g" not in enriched.columns:
        return

    # Use low speed samples where X isn't saturated
    low_speed = enriched[
        (enriched["rpm"] < 600)  # Well below X saturation (~720 RPM at 27mm)
        & (~enriched["is_x_saturated"])
    ].copy()

    if len(low_speed) < 100:
        findings.append("Insufficient unsaturated X samples for centrifugal validation")
        return

    # Expected centrifugal: a = ω²r where ω = rpm * 2π/60
    # In g units: a_g = (ω² * r) / 9.81
    rpm_values = low_speed["rpm"].values
    omega = rpm_values * 2 * np.pi / 60
    expected_x_g = (omega**2 * IMU_RADIUS_M) / 9.81

    measured_x_g = low_speed["x_g"].values

    # Correlation and error
    correlation = np.corrcoef(expected_x_g, measured_x_g)[0, 1]
    mean_error = np.mean(measured_x_g - expected_x_g)

    # Estimate effective radius from data
    # If measured = expected * scale, scale gives radius ratio
    if np.var(expected_x_g) > 0:
        slope, intercept, _, _, _ = stats.linregress(expected_x_g, measured_x_g)
        effective_radius_mm = IMU_RADIUS_M * 1000 * slope
    else:
        slope = 1.0
        effective_radius_mm = IMU_RADIUS_M * 1000

    metrics["centrifugal"]["correlation"] = round(float(correlation), 3)
    metrics["centrifugal"]["mean_error_g"] = round(float(mean_error), 3)
    metrics["centrifugal"]["effective_radius_mm"] = round(float(effective_radius_mm), 1)
    metrics["centrifugal"]["assumed_radius_mm"] = IMU_RADIUS_M * 1000

    if correlation > 0.95 and 0.8 < slope < 1.2:
        findings.append(
            f"Centrifugal validation: excellent (r={correlation:.3f}, effective radius={effective_radius_mm:.0f}mm)"
        )
    elif correlation > 0.8:
        findings.append(
            f"Centrifugal validation: good (r={correlation:.3f}, effective radius={effective_radius_mm:.0f}mm vs assumed {IMU_RADIUS_M*1000:.0f}mm)"
        )
    else:
        findings.append(
            f"Centrifugal validation: poor (r={correlation:.3f}) - check sensor orientation or mounting radius"
        )
