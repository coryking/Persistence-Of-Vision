"""Data loading and enrichment for telemetry analysis."""

from pathlib import Path

import pandas as pd

from .types import AnalysisContext

# =============================================================================
# MPU-9250 Conversion Constants (for reference/ad-hoc analysis)
#
# NOTE: CSV enrichment with physical units is done during dump by
# pov_tools/serial_comm.py:enrich_accel_csv(). These constants are kept here
# for any ad-hoc analysis that needs them.
#
# Authoritative source for range config: led_display/src/Imu.cpp lines 100-109
# Authoritative source for enrichment: pov_tools/serial_comm.py
# =============================================================================

ACCEL_RANGE_G = 16.0
ACCEL_G_PER_LSB = ACCEL_RANGE_G / 32768.0  # 0.00048828125

GYRO_RANGE_DPS = 2000.0
GYRO_DPS_PER_LSB = GYRO_RANGE_DPS / 32768.0  # 0.06103515625


def load_and_enrich(data_dir: Path) -> AnalysisContext:
    """Load pre-enriched CSVs for analysis.

    The CSV is expected to already contain physical units and derived values
    from the dump enrichment step (see serial_comm.py:enrich_accel_csv).

    Expected columns in MSG_ACCEL_SAMPLES.csv:
        timestamp_us, sequence_num, rotation_num, micros_since_hall,
        angle_deg, rpm, x_g, y_g, z_g, gx_dps, gy_dps, gz_dps,
        gyro_wobble_dps, is_y_saturated, is_gz_saturated

    Args:
        data_dir: Directory containing MSG_ACCEL_SAMPLES.csv and MSG_HALL_EVENT.csv

    Returns:
        AnalysisContext with loaded data
    """
    accel_path = data_dir / "MSG_ACCEL_SAMPLES.csv"
    hall_path = data_dir / "MSG_HALL_EVENT.csv"

    if not accel_path.exists():
        raise FileNotFoundError(f"Accelerometer data not found: {accel_path}")
    if not hall_path.exists():
        raise FileNotFoundError(f"Hall event data not found: {hall_path}")

    accel = pd.read_csv(accel_path)
    hall = pd.read_csv(hall_path)

    # Merge hall period for any additional analysis
    enriched = accel.merge(
        hall[["rotation_num", "period_us"]], on="rotation_num", how="left"
    )

    # Compute phase (0.0 to 1.0) from angle_deg for compatibility
    if "angle_deg" in enriched.columns:
        enriched["phase"] = enriched["angle_deg"] / 360.0

    # Create plots directory
    plots_dir = data_dir / "plots"
    plots_dir.mkdir(exist_ok=True)

    return AnalysisContext(
        accel=accel, hall=hall, enriched=enriched, output_dir=data_dir
    )
