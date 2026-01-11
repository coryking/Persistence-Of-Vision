"""Data loading and enrichment for telemetry analysis."""

from pathlib import Path

import pandas as pd

from .types import AnalysisContext


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
