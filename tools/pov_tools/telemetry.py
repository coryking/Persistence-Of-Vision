"""Telemetry capture commands."""

import json
import sys
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
)

app = typer.Typer(help="Telemetry capture commands")
console = Console()
err_console = Console(stderr=True)

# Output directory for downloaded telemetry
TELEMETRY_DIR = Path(__file__).parent.parent.parent / "telemetry"


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
    output: Optional[Path] = typer.Option(None, "--output", "-o", help="Output directory"),
    json_output: bool = typer.Option(False, "--json", help="Output as JSON"),
):
    """Download all telemetry files to local directory."""
    output_dir = output or TELEMETRY_DIR

    try:
        with get_connection(port) as conn:
            files = conn.dump()

            if not files:
                if json_output:
                    print(json.dumps({"files": [], "output_dir": str(output_dir)}))
                else:
                    console.print("[dim]No telemetry data to dump[/dim]")
                return

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
