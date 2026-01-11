"""Phase analyzer: phase patterns and polar disc plots."""

import matplotlib.cm as cm
import matplotlib.pyplot as plt
import numpy as np

from ..types import AnalysisContext, AnalysisResult


def phase_analysis(ctx: AnalysisContext) -> AnalysisResult:
    """Analyze phase patterns and generate polar disc plots."""
    # Filter out glitch rotations and NaN
    enriched = ctx.enriched.dropna(subset=["period_us", "rpm"])
    enriched = enriched[enriched["rpm"] < 10000]  # Remove glitch rotations

    n_bins = 72  # 5 degree resolution
    enriched = enriched.copy()
    enriched["phase_bin"] = (enriched["phase"] * n_bins).astype(int) % n_bins

    plots = []
    findings = []
    metrics: dict[str, object] = {"frequency_analysis": {}, "balancing": {}}

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
            "HALL\n(0deg)",
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
            "HALL\n(0deg)",
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

        findings.append(f"X-axis 1x: {x_1x_mag:.3f}g @ {x_1x_phase:+.0f} deg")
        findings.append(f"Z-axis 1x: {z_1x_mag:.3f}g @ {z_1x_phase:+.0f} deg")
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

        findings.append(f"Peak imbalance at: {peak_angle:.0f} deg")
        findings.append(f"Place counterweight at: {counterweight_angle:.0f} deg")

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
            "HALL\n(0deg)",
            xy=(0, combined_1x.max() * 1.4),
            fontsize=10,
            ha="center",
            fontweight="bold",
        )
        ax.annotate(
            f"Heavy\nspot\n{peak_angle:.0f}deg",
            xy=(np.radians(peak_angle), combined_1x[peak_idx]),
            fontsize=9,
            ha="center",
            color="darkred",
            fontweight="bold",
        )
        ax.annotate(
            f"Counter-\nweight\nhere\n{counterweight_angle:.0f}deg",
            xy=(np.radians(counterweight_angle), combined_1x.max() * 0.5),
            fontsize=9,
            ha="center",
            color="blue",
            fontweight="bold",
        )
        ax.set_title(
            f"1x Component Only (imbalance signal)\nFiltered to remove 3-arm pattern\nPlace counterweight at {counterweight_angle:.0f} deg",
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
