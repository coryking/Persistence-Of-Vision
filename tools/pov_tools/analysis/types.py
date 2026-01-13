"""Data structures for telemetry analysis."""

from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

import pandas as pd


@dataclass
class AnalysisContext:
    """Shared context passed to all analyzers.

    Contains raw data, enriched data with computed columns, and output config.
    """

    accel: pd.DataFrame  # Raw accel (timestamp_us, sequence_num, x, y, z)
    hall: pd.DataFrame  # Raw hall (timestamp_us, period_us, rotation_num)
    enriched: pd.DataFrame  # Accel merged with hall + computed columns
    output_dir: Path  # Where to write plots
    speed_log: pd.DataFrame | None = None  # Speed log with position boundaries

    # Pre-computed in enriched DataFrame:
    # - rpm: from period_us
    # - phase: 0-1 within rotation
    # - phase_deg: 0-360
    # - x_g, y_g, z_g: converted to g units (3.9mg/LSB)
    # - is_y_saturated: boolean (raw y >= 4094)
    # - speed_position: integer speed position (1-N) from speed_log


@dataclass
class AnalysisResult:
    """Output from one analysis module."""

    name: str  # e.g., "rpm_sweep"
    metrics: dict  # Structured data for JSON output
    plots: list[Path] = field(default_factory=list)  # Generated plot files
    findings: list[str] = field(default_factory=list)  # Human-readable bullet points


# Type alias for analyzer functions
Analyzer = Callable[[AnalysisContext], AnalysisResult]
