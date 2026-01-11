"""Telemetry capture commands."""

import json
import sys
from datetime import datetime
from pathlib import Path
from typing import Optional

import typer
from rich.console import Console
from rich.table import Table

from .serial_comm import (
    DeviceConnection,
    DeviceError,
    save_csv_files,
    enrich_accel_csv,
    DEFAULT_PORT,
    RxStats,
)

app = typer.Typer(help="Telemetry capture commands")
console = Console()
err_console = Console(stderr=True)

# Output directory for downloaded telemetry
TELEMETRY_DIR = Path(__file__).parent.parent.parent / "telemetry"


def create_timestamped_dir(base_dir: Path) -> Path:
    """Create a timestamped subdirectory for telemetry output.

    Format: 2025-01-11T14-33-22 (ISO-ish, filesystem-safe)
    """
    timestamp = datetime.now().strftime("%Y-%m-%dT%H-%M-%S")
    output_dir = base_dir / timestamp
    output_dir.mkdir(parents=True, exist_ok=True)
    return output_dir


def list_capture_dirs(base_dir: Path) -> list[Path]:
    """List available capture directories, sorted newest first."""
    if not base_dir.exists():
        return []

    dirs = []
    for item in base_dir.iterdir():
        if item.is_dir() and (item / "MSG_ACCEL_SAMPLES.csv").exists():
            dirs.append(item)

    # Sort by name (timestamp format sorts chronologically)
    return sorted(dirs, reverse=True)


def get_connection(port: str) -> DeviceConnection:
    """Create device connection with error handling."""
    return DeviceConnection(port=port)


@app.command()
def status(
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p", help="Serial port"),
    json_output: bool = typer.Option(False, "--json", help="Output as JSON"),
):
    """Get capture state and file inventory."""
    try:
        with get_connection(port) as conn:
            state = conn.status()
            files = conn.list_files()

            if json_output:
                data = {
                    "state": state,
                    "files": [
                        {"filename": f.filename, "records": f.records, "bytes": f.bytes}
                        for f in files
                    ]
                }
                print(json.dumps(data))
            else:
                console.print(f"State: [bold]{state}[/bold]")
                if files:
                    table = Table(title="Files")
                    table.add_column("Filename")
                    table.add_column("Records", justify="right")
                    table.add_column("Bytes", justify="right")
                    for f in files:
                        table.add_row(f.filename, str(f.records), str(f.bytes))
                    console.print(table)
                else:
                    console.print("[dim]No telemetry files[/dim]")

    except DeviceError as e:
        err_console.print(f"[red]Error:[/red] {e}")
        raise typer.Exit(2)


@app.command()
def start(
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p", help="Serial port"),
    json_output: bool = typer.Option(False, "--json", help="Output as JSON"),
):
    """Start recording telemetry."""
    try:
        with get_connection(port) as conn:
            result = conn.start()

            if json_output:
                success = result == "OK"
                print(json.dumps({"success": success, "message": result}))
                if not success:
                    raise typer.Exit(1)
            else:
                if result == "OK":
                    console.print("[green]Recording started[/green]")
                else:
                    err_console.print(f"[red]{result}[/red]")
                    raise typer.Exit(1)

    except DeviceError as e:
        err_console.print(f"[red]Error:[/red] {e}")
        raise typer.Exit(2)


@app.command()
def stop(
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p", help="Serial port"),
    json_output: bool = typer.Option(False, "--json", help="Output as JSON"),
):
    """Stop recording telemetry."""
    try:
        with get_connection(port) as conn:
            result = conn.stop()

            if json_output:
                success = result == "OK"
                print(json.dumps({"success": success, "message": result}))
                if not success:
                    raise typer.Exit(1)
            else:
                if result == "OK":
                    console.print("[green]Recording stopped[/green]")
                else:
                    err_console.print(f"[red]{result}[/red]")
                    raise typer.Exit(1)

    except DeviceError as e:
        err_console.print(f"[red]Error:[/red] {e}")
        raise typer.Exit(2)


@app.command("list")
def list_files(
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p", help="Serial port"),
    json_output: bool = typer.Option(False, "--json", help="Output as JSON"),
):
    """List telemetry files on device."""
    try:
        with get_connection(port) as conn:
            files = conn.list_files()

            if json_output:
                data = [
                    {"filename": f.filename, "records": f.records, "bytes": f.bytes}
                    for f in files
                ]
                print(json.dumps(data))
            else:
                if files:
                    table = Table()
                    table.add_column("Filename")
                    table.add_column("Records", justify="right")
                    table.add_column("Bytes", justify="right")
                    for f in files:
                        table.add_row(f.filename, str(f.records), str(f.bytes))
                    console.print(table)
                else:
                    console.print("[dim]No telemetry files[/dim]")

    except DeviceError as e:
        err_console.print(f"[red]Error:[/red] {e}")
        raise typer.Exit(2)


@app.command()
def dump(
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p", help="Serial port"),
    output: Optional[Path] = typer.Option(None, "--output", "-o", help="Output directory (creates timestamped subdir)"),
    json_output: bool = typer.Option(False, "--json", help="Output as JSON"),
):
    """Download all telemetry files to timestamped local directory."""
    base_dir = output or TELEMETRY_DIR

    try:
        with get_connection(port) as conn:
            files = conn.dump()

            if not files:
                if json_output:
                    print(json.dumps({"files": [], "output_dir": None}))
                else:
                    console.print("[dim]No telemetry data to dump[/dim]")
                return

            # Create timestamped subdirectory
            output_dir = create_timestamped_dir(base_dir)

            saved = save_csv_files(files, output_dir)

            # Enrich accel CSV with rotation_num and micros_since_hall
            # (computed from hall event timestamps, no longer in firmware)
            enriched = enrich_accel_csv(output_dir)

            if json_output:
                data = {
                    "files": [str(p) for p in saved],
                    "output_dir": str(output_dir),
                    "enriched": enriched
                }
                print(json.dumps(data))
            else:
                console.print(f"[green]Saved {len(saved)} files to {output_dir}[/green]")
                for p in saved:
                    console.print(f"  {p.name}")
                if enriched:
                    console.print("[dim]Added rotation_num and micros_since_hall to accel CSV[/dim]")

    except DeviceError as e:
        err_console.print(f"[red]Error:[/red] {e}")
        raise typer.Exit(2)


@app.command()
def delete(
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p", help="Serial port"),
    json_output: bool = typer.Option(False, "--json", help="Output as JSON"),
    force: bool = typer.Option(False, "--force", "-f", help="Skip confirmation"),
):
    """Delete all telemetry files on device."""
    if not force and not json_output:
        confirm = typer.confirm("Delete all telemetry files?")
        if not confirm:
            raise typer.Abort()

    try:
        with get_connection(port) as conn:
            result = conn.delete()

            if json_output:
                print(json.dumps({"success": True, "message": result}))
            else:
                console.print("[green]Files deleted[/green]")

    except DeviceError as e:
        err_console.print(f"[red]Error:[/red] {e}")
        raise typer.Exit(2)


@app.command()
def view(
    msg_type: str = typer.Argument(..., help="Message type (MSG_ACCEL_SAMPLES, MSG_HALL_EVENT, MSG_TELEMETRY)"),
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p", help="Serial port"),
    limit: int = typer.Option(20, "--limit", "-n", help="Number of rows to display"),
):
    """View a specific telemetry file from device."""
    try:
        with get_connection(port) as conn:
            files = conn.dump()

            # Find matching file
            target = f"{msg_type}.bin"
            matching = [f for f in files if f.filename == target]

            if not matching:
                err_console.print(f"[red]File not found:[/red] {target}")
                console.print("Available files:")
                for f in files:
                    console.print(f"  {f.filename}")
                raise typer.Exit(1)

            f = matching[0]
            console.print(f"[bold]{f.filename}[/bold] ({len(f.rows)} records)")
            console.print(f"[dim]{f.header}[/dim]")

            for row in f.rows[:limit]:
                console.print(row)

            if len(f.rows) > limit:
                console.print(f"[dim]... and {len(f.rows) - limit} more rows[/dim]")

    except DeviceError as e:
        err_console.print(f"[red]Error:[/red] {e}")
        raise typer.Exit(2)


@app.command()
def rxstats(
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p", help="Serial port"),
    json_output: bool = typer.Option(False, "--json", help="Output as JSON"),
    reset: bool = typer.Option(False, "--reset", "-r", help="Reset counters after reading"),
):
    """Get ESP-NOW receive statistics (for debugging packet delivery)."""
    try:
        with get_connection(port) as conn:
            stats = conn.rxstats()

            if reset:
                conn.rxreset()

            if json_output:
                data = {
                    "hall_packets": stats.hall_packets,
                    "accel_packets": stats.accel_packets,
                    "accel_samples": stats.accel_samples,
                    "last_accel_len": stats.last_accel_len,
                    "reset": reset
                }
                print(json.dumps(data))
            else:
                console.print(f"Hall packets:    [bold]{stats.hall_packets}[/bold]")
                console.print(f"Accel packets:   [bold]{stats.accel_packets}[/bold]")
                console.print(f"Accel samples:   [bold]{stats.accel_samples}[/bold]")
                console.print(f"Last accel len:  [bold]{stats.last_accel_len}[/bold] bytes")

                # Diagnostic hints
                if stats.hall_packets > 0 and stats.accel_packets == 0:
                    console.print("\n[yellow]Hall packets arriving but no accel packets![/yellow]")
                    console.print("[dim]LED display may not be sending accel data (queue/task issue or send rejection)[/dim]")
                elif stats.accel_packets > 0:
                    console.print(f"\n[green]Accel packets ARE arriving[/green]")

                if reset:
                    console.print("\n[dim]Counters reset[/dim]")

    except DeviceError as e:
        err_console.print(f"[red]Error:[/red] {e}")
        raise typer.Exit(2)


def _get_capture_preview(capture_dir: Path) -> str:
    """Get a brief preview of a capture directory for the picker."""
    accel_path = capture_dir / "MSG_ACCEL_SAMPLES.csv"
    hall_path = capture_dir / "MSG_HALL_EVENT.csv"

    try:
        import pandas as pd

        info_parts = []

        if accel_path.exists():
            accel = pd.read_csv(accel_path)
            info_parts.append(f"{len(accel):,} samples")

        if hall_path.exists():
            hall = pd.read_csv(hall_path)
            hall["rpm"] = 60_000_000 / hall["period_us"]
            rpm_min = hall["rpm"].min()
            rpm_max = hall["rpm"].max()
            info_parts.append(f"{rpm_min:.0f}-{rpm_max:.0f} RPM")

        return " | ".join(info_parts) if info_parts else "no data"
    except Exception:
        return "error reading"


def _interactive_capture_picker(base_dir: Path) -> Path | None:
    """Show interactive menu to pick a capture directory."""
    from rich.prompt import Prompt

    captures = list_capture_dirs(base_dir)

    if not captures:
        err_console.print(f"[red]No capture directories found in {base_dir}[/red]")
        return None

    console.print("\n[bold]Available captures:[/bold]\n")

    for i, capture in enumerate(captures, 1):
        preview = _get_capture_preview(capture)
        console.print(f"  [cyan]{i}[/cyan]. {capture.name}  [dim]({preview})[/dim]")

    console.print()

    choice = Prompt.ask(
        "Select capture",
        choices=[str(i) for i in range(1, len(captures) + 1)],
        default="1",
    )

    return captures[int(choice) - 1]


@app.command()
def analyze(
    data_dir: Optional[Path] = typer.Argument(
        None, help="Capture directory to analyze (interactive picker if not specified)"
    ),
):
    """Analyze telemetry data and generate report.

    Outputs JSON to stdout. HTML report and plots written to data_dir.
    """
    from .analysis import (
        load_and_enrich,
        run_all_analyses,
        results_to_json,
        generate_report,
    )

    # Determine which directory to analyze
    if data_dir is None:
        # Check for captures in default location
        data_dir = _interactive_capture_picker(TELEMETRY_DIR)
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