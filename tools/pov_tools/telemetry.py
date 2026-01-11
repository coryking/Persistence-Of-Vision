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
    RxStats,
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
                    if stats.last_accel_len > 256:
                        console.print(f"[yellow]Warning: last_len={stats.last_accel_len} > MAX_CAPTURE_PAYLOAD(256)[/yellow]")
                        console.print("[dim]Packets may be dropped in captureWrite()[/dim]")

                if reset:
                    console.print("\n[dim]Counters reset[/dim]")

    except DeviceError as e:
        err_console.print(f"[red]Error:[/red] {e}")
        raise typer.Exit(2)
