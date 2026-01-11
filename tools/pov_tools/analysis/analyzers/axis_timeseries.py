"""Axis timeseries analyzer: time series plots for each axis."""

import matplotlib.pyplot as plt

from ..types import AnalysisContext, AnalysisResult


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

    # Check saturation on all axes
    saturation_pcts = {}
    for axis in ["x", "y", "z"]:
        saturation_pcts[axis] = ctx.enriched[f"is_{axis}_saturated"].mean() * 100

    findings = [
        f"X-axis range: {axis_stats['x']['min']:.2f}g to {axis_stats['x']['max']:.2f}g",
        f"Y-axis range: {axis_stats['y']['min']:.2f}g to {axis_stats['y']['max']:.2f}g",
        f"Z-axis range: {axis_stats['z']['min']:.2f}g to {axis_stats['z']['max']:.2f}g",
    ]
    for axis in ["x", "y", "z"]:
        if saturation_pcts[axis] > 1:
            findings.append(
                f"{axis.upper()}-axis saturated: {saturation_pcts[axis]:.1f}% of samples at ±16g"
            )

    return AnalysisResult(
        name="axis_timeseries",
        metrics={
            "axis_stats": axis_stats,
            "x_saturation_pct": round(saturation_pcts["x"], 1),
            "y_saturation_pct": round(saturation_pcts["y"], 1),
            "z_saturation_pct": round(saturation_pcts["z"], 1),
            "samples_plotted": len(early),
        },
        plots=plots,
        findings=findings,
    )
