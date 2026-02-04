"""POV Tools CLI entrypoint."""

import typer

from . import telemetry
from . import calibration

app = typer.Typer(
    name="pov",
    help="POV Display Tools - CLI for telemetry capture and analysis",
    no_args_is_help=True,
)

# Add subcommand groups
app.add_typer(telemetry.app, name="telemetry")
app.add_typer(calibration.app, name="calibration")


if __name__ == "__main__":
    app()
