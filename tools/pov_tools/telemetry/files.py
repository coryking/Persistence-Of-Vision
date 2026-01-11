"""File commands - list, dump, delete, view telemetry files."""

import json
from pathlib import Path
from typing import Optional

import typer
from rich.table import Table

from ..serial_comm import DeviceError, save_csv_files, enrich_accel_csv, DEFAULT_PORT
from .utils import get_connection, create_timestamped_dir, console, err_console, TELEMETRY_DIR


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
