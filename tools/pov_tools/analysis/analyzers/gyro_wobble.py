"""Gyro wobble analyzer: wobble vs RPM, precession, and gyro-based phase."""

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy import optimize

from ..types import AnalysisContext, AnalysisResult


def gyro_wobble_analysis(ctx: AnalysisContext) -> AnalysisResult:
    """Analyze gyroscope data for wobble, precession, and phase patterns."""
    plots = []
    findings = []
    metrics: dict = {
        "wobble_vs_rpm": {},
        "precession": {},
        "gyro_phase": {},
    }

    enriched = ctx.enriched.dropna(subset=["rpm", "gyro_wobble_dps"])
    enriched = enriched[enriched["rpm"] < 10000]  # Remove glitch rotations

    # Check if we have gyro data
    if "gx_dps" not in enriched.columns or "gy_dps" not in enriched.columns:
        findings.append("No gyroscope data available for wobble analysis")
        return AnalysisResult(
            name="gyro_wobble", metrics=metrics, plots=plots, findings=findings
        )

    # A. Wobble vs RPM Analysis
    wobble_plot = _wobble_vs_rpm_analysis(ctx, enriched, metrics, findings)
    if wobble_plot:
        plots.append(wobble_plot)

    # B. Precession Analysis
    precession_plot = _precession_analysis(ctx, enriched, metrics, findings)
    if precession_plot:
        plots.append(precession_plot)

    # C. Gyro Phase Analysis (per speed preset)
    if ctx.speed_log is not None and "speed_preset" in enriched.columns:
        gyro_phase_plot = _gyro_phase_analysis(ctx, enriched, metrics, findings)
        if gyro_phase_plot:
            plots.append(gyro_phase_plot)

    return AnalysisResult(
        name="gyro_wobble", metrics=metrics, plots=plots, findings=findings
    )


def _wobble_vs_rpm_analysis(ctx, enriched, metrics, findings):
    """Plot wobble vs RPM and fit quadratic to confirm imbalance signature."""
    # Bin by RPM for cleaner plot
    rpm_bins = np.arange(200, enriched["rpm"].max() + 50, 50)
    enriched = enriched.copy()
    enriched["rpm_bin"] = pd.cut(enriched["rpm"], bins=rpm_bins)

    # Group by RPM bin and compute mean wobble
    grouped = enriched.groupby("rpm_bin", observed=True).agg(
        rpm_mean=("rpm", "mean"),
        wobble_mean=("gyro_wobble_dps", "mean"),
        wobble_std=("gyro_wobble_dps", "std"),
        count=("gyro_wobble_dps", "count"),
    )
    grouped = grouped[grouped["count"] > 10].dropna()

    if len(grouped) < 5:
        findings.append("Insufficient RPM range for wobble vs RPM analysis")
        return None

    rpm_vals = grouped["rpm_mean"].values
    wobble_vals = grouped["wobble_mean"].values
    wobble_std = grouped["wobble_std"].values

    # Fit quadratic: wobble = a*RPM² + b
    def quadratic(rpm, a, b):
        return a * rpm**2 + b

    try:
        popt, _ = optimize.curve_fit(quadratic, rpm_vals, wobble_vals, p0=[1e-5, 0])
        a, b = popt
        wobble_fit = quadratic(rpm_vals, a, b)

        # Compute R²
        ss_res = np.sum((wobble_vals - wobble_fit) ** 2)
        ss_tot = np.sum((wobble_vals - np.mean(wobble_vals)) ** 2)
        r_squared = 1 - (ss_res / ss_tot) if ss_tot > 0 else 0

        metrics["wobble_vs_rpm"]["quadratic_coefficient"] = float(a)
        metrics["wobble_vs_rpm"]["constant"] = float(b)
        metrics["wobble_vs_rpm"]["r_squared"] = round(float(r_squared), 3)

        if r_squared > 0.9:
            findings.append(
                f"Wobble scales with RPM² (R²={r_squared:.2f}) - classic mass imbalance signature"
            )
        elif r_squared > 0.7:
            findings.append(
                f"Wobble partially correlates with RPM² (R²={r_squared:.2f})"
            )
        else:
            findings.append(
                f"Wobble does not follow RPM² pattern (R²={r_squared:.2f}) - may indicate non-imbalance vibration"
            )

    except Exception:
        r_squared = None
        a, b = None, None

    # Plot
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.errorbar(
        rpm_vals,
        wobble_vals,
        yerr=wobble_std,
        fmt="o",
        capsize=3,
        label="Measured wobble",
        alpha=0.7,
    )

    if a is not None:
        rpm_smooth = np.linspace(rpm_vals.min(), rpm_vals.max(), 100)
        ax.plot(
            rpm_smooth,
            quadratic(rpm_smooth, a, b),
            "r-",
            label=f"Quadratic fit (R²={r_squared:.2f})",
            linewidth=2,
        )

    ax.set_xlabel("RPM")
    ax.set_ylabel("Gyro Wobble (°/s)")
    ax.set_title("Wobble vs RPM\n(Classic imbalance shows quadratic relationship)")
    ax.legend()
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plot_path = ctx.output_dir / "plots" / "wobble_vs_rpm.png"
    plt.savefig(plot_path, dpi=150)
    plt.close()

    return plot_path


def _precession_analysis(ctx, enriched, metrics, findings):
    """Analyze precession from GX/GY DC offsets per speed preset."""
    # If we have speed presets, analyze per preset
    if ctx.speed_log is not None and "speed_preset" in enriched.columns:
        positions = enriched["speed_preset"].dropna().unique()
        positions = sorted([int(p) for p in positions if not np.isnan(p)])

        if len(positions) < 2:
            findings.append("Insufficient speed presets for precession analysis")
            return None

        gx_means = []
        gy_means = []
        rpms = []
        pos_labels = []

        for pos in positions:
            pos_data = enriched[enriched["speed_preset"] == pos]
            # Exclude first 2 seconds of each position (transition)
            if "timestamp_us" in pos_data.columns and len(pos_data) > 100:
                t0 = pos_data["timestamp_us"].min()
                pos_data = pos_data[pos_data["timestamp_us"] > t0 + 2_000_000]

            if len(pos_data) > 50:
                gx_means.append(pos_data["gx_dps"].mean())
                gy_means.append(pos_data["gy_dps"].mean())
                rpms.append(pos_data["rpm"].mean())
                pos_labels.append(pos)

        if len(gx_means) < 2:
            return None

        gx_means = np.array(gx_means)
        gy_means = np.array(gy_means)
        rpms = np.array(rpms)

        # Compute precession direction for each position
        directions = np.degrees(np.arctan2(gy_means, gx_means))
        metrics["precession"]["directions_deg"] = [round(d, 1) for d in directions]
        metrics["precession"]["mean_direction_deg"] = round(float(np.mean(directions)), 1)

        # Circular standard deviation for consistency
        sin_sum = np.sum(np.sin(np.radians(directions)))
        cos_sum = np.sum(np.cos(np.radians(directions)))
        r_bar = np.sqrt(sin_sum**2 + cos_sum**2) / len(directions)
        circ_std = np.degrees(np.sqrt(-2 * np.log(r_bar))) if r_bar > 0 else 180

        metrics["precession"]["consistency_std_deg"] = round(float(circ_std), 1)

        if circ_std < 15:
            findings.append(
                f"Precession direction consistent across speeds (std={circ_std:.0f}°) - reliable imbalance indicator"
            )
        elif circ_std < 45:
            findings.append(
                f"Precession direction moderately consistent (std={circ_std:.0f}°)"
            )
        else:
            findings.append(
                f"Precession direction varies significantly (std={circ_std:.0f}°) - investigate per-speed breakdown"
            )

        # Plot GX vs GY scatter colored by RPM
        fig, ax = plt.subplots(figsize=(8, 8))
        scatter = ax.scatter(gx_means, gy_means, c=rpms, cmap="viridis", s=100)

        for i, pos in enumerate(pos_labels):
            ax.annotate(f"Pos {pos}", (gx_means[i], gy_means[i]), fontsize=8)

        ax.set_xlabel("Mean GX (°/s)")
        ax.set_ylabel("Mean GY (°/s)")
        ax.set_title(
            f"Precession Direction by Speed Preset\n(Direction consistency: std={circ_std:.0f}°)"
        )
        ax.axhline(y=0, color="gray", linestyle="--", alpha=0.5)
        ax.axvline(x=0, color="gray", linestyle="--", alpha=0.5)
        ax.set_aspect("equal")
        plt.colorbar(scatter, label="RPM")

        plt.tight_layout()
        plot_path = ctx.output_dir / "plots" / "precession_direction.png"
        plt.savefig(plot_path, dpi=150)
        plt.close()

        return plot_path

    return None


def _gyro_phase_analysis(ctx, enriched, metrics, findings):
    """Fit sinusoids to GX/GY vs angle at each speed preset."""
    if "angle_deg" not in enriched.columns:
        return None

    positions = enriched["speed_preset"].dropna().unique()
    positions = sorted([int(p) for p in positions if not np.isnan(p)])

    if len(positions) < 2:
        return None

    n_bins = 36  # 10 degree resolution
    phase_results = []

    for pos in positions:
        pos_data = enriched[enriched["speed_preset"] == pos].copy()

        # Exclude first 2 seconds
        if "timestamp_us" in pos_data.columns and len(pos_data) > 100:
            t0 = pos_data["timestamp_us"].min()
            pos_data = pos_data[pos_data["timestamp_us"] > t0 + 2_000_000]

        if len(pos_data) < 100:
            continue

        # Bin by angle
        pos_data["angle_bin"] = (pos_data["angle_deg"] / 10).astype(int) % n_bins

        for axis in ["gx_dps", "gy_dps"]:
            bin_means = pos_data.groupby("angle_bin")[axis].mean()
            if len(bin_means) < n_bins * 0.8:
                continue

            angles = bin_means.index.values * 10 * np.pi / 180
            values = bin_means.values

            # Fit sinusoid: y = A*sin(theta + phi) + offset
            def sinusoid(theta, A, phi, offset):
                return A * np.sin(theta + phi) + offset

            try:
                popt, _ = optimize.curve_fit(
                    sinusoid,
                    angles,
                    values,
                    p0=[np.std(values), 0, np.mean(values)],
                    maxfev=5000,
                )
                A, phi, offset = popt
                fit_values = sinusoid(angles, A, phi, offset)

                ss_res = np.sum((values - fit_values) ** 2)
                ss_tot = np.sum((values - np.mean(values)) ** 2)
                r_squared = 1 - (ss_res / ss_tot) if ss_tot > 0 else 0

                phase_deg = np.degrees(phi) % 360

                phase_results.append(
                    {
                        "position": pos,
                        "axis": axis,
                        "phase_deg": phase_deg,
                        "amplitude": abs(A),
                        "r_squared": r_squared,
                        "rpm": pos_data["rpm"].mean(),
                    }
                )

            except Exception:
                continue

    if not phase_results:
        return None

    metrics["gyro_phase"]["results"] = phase_results

    # Summarize findings
    good_fits = [r for r in phase_results if r["r_squared"] > 0.15]
    if good_fits:
        phases = [r["phase_deg"] for r in good_fits]
        weights = [r["r_squared"] for r in good_fits]

        # Weighted circular mean
        sin_sum = sum(w * np.sin(np.radians(p)) for w, p in zip(weights, phases))
        cos_sum = sum(w * np.cos(np.radians(p)) for w, p in zip(weights, phases))
        mean_phase = np.degrees(np.arctan2(sin_sum, cos_sum)) % 360

        metrics["gyro_phase"]["mean_phase_deg"] = round(mean_phase, 1)
        metrics["gyro_phase"]["n_good_fits"] = len(good_fits)

        findings.append(
            f"Gyro phase analysis: {len(good_fits)} good fits, mean phase={mean_phase:.0f}°"
        )

    # Plot phase vs RPM
    fig, ax = plt.subplots(figsize=(10, 6))

    for axis, marker, color in [("gx_dps", "o", "blue"), ("gy_dps", "s", "green")]:
        axis_results = [r for r in phase_results if r["axis"] == axis]
        if axis_results:
            rpms = [r["rpm"] for r in axis_results]
            phases = [r["phase_deg"] for r in axis_results]
            r2s = [r["r_squared"] for r in axis_results]
            sizes = [50 + 100 * r2 for r2 in r2s]  # Size by R²

            ax.scatter(
                rpms, phases, s=sizes, marker=marker, alpha=0.7, label=axis, c=color
            )

    ax.set_xlabel("RPM")
    ax.set_ylabel("Phase (degrees)")
    ax.set_title("Gyro Phase vs RPM\n(Size indicates fit quality R²)")
    ax.set_ylim(0, 360)
    ax.legend()
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plot_path = ctx.output_dir / "plots" / "gyro_phase_vs_rpm.png"
    plt.savefig(plot_path, dpi=150)
    plt.close()

    return plot_path
