"""Telemetry capture commands."""

import typer

from .status import status
from .capture import start, stop
from .files import list_files, dump, delete, view
from .analyze_cmd import analyze
from .test import test

app = typer.Typer(help="Telemetry capture commands")

# Register commands
app.command()(status)
app.command()(start)
app.command()(stop)
app.command("list")(list_files)
app.command()(dump)
app.command()(delete)
app.command()(view)
app.command()(analyze)
app.command()(test)
