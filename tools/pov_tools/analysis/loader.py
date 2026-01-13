"""Data loading and enrichment for telemetry analysis."""

from pathlib import Path

import numpy as np
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

    # Load speed_log if present and assign speed positions
    speed_log_path = data_dir / "speed_log.csv"
    speed_log = None
    if speed_log_path.exists():
        speed_log = pd.read_csv(speed_log_path)
        enriched = _assign_speed_positions(enriched, speed_log)

    # Create plots directory
    plots_dir = data_dir / "plots"
    plots_dir.mkdir(exist_ok=True)

    return AnalysisContext(
        accel=accel,
        hall=hall,
        enriched=enriched,
        output_dir=data_dir,
        speed_log=speed_log,
    )


def _assign_speed_positions(
    enriched: pd.DataFrame, speed_log: pd.DataFrame
) -> pd.DataFrame:
    """Assign speed_position to each sample based on hall_packets boundaries.

    The speed_log contains cumulative hall_packets counts per position.
    We use these to partition rotation_num ranges:
        Position 1: rotations [0, hall_packets[0])
        Position 2: rotations [hall_packets[0], sum(hall_packets[0:2]))
        etc.

    Args:
        enriched: DataFrame with rotation_num column
        speed_log: DataFrame with position and hall_packets columns

    Returns:
        enriched DataFrame with speed_position column added
    """
    if "hall_packets" not in speed_log.columns:
        # Old format without hall_packets - can't assign positions
        enriched["speed_position"] = np.nan
        return enriched

    # Build cumulative rotation boundaries
    hall_packets = speed_log["hall_packets"].values
    positions = speed_log["position"].values

    # Cumulative sum gives upper bounds for each position
    cumsum = np.cumsum(hall_packets)
    # Lower bounds are 0, then previous cumsum values
    lower_bounds = np.concatenate([[0], cumsum[:-1]])

    # Create position lookup using searchsorted
    # For each rotation_num, find which position range it falls into
    rotation_nums = enriched["rotation_num"].values

    # searchsorted on upper bounds (cumsum) tells us which position
    # rotation_num < cumsum[0] -> position 0 (index 0)
    # cumsum[0] <= rotation_num < cumsum[1] -> position 1 (index 1)
    # etc.
    position_indices = np.searchsorted(cumsum, rotation_nums, side="right")

    # Map indices to actual position values (1-indexed in speed_log)
    # Handle out-of-range rotations (before first or after last position)
    speed_position = np.where(
        position_indices < len(positions),
        positions[position_indices],
        np.nan,  # Rotations beyond the last position
    )

    enriched = enriched.copy()
    enriched["speed_position"] = speed_position

    return enriched
