"""Analyze command - run analysis on captured telemetry data."""

import json
from pathlib import Path
from typing import Optional

import typer

from .utils import interactive_capture_picker, err_console, TELEMETRY_DIR


def analyze(
    data_dir: Optional[Path] = typer.Argument(
        None, help="Capture directory to analyze (interactive picker if not specified)"
    ),
):
    """Analyze telemetry data and generate report.

    Outputs JSON to stdout. HTML report and plots written to data_dir.
    """
    from ..analysis import (
        load_and_enrich,
        run_all_analyses,
        results_to_json,
        generate_report,
    )

    # Determine which directory to analyze
    if data_dir is None:
        # Check for captures in default location
        data_dir = interactive_capture_picker(TELEMETRY_DIR)
        if data_dir is None:
            raise typer.Exit(1)
    elif not data_dir.exists():
        err_console.print(f"[red]Directory not found:[/red] {data_dir}")
        raise typer.Exit(1)

    # Check for required files
    accel_path = data_dir / "MSG_ACCEL_SAMPLES.csv"
    hall_path = data_dir / "MSG_HALL_EVENT.csv"

    if not accel_path.exists() or not hall_path.exists():
        # Maybe they passed the base telemetry dir with flat files?
        # Check if files exist directly
        if not accel_path.exists():
            err_console.print(f"[red]Missing:[/red] {accel_path}")
        if not hall_path.exists():
            err_console.print(f"[red]Missing:[/red] {hall_path}")
        raise typer.Exit(1)

    try:
        # Load and enrich data
        ctx = load_and_enrich(data_dir)

        # Run all analyzers
        results = run_all_analyses(ctx)

        # Generate HTML report
        report_path = generate_report(results, ctx)

        # Output JSON to stdout
        output = results_to_json(results, ctx)
        print(json.dumps(output, indent=2))

    except FileNotFoundError as e:
        err_console.print(f"[red]Error:[/red] {e}")
        raise typer.Exit(1)
    except Exception as e:
        err_console.print(f"[red]Analysis failed:[/red] {e}")
        raise typer.Exit(2)
