"""RPM sweep analyzer: RPM progression over time."""

import matplotlib.pyplot as plt

from ..types import AnalysisContext, AnalysisResult


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
