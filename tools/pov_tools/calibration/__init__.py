"""LED geometry calibration commands."""

import typer

from .report import report
from .interactive import interactive

app = typer.Typer(help="LED geometry calibration tools")

app.command()(report)
app.command()(interactive)
