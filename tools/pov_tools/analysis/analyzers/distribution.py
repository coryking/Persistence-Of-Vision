"""Distribution analyzer: axis value distributions."""

import matplotlib.pyplot as plt

from ..types import AnalysisContext, AnalysisResult


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
