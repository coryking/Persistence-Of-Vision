"""Data quality analyzer: sample rates, gaps, timing consistency."""

from ..types import AnalysisContext, AnalysisResult


def data_quality_analysis(ctx: AnalysisContext) -> AnalysisResult:
    """Analyze data quality: sample rates, gaps, timing consistency."""
    accel = ctx.accel
    hall = ctx.hall

    metrics: dict[str, object] = {}
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
    findings.append(f"Sample interval: {sample_intervals.mean():.0f} +/- {interval_std_us:.0f} us")
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
        f"Samples per rotation: {samples_per_rot.mean():.0f} +/- {samples_per_rot.std():.0f} "
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

    # === AXIS SATURATION (all axes, both directions) ===
    # Check all axes since accelerometer orientation is arbitrary
    saturation_data = {}
    for axis in ["x", "y", "z"]:
        saturated = (accel[axis].abs() >= 4094).sum()
        sat_pct = 100 * saturated / len(accel)
        saturation_data[axis] = {
            "samples": int(saturated),
            "pct": round(sat_pct, 1),
        }
        if sat_pct > 5:
            findings.append(f"{axis.upper()}-axis saturation: {sat_pct:.1f}% of samples clipped at +/-16g")

    metrics["saturation"] = saturation_data

    return AnalysisResult(
        name="data_quality",
        metrics=metrics,
        plots=[],  # No plots for data quality
        findings=findings,
    )
