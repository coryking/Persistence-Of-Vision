"""Status command - get capture state and file inventory."""

import json

import typer
from rich.table import Table

from ..serial_comm import DeviceError, DEFAULT_PORT
from .utils import get_connection, console, err_console


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
