"""Phase analyzer: phase patterns and polar disc plots."""

import matplotlib.cm as cm
import matplotlib.pyplot as plt
import numpy as np
from scipy import optimize

from ..types import AnalysisContext, AnalysisResult


def phase_analysis(ctx: AnalysisContext) -> AnalysisResult:
    """Analyze phase patterns and generate polar disc plots.

    Uses speed positions from speed_log if available, otherwise falls back
    to hardcoded RPM thresholds for backward compatibility.
    """
    if ctx.speed_log is not None and "speed_position" in ctx.enriched.columns:
        return _phase_analysis_by_position(ctx)
    else:
        return _phase_analysis_fallback(ctx)


def _phase_analysis_by_position(ctx: AnalysisContext) -> AnalysisResult:
    """Phase analysis using speed positions from speed_log."""
    enriched = ctx.enriched.dropna(subset=["rpm", "speed_position"])
    enriched = enriched[enriched["rpm"] < 10000]  # Remove glitch rotations

    plots = []
    findings = []
    metrics: dict = {
        "frequency_analysis": {},
        "balancing": {},
        "phase_by_position": {},
        "phase_summary": {},
    }

    n_bins = 72  # 5 degree resolution
    angles = np.linspace(0, 2 * np.pi, n_bins, endpoint=False)

    positions = enriched["speed_position"].dropna().unique()
    positions = sorted([int(p) for p in positions if not np.isnan(p)])

    if len(positions) == 0:
        findings.append("No valid speed positions found")
        return AnalysisResult(
            name="phase_analysis", metrics=metrics, plots=plots, findings=findings
        )

    findings.append(f"Analyzing {len(positions)} speed positions")

    # Collect phase estimates from all positions and axes
    all_phase_estimates = []

    # Identify low and high speed positions
    pos_rpm_means = {}
    for pos in positions:
        pos_data = enriched[enriched["speed_position"] == pos]
        pos_rpm_means[pos] = pos_data["rpm"].mean()

    # Sort positions by RPM
    sorted_positions = sorted(pos_rpm_means.keys(), key=lambda p: pos_rpm_means[p])
    low_positions = sorted_positions[: max(1, len(sorted_positions) // 3)]
    high_positions = sorted_positions[-max(1, len(sorted_positions) // 3) :]

    # --- ANALYSIS PER POSITION ---
    for pos in positions:
        pos_data = enriched[enriched["speed_position"] == pos].copy()

        # Exclude first 2 seconds for steady-state
        if "timestamp_us" in pos_data.columns and len(pos_data) > 100:
            t0 = pos_data["timestamp_us"].min()
            pos_data = pos_data[pos_data["timestamp_us"] > t0 + 2_000_000]

        if len(pos_data) < 100:
            continue

        mean_rpm = pos_data["rpm"].mean()
        pos_data["phase_bin"] = (pos_data["phase"] * n_bins).astype(int) % n_bins

        position_results = {
            "position": pos,
            "mean_rpm": round(mean_rpm, 1),
            "n_samples": len(pos_data),
            "axes": {},
        }

        # Analyze each axis with sinusoid fitting
        for axis in ["x_g", "z_g"]:
            if axis not in pos_data.columns:
                continue

            bin_means = pos_data.groupby("phase_bin")[axis].mean()
            if len(bin_means) < n_bins * 0.8:
                continue

            # Ensure we have all bins (fill missing with mean)
            full_bins = np.zeros(n_bins)
            for idx, val in bin_means.items():
                full_bins[idx] = val

            # FFT for 1x extraction
            fft = np.fft.rfft(full_bins)
            mag_1x = float(np.abs(fft[1]))
            phase_1x = float(np.degrees(np.angle(fft[1])))

            # Sinusoid fitting for R² confidence
            r_squared = _compute_sinusoid_r2(angles, full_bins)

            position_results["axes"][axis] = {
                "magnitude_g": round(mag_1x, 4),
                "phase_deg": round(phase_1x, 1),
                "r_squared": round(r_squared, 3),
            }

            if r_squared > 0.15:
                all_phase_estimates.append(
                    {
                        "position": pos,
                        "axis": axis,
                        "phase_deg": phase_1x,
                        "r_squared": r_squared,
                        "rpm": mean_rpm,
                    }
                )

        metrics["phase_by_position"][f"position_{pos}"] = position_results

    # --- LOW SPEED POLAR PLOT ---
    low_data = enriched[enriched["speed_position"].isin(low_positions)].copy()
    if len(low_data) > 100:
        # Exclude transitions
        if "timestamp_us" in low_data.columns:
            for pos in low_positions:
                pos_mask = low_data["speed_position"] == pos
                if pos_mask.sum() > 100:
                    t0 = low_data.loc[pos_mask, "timestamp_us"].min()
                    low_data = low_data[
                        ~pos_mask | (low_data["timestamp_us"] > t0 + 2_000_000)
                    ]

        low_plot = _generate_polar_plot(
            ctx,
            low_data,
            angles,
            n_bins,
            f"Low Speed Imbalance Map (Positions {low_positions})",
            "disc_low_speed.png",
        )
        if low_plot:
            plots.append(low_plot)
            findings.append(f"Low speed: {len(low_data):,} samples from positions {low_positions}")

    # --- HIGH SPEED POLAR PLOT AND FFT ---
    high_data = enriched[enriched["speed_position"].isin(high_positions)].copy()
    if len(high_data) > 100:
        # Exclude transitions
        if "timestamp_us" in high_data.columns:
            for pos in high_positions:
                pos_mask = high_data["speed_position"] == pos
                if pos_mask.sum() > 100:
                    t0 = high_data.loc[pos_mask, "timestamp_us"].min()
                    high_data = high_data[
                        ~pos_mask | (high_data["timestamp_us"] > t0 + 2_000_000)
                    ]

        high_plot = _generate_polar_plot(
            ctx,
            high_data,
            angles,
            n_bins,
            f"High Speed Imbalance Map (Positions {high_positions})",
            "disc_high_speed.png",
        )
        if high_plot:
            plots.append(high_plot)
            findings.append(f"High speed: {len(high_data):,} samples from positions {high_positions}")

        # FFT analysis on high speed data
        high_data["phase_bin"] = (high_data["phase"] * n_bins).astype(int) % n_bins
        x_means = high_data.groupby("phase_bin")["x_g"].mean()
        z_means = high_data.groupby("phase_bin")["z_g"].mean()

        if len(x_means) >= n_bins * 0.8 and len(z_means) >= n_bins * 0.8:
            x_full = np.zeros(n_bins)
            z_full = np.zeros(n_bins)
            for idx, val in x_means.items():
                x_full[idx] = val
            for idx, val in z_means.items():
                z_full[idx] = val

            x_fft = np.fft.rfft(x_full)
            z_fft = np.fft.rfft(z_full)

            x_1x_mag = float(np.abs(x_fft[1]))
            x_1x_phase = float(np.degrees(np.angle(x_fft[1])))
            z_1x_mag = float(np.abs(z_fft[1]))
            z_1x_phase = float(np.degrees(np.angle(z_fft[1])))
            z_3x_mag = float(np.abs(z_fft[3])) if len(z_fft) > 3 else 0.0

            metrics["frequency_analysis"] = {
                "x_1x": {
                    "magnitude_g": round(x_1x_mag, 4),
                    "phase_deg": round(x_1x_phase, 1),
                },
                "z_1x": {
                    "magnitude_g": round(z_1x_mag, 4),
                    "phase_deg": round(z_1x_phase, 1),
                },
                "z_3x_magnitude_g": round(z_3x_mag, 4),
            }

            findings.append(f"X-axis 1x: {x_1x_mag:.3f}g @ {x_1x_phase:+.0f}°")
            findings.append(f"Z-axis 1x: {z_1x_mag:.3f}g @ {z_1x_phase:+.0f}°")
            findings.append(f"Z-axis 3x (arm geometry): {z_3x_mag:.3f}g")

            # 1x filtered plot
            filtered_plot = _generate_1x_filtered_plot(
                ctx, x_fft, z_fft, angles, n_bins, metrics
            )
            if filtered_plot:
                plots.append(filtered_plot)

    # --- PHASE SUMMARY ---
    if all_phase_estimates:
        summary = _compute_phase_summary(all_phase_estimates, findings)
        metrics["phase_summary"] = summary

    return AnalysisResult(
        name="phase_analysis", metrics=metrics, plots=plots, findings=findings
    )


def _phase_analysis_fallback(ctx: AnalysisContext) -> AnalysisResult:
    """Original phase analysis using hardcoded RPM thresholds."""
    # Filter out glitch rotations and NaN
    enriched = ctx.enriched.dropna(subset=["period_us", "rpm"])
    enriched = enriched[enriched["rpm"] < 10000]  # Remove glitch rotations

    n_bins = 72  # 5 degree resolution
    enriched = enriched.copy()
    enriched["phase_bin"] = (enriched["phase"] * n_bins).astype(int) % n_bins

    plots = []
    findings = []
    metrics: dict = {"frequency_analysis": {}, "balancing": {}}

    # Define speed bands (fallback thresholds)
    low_speed = enriched[enriched["rpm"] < 650]
    high_speed = enriched[enriched["rpm"] > 1200]

    angles = np.linspace(0, 2 * np.pi, n_bins, endpoint=False)

    findings.append("Using fallback RPM thresholds (no speed_log available)")

    # --- LOW SPEED POLAR PLOT ---
    if len(low_speed) > 100:
        low_plot = _generate_polar_plot(
            ctx, low_speed, angles, n_bins, "Low Speed Imbalance Map (<650 RPM)", "disc_low_speed.png"
        )
        if low_plot:
            plots.append(low_plot)
            findings.append(f"Low speed samples: {len(low_speed):,}")

    # --- HIGH SPEED POLAR PLOT ---
    if len(high_speed) > 100:
        high_plot = _generate_polar_plot(
            ctx,
            high_speed,
            angles,
            n_bins,
            "High Speed Imbalance Map (>1200 RPM)",
            "disc_high_speed.png",
        )
        if high_plot:
            plots.append(high_plot)
            findings.append(f"High speed samples: {len(high_speed):,}")

        # FFT analysis
        high_speed = high_speed.copy()
        high_speed["phase_bin"] = (high_speed["phase"] * n_bins).astype(int) % n_bins
        x_means = high_speed.groupby("phase_bin")["x_g"].mean().values
        z_means = high_speed.groupby("phase_bin")["z_g"].mean().values

        if len(x_means) == n_bins and len(z_means) == n_bins:
            x_fft = np.fft.rfft(x_means)
            z_fft = np.fft.rfft(z_means)

            x_1x_mag = float(np.abs(x_fft[1]))
            x_1x_phase = float(np.degrees(np.angle(x_fft[1])))
            z_1x_mag = float(np.abs(z_fft[1]))
            z_1x_phase = float(np.degrees(np.angle(z_fft[1])))
            z_3x_mag = float(np.abs(z_fft[3]))

            metrics["frequency_analysis"] = {
                "x_1x": {
                    "magnitude_g": round(x_1x_mag, 4),
                    "phase_deg": round(x_1x_phase, 1),
                },
                "z_1x": {
                    "magnitude_g": round(z_1x_mag, 4),
                    "phase_deg": round(z_1x_phase, 1),
                },
                "z_3x_magnitude_g": round(z_3x_mag, 4),
            }

            findings.append(f"X-axis 1x: {x_1x_mag:.3f}g @ {x_1x_phase:+.0f}°")
            findings.append(f"Z-axis 1x: {z_1x_mag:.3f}g @ {z_1x_phase:+.0f}°")
            findings.append(f"Z-axis 3x (arm geometry): {z_3x_mag:.3f}g")

            # 1x filtered plot
            filtered_plot = _generate_1x_filtered_plot(
                ctx, x_fft, z_fft, angles, n_bins, metrics
            )
            if filtered_plot:
                plots.append(filtered_plot)

    return AnalysisResult(
        name="phase_analysis", metrics=metrics, plots=plots, findings=findings
    )


def _generate_polar_plot(ctx, data, angles, n_bins, title, filename):
    """Generate a polar imbalance map plot."""
    data = data.copy()
    data["phase_bin"] = (data["phase"] * n_bins).astype(int) % n_bins

    # Compute deviation from mean for X and Z
    phase_data = []
    for axis in ["x", "z"]:
        col = f"{axis}_g"
        if col not in data.columns:
            return None
        overall_mean = data[col].mean()
        bin_means = data.groupby("phase_bin")[col].mean()

        # Fill missing bins
        full_bins = np.zeros(n_bins)
        for idx, val in bin_means.items():
            full_bins[idx] = val - overall_mean
        phase_data.append(full_bins)

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
    ax.set_title(f"{title}\nn={len(data):,} samples", pad=20)
    ax.set_theta_zero_location("N")
    ax.set_theta_direction(-1)

    plt.tight_layout()
    plot_path = ctx.output_dir / "plots" / filename
    plt.savefig(plot_path, dpi=150)
    plt.close()

    return plot_path


def _generate_1x_filtered_plot(ctx, x_fft, z_fft, angles, n_bins, metrics):
    """Generate the 1x filtered polar plot with balancing recommendation."""
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
        f"1x Component Only (imbalance signal)\n"
        f"Filtered to remove 3-arm pattern\n"
        f"Place counterweight at {counterweight_angle:.0f}°",
        pad=20,
    )
    ax.set_theta_zero_location("N")
    ax.set_theta_direction(-1)

    plt.tight_layout()
    plot_path = ctx.output_dir / "plots" / "disc_1x_filtered.png"
    plt.savefig(plot_path, dpi=150)
    plt.close()

    return plot_path


def _compute_sinusoid_r2(angles, values):
    """Compute R² for sinusoid fit to phase-binned data."""

    def sinusoid(theta, A, phi, offset):
        return A * np.sin(theta + phi) + offset

    try:
        popt, _ = optimize.curve_fit(
            sinusoid, angles, values, p0=[np.std(values), 0, np.mean(values)], maxfev=5000
        )
        fit_values = sinusoid(angles, *popt)
        ss_res = np.sum((values - fit_values) ** 2)
        ss_tot = np.sum((values - np.mean(values)) ** 2)
        return 1 - (ss_res / ss_tot) if ss_tot > 0 else 0
    except Exception:
        return 0.0


def _compute_phase_summary(all_estimates, findings):
    """Compute weighted circular mean of all phase estimates."""
    if not all_estimates:
        return {}

    # Filter to estimates with R² > 0.15
    good_estimates = [e for e in all_estimates if e["r_squared"] > 0.15]

    if not good_estimates:
        findings.append("No phase estimates with R² > 0.15")
        return {"n_estimates": 0}

    # Weighted circular mean
    phases = [e["phase_deg"] for e in good_estimates]
    weights = [e["r_squared"] for e in good_estimates]

    sin_sum = sum(w * np.sin(np.radians(p)) for w, p in zip(weights, phases))
    cos_sum = sum(w * np.cos(np.radians(p)) for w, p in zip(weights, phases))
    mean_phase = np.degrees(np.arctan2(sin_sum, cos_sum)) % 360

    # Circular standard deviation
    total_weight = sum(weights)
    r_bar = np.sqrt(sin_sum**2 + cos_sum**2) / total_weight if total_weight > 0 else 0
    circ_std = np.degrees(np.sqrt(-2 * np.log(r_bar))) if r_bar > 0 else 180

    counterweight = (mean_phase + 180) % 360

    findings.append(
        f"Phase summary: {len(good_estimates)} estimates, mean={mean_phase:.0f}° (std={circ_std:.0f}°)"
    )
    findings.append(f"Recommended counterweight position: {counterweight:.0f}°")

    return {
        "n_estimates": len(good_estimates),
        "mean_phase_deg": round(mean_phase, 1),
        "circular_std_deg": round(circ_std, 1),
        "counterweight_deg": round(counterweight, 1),
        "estimates": good_estimates,
    }
