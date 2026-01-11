"""Telemetry analysis module with pluggable analyzers.

This module provides tools for analyzing telemetry data from the POV display,
including data quality checks, RPM analysis, and rotor balancing recommendations.

Usage:
    from pov_tools.analysis import load_and_enrich, run_all_analyses, generate_report

    ctx = load_and_enrich(Path("data_dir"))
    results = run_all_analyses(ctx)
    report_path = generate_report(results, ctx)
"""

from .analyzers import ANALYZERS, run_all_analyses
from .loader import load_and_enrich
from .output import generate_report, results_to_json
from .types import AnalysisContext, AnalysisResult, Analyzer

__all__ = [
    # Types
    "AnalysisContext",
    "AnalysisResult",
    "Analyzer",
    # Loading
    "load_and_enrich",
    # Running
    "ANALYZERS",
    "run_all_analyses",
    # Output
    "results_to_json",
    "generate_report",
]
