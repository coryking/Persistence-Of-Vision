"""Status command - get unified device status."""

import json

import typer
from rich.table import Table

from ..serial_comm import DeviceError, DEFAULT_PORT
from .utils import get_connection, console, err_console


def status(
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p", help="Serial port"),
    json_output: bool = typer.Option(False, "--json", help="Output as JSON"),
) -> None:
    """Get unified device status (motor, capture, RX stats) and file inventory."""
    try:
        with get_connection(port) as conn:
            device_status = conn.status()
            files = conn.list_files()

            if json_output:
                # Pass through raw status dict, add files
                output: dict = dict(device_status)
                output["files"] = [
                    {"filename": f.filename, "records": f.records, "bytes": f.bytes}
                    for f in files
                ]
                print(json.dumps(output))
            else:
                # Motor status
                motor_on = device_status.get("motor_enabled") == "1"
                motor_state = "[green]ON[/green]" if motor_on else "[red]OFF[/red]"
                position = device_status.get("speed_position", "?")
                console.print(f"Motor: {motor_state} (position {position})")

                # Capture status
                capture = device_status.get("capture_state", "UNKNOWN")
                console.print(f"Capture: [bold]{capture}[/bold]")

                # RX stats
                console.print(
                    f"RX: {device_status.get('rx_hall_packets', '?')} hall, "
                    f"{device_status.get('rx_accel_packets', '?')} accel pkts, "
                    f"{device_status.get('rx_accel_samples', '?')} samples"
                )

                # Files
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
