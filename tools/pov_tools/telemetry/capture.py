"""Capture commands - start and stop recording."""

import json

import typer

from ..serial_comm import DeviceError, DEFAULT_PORT
from .utils import get_connection, console, err_console


def start(
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p", help="Serial port"),
    json_output: bool = typer.Option(False, "--json", help="Output as JSON"),
):
    """Start recording telemetry.

    Note: Call 'delete' first to erase partitions. START_CAPTURE is now instant.
    """
    try:
        with get_connection(port) as conn:
            result = conn.start_capture()

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


def stop(
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p", help="Serial port"),
    json_output: bool = typer.Option(False, "--json", help="Output as JSON"),
):
    """Stop recording telemetry."""
    try:
        with get_connection(port) as conn:
            result = conn.stop_capture()

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
