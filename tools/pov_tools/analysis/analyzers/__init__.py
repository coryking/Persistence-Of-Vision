"""Analyzer registry and runner."""

from ..types import Analyzer, AnalysisContext, AnalysisResult
from .axis_timeseries import axis_timeseries_analysis
from .data_quality import data_quality_analysis
from .distribution import distribution_analysis
from .phase import phase_analysis
from .rpm_sweep import rpm_sweep_analysis

ANALYZERS: list[Analyzer] = [
    data_quality_analysis,
    rpm_sweep_analysis,
    axis_timeseries_analysis,
    distribution_analysis,
    phase_analysis,
]


def run_all_analyses(ctx: AnalysisContext) -> list[AnalysisResult]:
    """Run all registered analyzers and collect results."""
    return [analyzer(ctx) for analyzer in ANALYZERS]


__all__ = [
    "ANALYZERS",
    "run_all_analyses",
    "data_quality_analysis",
    "rpm_sweep_analysis",
    "axis_timeseries_analysis",
    "distribution_analysis",
    "phase_analysis",
]
