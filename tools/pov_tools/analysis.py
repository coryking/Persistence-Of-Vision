"""Telemetry analysis module with pluggable analyzers."""

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

import matplotlib.pyplot as plt
import matplotlib.cm as cm
import numpy as np
import pandas as pd


# =============================================================================
# Data Structures
# =============================================================================


@dataclass
class AnalysisContext:
    """Shared context passed to all analyzers.

    Contains raw data, enriched data with computed columns, and output config.
    """

    accel: pd.DataFrame  # Raw accel (timestamp_us, sequence_num, x, y, z)
    hall: pd.DataFrame  # Raw hall (timestamp_us, period_us, rotation_num)
    enriched: pd.DataFrame  # Accel merged with hall + computed columns
    output_dir: Path  # Where to write plots

    # Pre-computed in enriched DataFrame:
    # - rpm: from period_us
    # - phase: 0-1 within rotation
    # - phase_deg: 0-360
    # - x_g, y_g, z_g: converted to g units (3.9mg/LSB)
    # - is_y_saturated: boolean (raw y >= 4094)


@dataclass
class AnalysisResult:
    """Output from one analysis module."""

    name: str  # e.g., "rpm_sweep"
    metrics: dict  # Structured data for JSON output
    plots: list[Path] = field(default_factory=list)  # Generated plot files
    findings: list[str] = field(default_factory=list)  # Human-readable bullet points


# Type alias for analyzer functions
Analyzer = Callable[[AnalysisContext], AnalysisResult]


# =============================================================================
# Data Loading and Enrichment
# =============================================================================


def load_and_enrich(data_dir: Path) -> AnalysisContext:
    """Load CSVs and compute derived columns.

    Args:
        data_dir: Directory containing MSG_ACCEL_SAMPLES.csv and MSG_HALL_EVENT.csv

    Returns:
        AnalysisContext with raw and enriched data
    """
    accel_path = data_dir / "MSG_ACCEL_SAMPLES.csv"
    hall_path = data_dir / "MSG_HALL_EVENT.csv"

    if not accel_path.exists():
        raise FileNotFoundError(f"Accelerometer data not found: {accel_path}")
    if not hall_path.exists():
        raise FileNotFoundError(f"Hall event data not found: {hall_path}")

    accel = pd.read_csv(accel_path)
    hall = pd.read_csv(hall_path)

    # Create enriched dataframe
    enriched = accel.merge(
        hall[["rotation_num", "period_us"]], on="rotation_num", how="left"
    )

    # Convert to g units (ADXL345: 3.9mg/LSB at full resolution)
    for axis in ["x", "y", "z"]:
        enriched[f"{axis}_g"] = enriched[axis] * 0.00390625

    # Compute RPM from period
    enriched["rpm"] = 60_000_000 / enriched["period_us"]

    # Compute phase within rotation (0-1)
    enriched["phase"] = (enriched["micros_since_hall"] / enriched["period_us"]) % 1.0
    enriched["phase_deg"] = enriched["phase"] * 360

    # Mark saturated Y samples (raw value at or near max)
    enriched["is_y_saturated"] = enriched["y"] >= 4094

    # Create plots directory
    plots_dir = data_dir / "plots"
    plots_dir.mkdir(exist_ok=True)

    return AnalysisContext(
        accel=accel, hall=hall, enriched=enriched, output_dir=data_dir
    )


# =============================================================================
# Analyzer Functions
# =============================================================================


def data_quality_analysis(ctx: AnalysisContext) -> AnalysisResult:
    """Analyze data quality: sample rates, gaps, timing consistency."""
    accel = ctx.accel
    hall = ctx.hall

    metrics: dict = {}
    findings: list[str] = []

    # === ACCELEROMETER SAMPLE TIMING ===
    sample_intervals = accel["timestamp_us"].diff().dropna()

    # Expected interval at 800Hz = 1250us
    expected_interval_us = 1250

    sample_rate_hz = 1_000_000 / sample_intervals.mean()
    interval_std_us = sample_intervals.std()
    interval_min_us = sample_intervals.min()
    interval_max_us = sample_intervals.max()

    # Jitter as percentage of expected interval
    jitter_pct = (interval_std_us / expected_interval_us) * 100

    metrics["sample_timing"] = {
        "rate_hz": round(sample_rate_hz, 1),
        "interval_mean_us": round(sample_intervals.mean(), 1),
        "interval_std_us": round(interval_std_us, 1),
        "interval_min_us": round(interval_min_us, 1),
        "interval_max_us": round(interval_max_us, 1),
        "jitter_pct": round(jitter_pct, 2),
    }

    findings.append(f"Sample rate: {sample_rate_hz:.1f} Hz (expected 800 Hz)")
    findings.append(f"Sample interval: {sample_intervals.mean():.0f} ± {interval_std_us:.0f} µs")
    findings.append(f"Timing jitter: {jitter_pct:.1f}%")

    # === SEQUENCE GAPS (dropped samples) ===
    seq_diff = accel["sequence_num"].diff().dropna()
    gaps = seq_diff[seq_diff != 1]
    gap_count = len(gaps)
    total_dropped = int((gaps - 1).sum()) if gap_count > 0 else 0

    metrics["sequence_gaps"] = {
        "gap_count": gap_count,
        "total_dropped_samples": total_dropped,
        "drop_rate_pct": round(100 * total_dropped / len(accel), 3) if len(accel) > 0 else 0,
    }

    if gap_count > 0:
        findings.append(f"Sequence gaps: {gap_count} (dropped ~{total_dropped} samples, {metrics['sequence_gaps']['drop_rate_pct']:.2f}%)")
    else:
        findings.append("No sequence gaps detected (no dropped samples)")

    # === HALL EVENT TIMING ===
    hall_intervals = hall["timestamp_us"].diff().dropna()
    hall_clean = hall[hall["period_us"] < 200000]  # Filter out glitches for stats

    if len(hall_clean) > 1:
        period_std = hall_clean["period_us"].std()
        period_mean = hall_clean["period_us"].mean()
        period_cv = (period_std / period_mean) * 100  # Coefficient of variation

        metrics["hall_timing"] = {
            "events": len(hall),
            "period_mean_us": round(period_mean, 1),
            "period_std_us": round(period_std, 1),
            "period_cv_pct": round(period_cv, 2),
        }
        findings.append(f"Hall events: {len(hall)} (period CV: {period_cv:.1f}%)")

    # === SAMPLES PER ROTATION ===
    samples_per_rot = ctx.enriched.groupby("rotation_num").size()

    metrics["samples_per_rotation"] = {
        "min": int(samples_per_rot.min()),
        "max": int(samples_per_rot.max()),
        "mean": round(samples_per_rot.mean(), 1),
        "std": round(samples_per_rot.std(), 1),
    }

    findings.append(
        f"Samples per rotation: {samples_per_rot.mean():.0f} ± {samples_per_rot.std():.0f} "
        f"(range: {samples_per_rot.min()}-{samples_per_rot.max()})"
    )

    # === PHASE COVERAGE ===
    # Check if all phase bins are represented (good for FFT)
    n_bins = 36
    enriched = ctx.enriched.dropna(subset=["phase"])
    enriched = enriched.copy()
    enriched["phase_bin"] = (enriched["phase"] * n_bins).astype(int) % n_bins
    bins_covered = enriched["phase_bin"].nunique()

    metrics["phase_coverage"] = {
        "bins_total": n_bins,
        "bins_covered": bins_covered,
        "coverage_pct": round(100 * bins_covered / n_bins, 1),
    }

    if bins_covered < n_bins:
        findings.append(f"Phase coverage: {bins_covered}/{n_bins} bins ({metrics['phase_coverage']['coverage_pct']:.0f}%)")
    else:
        findings.append(f"Phase coverage: 100% ({n_bins} bins)")

    # === TIMESTAMP SANITY ===
    duration_s = (accel["timestamp_us"].max() - accel["timestamp_us"].min()) / 1e6
    expected_samples = duration_s * 800
    actual_samples = len(accel)
    capture_efficiency = (actual_samples / expected_samples) * 100 if expected_samples > 0 else 0

    metrics["capture_stats"] = {
        "duration_s": round(duration_s, 2),
        "total_samples": actual_samples,
        "expected_samples": int(expected_samples),
        "capture_efficiency_pct": round(capture_efficiency, 1),
    }

    findings.append(f"Capture duration: {duration_s:.1f}s, efficiency: {capture_efficiency:.0f}%")

    # === Y-AXIS SATURATION ===
    y_saturated = (accel["y"] >= 4094).sum()
    y_sat_pct = 100 * y_saturated / len(accel)

    metrics["saturation"] = {
        "y_saturated_samples": int(y_saturated),
        "y_saturation_pct": round(y_sat_pct, 1),
    }

    if y_sat_pct > 5:
        findings.append(f"Y-axis saturation: {y_sat_pct:.1f}% of samples clipped at ±16g")

    return AnalysisResult(
        name="data_quality",
        metrics=metrics,
        plots=[],  # No plots for data quality
        findings=findings,
    )


def rpm_sweep_analysis(ctx: AnalysisContext) -> AnalysisResult:
    """Analyze RPM progression over time."""
    hall = ctx.hall.copy()
    hall["rpm"] = 60_000_000 / hall["period_us"]
    hall["time_s"] = (hall["timestamp_us"] - hall["timestamp_us"].min()) / 1e6

    # Detect glitches (impossibly high RPM)
    glitch_mask = hall["rpm"] > 10000
    glitch_count = glitch_mask.sum()
    hall_clean = hall[~glitch_mask]

    # Stats
    rpm_min = hall_clean["rpm"].min()
    rpm_max = hall_clean["rpm"].max()
    duration_s = hall_clean["time_s"].max()

    # Generate plot
    fig, ax = plt.subplots(figsize=(10, 4))
    ax.plot(hall_clean["time_s"], hall_clean["rpm"], "b-", linewidth=0.8)
    ax.set_xlabel("Time (seconds)")
    ax.set_ylabel("RPM")
    ax.set_title("RPM Sweep Over Time")
    ax.grid(True, alpha=0.3)

    # Mark glitches if any
    if glitch_count > 0:
        glitch_times = hall[glitch_mask]["time_s"]
        for t in glitch_times:
            ax.axvline(x=t, color="red", linestyle="--", alpha=0.5, linewidth=0.5)
        ax.text(
            0.02,
            0.98,
            f"{glitch_count} glitch(es) detected",
            transform=ax.transAxes,
            fontsize=9,
            color="red",
            verticalalignment="top",
        )

    plt.tight_layout()
    plot_path = ctx.output_dir / "plots" / "rpm_sweep.png"
    plt.savefig(plot_path, dpi=150)
    plt.close()

    findings = [
        f"RPM range: {rpm_min:.0f} to {rpm_max:.0f}",
        f"Duration: {duration_s:.1f} seconds",
        f"Rotations captured: {len(hall_clean)}",
    ]
    if glitch_count > 0:
        findings.append(f"Hall sensor glitches detected: {glitch_count}")

    return AnalysisResult(
        name="rpm_sweep",
        metrics={
            "rpm_min": round(rpm_min, 1),
            "rpm_max": round(rpm_max, 1),
            "duration_s": round(duration_s, 1),
            "rotations": len(hall_clean),
            "hall_glitches": int(glitch_count),
        },
        plots=[plot_path],
        findings=findings,
    )


def axis_timeseries_analysis(ctx: AnalysisContext) -> AnalysisResult:
    """Generate time series plots for each axis."""
    # Use first 2 seconds of data (low speed, patterns visible)
    early = ctx.enriched[
        ctx.enriched["timestamp_us"] < ctx.enriched["timestamp_us"].min() + 2e6
    ].copy()
    early["time_s"] = (early["timestamp_us"] - early["timestamp_us"].min()) / 1e6

    plots = []
    axis_stats = {}

    for axis, color in [("x", "red"), ("y", "green"), ("z", "blue")]:
        col = f"{axis}_g"

        fig, ax = plt.subplots(figsize=(10, 3))
        ax.plot(early["time_s"], early[col], color=color, linewidth=0.5, alpha=0.8)
        ax.set_xlabel("Time (seconds)")
        ax.set_ylabel(f"{axis.upper()}-axis (g)")
        ax.set_title(f"{axis.upper()}-Axis Acceleration (first 2 seconds)")
        ax.grid(True, alpha=0.3)

        plt.tight_layout()
        plot_path = ctx.output_dir / "plots" / f"axis_{axis}.png"
        plt.savefig(plot_path, dpi=150)
        plt.close()
        plots.append(plot_path)

        # Stats for full dataset
        full_col = ctx.enriched[col]
        axis_stats[axis] = {
            "min": round(float(full_col.min()), 3),
            "max": round(float(full_col.max()), 3),
            "mean": round(float(full_col.mean()), 3),
            "std": round(float(full_col.std()), 4),
        }

    # Check saturation
    y_saturated_pct = ctx.enriched["is_y_saturated"].mean() * 100

    findings = [
        f"X-axis range: {axis_stats['x']['min']:.2f}g to {axis_stats['x']['max']:.2f}g",
        f"Y-axis range: {axis_stats['y']['min']:.2f}g to {axis_stats['y']['max']:.2f}g",
        f"Z-axis range: {axis_stats['z']['min']:.2f}g to {axis_stats['z']['max']:.2f}g",
    ]
    if y_saturated_pct > 1:
        findings.append(f"Y-axis saturated: {y_saturated_pct:.1f}% of samples at +16g")

    return AnalysisResult(
        name="axis_timeseries",
        metrics={
            "axis_stats": axis_stats,
            "y_saturation_pct": round(y_saturated_pct, 1),
            "samples_plotted": len(early),
        },
        plots=plots,
        findings=findings,
    )


def distribution_analysis(ctx: AnalysisContext) -> AnalysisResult:
    """Generate axis value distributions."""
    fig, ax = plt.subplots(figsize=(10, 5))

    # X distribution (full)
    ax.hist(
        ctx.enriched["x_g"],
        bins=80,
        alpha=0.5,
        label=f"X (n={len(ctx.enriched)})",
        color="red",
        density=True,
    )

    # Y distribution - EXCLUDE saturated samples
    y_not_saturated = ctx.enriched[~ctx.enriched["is_y_saturated"]]["y_g"]
    if len(y_not_saturated) > 0:
        ax.hist(
            y_not_saturated,
            bins=80,
            alpha=0.5,
            label=f"Y non-sat (n={len(y_not_saturated)})",
            color="green",
            density=True,
        )

    # Z distribution (full)
    ax.hist(
        ctx.enriched["z_g"],
        bins=80,
        alpha=0.5,
        label=f"Z (n={len(ctx.enriched)})",
        color="blue",
        density=True,
    )

    ax.set_xlabel("Acceleration (g)")
    ax.set_ylabel("Density")
    ax.set_title("Axis Value Distributions (Y excludes saturated samples)")
    ax.legend()
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plot_path = ctx.output_dir / "plots" / "distributions.png"
    plt.savefig(plot_path, dpi=150)
    plt.close()

    return AnalysisResult(
        name="distributions",
        metrics={
            "total_samples": len(ctx.enriched),
            "y_not_saturated_samples": len(y_not_saturated),
        },
        plots=[plot_path],
        findings=[
            f"Total samples: {len(ctx.enriched):,}",
            f"Y non-saturated samples: {len(y_not_saturated):,}",
        ],
    )


def phase_analysis(ctx: AnalysisContext) -> AnalysisResult:
    """Analyze phase patterns and generate polar disc plots."""
    # Filter out glitch rotations and NaN
    enriched = ctx.enriched.dropna(subset=["period_us", "rpm"])
    enriched = enriched[enriched["rpm"] < 10000]  # Remove glitch rotations

    n_bins = 72  # 5° resolution
    enriched = enriched.copy()
    enriched["phase_bin"] = (enriched["phase"] * n_bins).astype(int) % n_bins

    plots = []
    findings = []
    metrics: dict = {"frequency_analysis": {}, "balancing": {}}

    # Define speed bands
    low_speed = enriched[enriched["rpm"] < 650]
    high_speed = enriched[enriched["rpm"] > 1200]

    angles = np.linspace(0, 2 * np.pi, n_bins, endpoint=False)

    # --- LOW SPEED POLAR PLOT ---
    if len(low_speed) > 100:
        low_speed = low_speed.copy()
        low_speed["phase_bin"] = (low_speed["phase"] * n_bins).astype(int) % n_bins

        # Compute deviation from mean for X and Z
        phase_data = []
        for axis in ["x", "z"]:
            col = f"{axis}_g"
            overall_mean = low_speed[col].mean()
            bin_means = low_speed.groupby("phase_bin")[col].mean()
            deviation = bin_means - overall_mean
            phase_data.append(deviation.values)

        combined_deviation = np.sqrt(phase_data[0] ** 2 + phase_data[1] ** 2)

        fig, ax = plt.subplots(figsize=(8, 8), subplot_kw={"projection": "polar"})
        norm_dev = (combined_deviation - combined_deviation.min()) / (
            combined_deviation.max() - combined_deviation.min() + 1e-10
        )
        colors = cm.Reds(norm_dev)

        ax.bar(
            angles,
            combined_deviation,
            width=2 * np.pi / n_bins,
            color=colors,
            alpha=0.8,
            edgecolor="darkred",
            linewidth=0.5,
        )
        ax.annotate(
            "HALL\n(0°)",
            xy=(0, combined_deviation.max() * 1.3),
            fontsize=10,
            ha="center",
            fontweight="bold",
        )
        ax.set_title(
            f"Low Speed Imbalance Map (<650 RPM)\nn={len(low_speed):,} samples",
            pad=20,
        )
        ax.set_theta_zero_location("N")
        ax.set_theta_direction(-1)

        plt.tight_layout()
        plot_path = ctx.output_dir / "plots" / "disc_low_speed.png"
        plt.savefig(plot_path, dpi=150)
        plt.close()
        plots.append(plot_path)

        findings.append(f"Low speed samples: {len(low_speed):,}")

    # --- HIGH SPEED POLAR PLOT ---
    if len(high_speed) > 100:
        high_speed = high_speed.copy()
        high_speed["phase_bin"] = (high_speed["phase"] * n_bins).astype(int) % n_bins

        phase_data_high = []
        for axis in ["x", "z"]:
            col = f"{axis}_g"
            overall_mean = high_speed[col].mean()
            bin_means = high_speed.groupby("phase_bin")[col].mean()
            deviation = bin_means - overall_mean
            phase_data_high.append(deviation.values)

        combined_deviation_high = np.sqrt(
            phase_data_high[0] ** 2 + phase_data_high[1] ** 2
        )

        fig, ax = plt.subplots(figsize=(8, 8), subplot_kw={"projection": "polar"})
        norm_dev_high = (combined_deviation_high - combined_deviation_high.min()) / (
            combined_deviation_high.max() - combined_deviation_high.min() + 1e-10
        )
        colors_high = cm.Reds(norm_dev_high)

        ax.bar(
            angles,
            combined_deviation_high,
            width=2 * np.pi / n_bins,
            color=colors_high,
            alpha=0.8,
            edgecolor="darkred",
            linewidth=0.5,
        )
        ax.annotate(
            "HALL\n(0°)",
            xy=(0, combined_deviation_high.max() * 1.3),
            fontsize=10,
            ha="center",
            fontweight="bold",
        )
        ax.set_title(
            f"High Speed Imbalance Map (>1200 RPM)\nn={len(high_speed):,} samples\n(note 3-arm pattern)",
            pad=20,
        )
        ax.set_theta_zero_location("N")
        ax.set_theta_direction(-1)

        plt.tight_layout()
        plot_path = ctx.output_dir / "plots" / "disc_high_speed.png"
        plt.savefig(plot_path, dpi=150)
        plt.close()
        plots.append(plot_path)

        findings.append(f"High speed samples: {len(high_speed):,}")

        # --- FFT ANALYSIS AND 1X FILTERED PLOT ---
        x_means = high_speed.groupby("phase_bin")["x_g"].mean().values
        z_means = high_speed.groupby("phase_bin")["z_g"].mean().values

        x_fft = np.fft.rfft(x_means)
        z_fft = np.fft.rfft(z_means)

        # Extract 1x components
        x_1x_mag = float(np.abs(x_fft[1]))
        x_1x_phase = float(np.degrees(np.angle(x_fft[1])))
        z_1x_mag = float(np.abs(z_fft[1]))
        z_1x_phase = float(np.degrees(np.angle(z_fft[1])))
        z_3x_mag = float(np.abs(z_fft[3]))

        metrics["frequency_analysis"] = {
            "x_1x": {"magnitude_g": round(x_1x_mag, 4), "phase_deg": round(x_1x_phase, 1)},
            "z_1x": {"magnitude_g": round(z_1x_mag, 4), "phase_deg": round(z_1x_phase, 1)},
            "z_3x_magnitude_g": round(z_3x_mag, 4),
        }

        findings.append(f"X-axis 1x: {x_1x_mag:.3f}g @ {x_1x_phase:+.0f}°")
        findings.append(f"Z-axis 1x: {z_1x_mag:.3f}g @ {z_1x_phase:+.0f}°")
        findings.append(f"Z-axis 3x (arm geometry): {z_3x_mag:.3f}g")

        # Filter to 1x only
        x_fft_1x = np.zeros_like(x_fft)
        z_fft_1x = np.zeros_like(z_fft)
        x_fft_1x[1] = x_fft[1]
        z_fft_1x[1] = z_fft[1]

        x_1x_signal = np.fft.irfft(x_fft_1x, n=n_bins)
        z_1x_signal = np.fft.irfft(z_fft_1x, n=n_bins)
        combined_1x = np.sqrt(x_1x_signal**2 + z_1x_signal**2)

        # Find peak (heavy spot)
        peak_idx = int(np.argmax(combined_1x))
        peak_angle = peak_idx * (360 / n_bins)
        counterweight_angle = (peak_angle + 180) % 360

        metrics["balancing"] = {
            "peak_imbalance_deg": round(peak_angle, 0),
            "counterweight_position_deg": round(counterweight_angle, 0),
        }

        findings.append(f"Peak imbalance at: {peak_angle:.0f}°")
        findings.append(f"Place counterweight at: {counterweight_angle:.0f}°")

        # Generate 1x filtered plot
        fig, ax = plt.subplots(figsize=(8, 8), subplot_kw={"projection": "polar"})
        norm_1x = (combined_1x - combined_1x.min()) / (
            combined_1x.max() - combined_1x.min() + 1e-10
        )
        colors_1x = cm.Reds(norm_1x)

        ax.bar(
            angles,
            combined_1x,
            width=2 * np.pi / n_bins,
            color=colors_1x,
            alpha=0.8,
            edgecolor="darkred",
            linewidth=0.5,
        )
        ax.annotate(
            "HALL\n(0°)",
            xy=(0, combined_1x.max() * 1.4),
            fontsize=10,
            ha="center",
            fontweight="bold",
        )
        ax.annotate(
            f"Heavy\nspot\n{peak_angle:.0f}°",
            xy=(np.radians(peak_angle), combined_1x[peak_idx]),
            fontsize=9,
            ha="center",
            color="darkred",
            fontweight="bold",
        )
        ax.annotate(
            f"Counter-\nweight\nhere\n{counterweight_angle:.0f}°",
            xy=(np.radians(counterweight_angle), combined_1x.max() * 0.5),
            fontsize=9,
            ha="center",
            color="blue",
            fontweight="bold",
        )
        ax.set_title(
            f"1x Component Only (imbalance signal)\nFiltered to remove 3-arm pattern\nPlace counterweight at {counterweight_angle:.0f}°",
            pad=20,
        )
        ax.set_theta_zero_location("N")
        ax.set_theta_direction(-1)

        plt.tight_layout()
        plot_path = ctx.output_dir / "plots" / "disc_1x_filtered.png"
        plt.savefig(plot_path, dpi=150)
        plt.close()
        plots.append(plot_path)

    return AnalysisResult(
        name="phase_analysis",
        metrics=metrics,
        plots=plots,
        findings=findings,
    )


# =============================================================================
# Analyzer Registry
# =============================================================================

ANALYZERS: list[Analyzer] = [
    data_quality_analysis,
    rpm_sweep_analysis,
    axis_timeseries_analysis,
    distribution_analysis,
    phase_analysis,
]


def run_all_analyses(ctx: AnalysisContext) -> list[AnalysisResult]:
    """Run all registered analyzers and collect results."""
    return [analyzer(ctx) for analyzer in ANALYZERS]


# =============================================================================
# Output Generation
# =============================================================================


def results_to_json(results: list[AnalysisResult], ctx: AnalysisContext) -> dict:
    """Convert analysis results to JSON-serializable dict."""
    output: dict = {
        "data_dir": str(ctx.output_dir),
    }

    # Merge metrics from each analyzer
    for result in results:
        if result.name == "data_quality":
            # Flatten data quality metrics into top-level
            output["data_quality"] = {
                "accel_samples": len(ctx.accel),
                "hall_events": len(ctx.hall),
                "rotations_covered": int(ctx.hall["rotation_num"].nunique()),
                **result.metrics.get("sample_timing", {}),
                **result.metrics.get("sequence_gaps", {}),
                **result.metrics.get("samples_per_rotation", {}),
                **result.metrics.get("phase_coverage", {}),
                **result.metrics.get("capture_stats", {}),
                **result.metrics.get("saturation", {}),
            }
            if "hall_timing" in result.metrics:
                output["data_quality"]["hall_period_cv_pct"] = result.metrics["hall_timing"].get("period_cv_pct")
        elif result.name == "rpm_sweep":
            output["rpm_range"] = {
                "min": result.metrics.get("rpm_min"),
                "max": result.metrics.get("rpm_max"),
            }
            output["data_quality"]["hall_glitches"] = result.metrics.get(
                "hall_glitches", 0
            )
        elif result.name == "phase_analysis":
            output["frequency_analysis"] = result.metrics.get("frequency_analysis", {})
            output["balancing"] = result.metrics.get("balancing", {})

    # Collect all plots
    all_plots = []
    for result in results:
        for plot in result.plots:
            all_plots.append(str(plot.relative_to(ctx.output_dir)))

    output["plots"] = all_plots
    output["report_path"] = str(ctx.output_dir / "report.html")

    return output


def generate_report(results: list[AnalysisResult], ctx: AnalysisContext) -> Path:
    """Generate HTML report from analysis results."""
    # Collect all findings
    all_findings = []
    for result in results:
        if result.findings:
            all_findings.append(f"<h3>{result.name.replace('_', ' ').title()}</h3>")
            all_findings.append("<ul>")
            for finding in result.findings:
                all_findings.append(f"  <li>{finding}</li>")
            all_findings.append("</ul>")

    findings_html = "\n".join(all_findings)

    # Collect plots by category
    plots_html_parts = []
    for result in results:
        if result.plots:
            plots_html_parts.append(
                f"<h3>{result.name.replace('_', ' ').title()}</h3>"
            )
            for plot in result.plots:
                rel_path = plot.relative_to(ctx.output_dir)
                plots_html_parts.append(
                    f'<img src="{rel_path}" style="max-width: 100%; margin: 10px 0;">'
                )

    plots_html = "\n".join(plots_html_parts)

    # Get JSON data for embedding
    json_data = results_to_json(results, ctx)

    html = f"""<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>Telemetry Analysis Report</title>
    <style>
        body {{
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
            background: #f5f5f5;
        }}
        h1 {{ color: #333; border-bottom: 2px solid #007acc; padding-bottom: 10px; }}
        h2 {{ color: #007acc; margin-top: 30px; }}
        h3 {{ color: #555; margin-top: 20px; }}
        .findings {{ background: white; padding: 20px; border-radius: 8px; margin: 20px 0; }}
        .plots {{ background: white; padding: 20px; border-radius: 8px; margin: 20px 0; }}
        .plots img {{ border: 1px solid #ddd; border-radius: 4px; }}
        ul {{ line-height: 1.8; }}
        .balancing-summary {{
            background: #e8f4f8;
            border-left: 4px solid #007acc;
            padding: 15px 20px;
            margin: 20px 0;
            font-size: 1.1em;
        }}
        .balancing-summary strong {{ color: #007acc; }}
        pre {{
            background: #2d2d2d;
            color: #f8f8f2;
            padding: 15px;
            border-radius: 8px;
            overflow-x: auto;
            font-size: 12px;
        }}
    </style>
</head>
<body>
    <h1>Telemetry Analysis Report</h1>
    <p>Data directory: <code>{ctx.output_dir}</code></p>

    <div class="balancing-summary">
        <strong>Balancing Recommendation:</strong><br>
        Peak imbalance at: <strong>{json_data.get('balancing', {}).get('peak_imbalance_deg', 'N/A')}°</strong><br>
        Place counterweight at: <strong>{json_data.get('balancing', {}).get('counterweight_position_deg', 'N/A')}°</strong>
    </div>

    <h2>Findings</h2>
    <div class="findings">
        {findings_html}
    </div>

    <h2>Plots</h2>
    <div class="plots">
        {plots_html}
    </div>

    <h2>Raw Data (JSON)</h2>
    <pre>{json.dumps(json_data, indent=2)}</pre>
</body>
</html>
"""

    report_path = ctx.output_dir / "report.html"
    report_path.write_text(html)
    return report_path
