"""RX stats command - ESP-NOW receive statistics for debugging."""

import json

import typer

from ..serial_comm import DeviceError, DEFAULT_PORT
from .utils import get_connection, console, err_console


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
