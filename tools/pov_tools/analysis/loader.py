"""Data loading and enrichment for telemetry analysis."""

from pathlib import Path

import pandas as pd

from .types import AnalysisContext

# =============================================================================
# MPU-9250 Conversion Constants
#
# The MPU-9250 outputs 16-bit signed integers (-32768 to +32767).
# Conversion to physical units depends on the configured full-scale range.
#
# Formula: physical_value = raw_value * (full_scale_range / 32768)
#
# Our configuration (see led_display/src/Imu.cpp):
#   - Accelerometer: ±16g range
#   - Gyroscope: ±2000°/s range
# =============================================================================

# Accelerometer: ±16g range means 32768 counts = 16g
# LSB sensitivity: 2048 counts/g (from datasheet)
ACCEL_RANGE_G = 16.0
ACCEL_LSB_PER_G = 32768.0 / ACCEL_RANGE_G  # 2048
ACCEL_G_PER_LSB = ACCEL_RANGE_G / 32768.0  # 0.00048828125

# Gyroscope: ±2000°/s range means 32768 counts = 2000°/s
# LSB sensitivity: 16.4 counts/(°/s) (from datasheet)
GYRO_RANGE_DPS = 2000.0
GYRO_LSB_PER_DPS = 32768.0 / GYRO_RANGE_DPS  # 16.384
GYRO_DPS_PER_LSB = GYRO_RANGE_DPS / 32768.0  # 0.06103515625

# Saturation threshold: 16-bit signed max is ±32767
# Using 32700 to catch values near saturation (within ~0.2% of limit)
SATURATION_THRESHOLD = 32700


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

    # Convert accelerometer raw counts to g
    for axis in ["x", "y", "z"]:
        enriched[f"{axis}_g"] = enriched[axis] * ACCEL_G_PER_LSB

    # Convert gyroscope raw counts to degrees per second
    for axis in ["gx", "gy", "gz"]:
        if axis in enriched.columns:
            enriched[f"{axis}_dps"] = enriched[axis] * GYRO_DPS_PER_LSB

    # Compute RPM from hall period (microseconds per revolution)
    enriched["rpm"] = 60_000_000 / enriched["period_us"]

    # Compute phase within rotation (0.0 to 1.0)
    enriched["phase"] = (enriched["micros_since_hall"] / enriched["period_us"]) % 1.0
    enriched["phase_deg"] = enriched["phase"] * 360

    # Mark saturated samples (near 16-bit signed limits)
    for axis in ["x", "y", "z"]:
        enriched[f"is_{axis}_saturated"] = enriched[axis].abs() >= SATURATION_THRESHOLD

    # Create plots directory
    plots_dir = data_dir / "plots"
    plots_dir.mkdir(exist_ok=True)

    return AnalysisContext(
        accel=accel, hall=hall, enriched=enriched, output_dir=data_dir
    )
