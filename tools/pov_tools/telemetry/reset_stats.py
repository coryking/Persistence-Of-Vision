"""Reset rotor diagnostic stats command."""

import json

import typer

from ..serial_comm import DeviceError, DEFAULT_PORT
from .utils import get_connection, console, err_console


def reset_stats(
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p", help="Serial port"),
    json_output: bool = typer.Option(False, "--json", help="Output as JSON"),
):
    """Reset rotor diagnostic stats on the LED display.

    Sends RESET_ROTOR_STATS command via motor controller to LED display.
    This zeros all counters and updates the created_us timestamp.
    """
    try:
        with get_connection(port) as conn:
            result = conn.reset_rotor_stats()

            if json_output:
                success = result == "OK"
                print(json.dumps({"success": success, "message": result}))
                if not success:
                    raise typer.Exit(1)
            else:
                if result == "OK":
                    console.print("[green]Rotor stats reset[/green]")
                else:
                    err_console.print(f"[red]{result}[/red]")
                    raise typer.Exit(1)

    except DeviceError as e:
        err_console.print(f"[red]Error:[/red] {e}")
        raise typer.Exit(2)
