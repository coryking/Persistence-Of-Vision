"""Shared utilities for telemetry commands."""

from datetime import datetime
from pathlib import Path

from rich.console import Console

from ..serial_comm import DeviceConnection

console = Console()
err_console = Console(stderr=True)

# Output directory for downloaded telemetry
TELEMETRY_DIR = Path(__file__).parent.parent.parent / "telemetry"


def create_timestamped_dir(base_dir: Path) -> Path:
    """Create a timestamped subdirectory for telemetry output.

    Format: 2025-01-11T14-33-22 (ISO-ish, filesystem-safe)
    """
    timestamp = datetime.now().strftime("%Y-%m-%dT%H-%M-%S")
    output_dir = base_dir / timestamp
    output_dir.mkdir(parents=True, exist_ok=True)
    return output_dir


def list_capture_dirs(base_dir: Path) -> list[Path]:
    """List available capture directories, sorted newest first."""
    if not base_dir.exists():
        return []

    dirs = []
    for item in base_dir.iterdir():
        if item.is_dir() and (item / "MSG_ACCEL_SAMPLES.csv").exists():
            dirs.append(item)

    # Sort by name (timestamp format sorts chronologically)
    return sorted(dirs, reverse=True)


def get_connection(port: str) -> DeviceConnection:
    """Create device connection with error handling."""
    return DeviceConnection(port=port)


def get_capture_preview(capture_dir: Path) -> str:
    """Get a brief preview of a capture directory for the picker."""
    accel_path = capture_dir / "MSG_ACCEL_SAMPLES.csv"
    hall_path = capture_dir / "MSG_HALL_EVENT.csv"

    try:
        import pandas as pd

        info_parts = []

        if accel_path.exists():
            accel = pd.read_csv(accel_path)
            info_parts.append(f"{len(accel):,} samples")

        if hall_path.exists():
            hall = pd.read_csv(hall_path)
            hall["rpm"] = 60_000_000 / hall["period_us"]
            rpm_min = hall["rpm"].min()
            rpm_max = hall["rpm"].max()
            info_parts.append(f"{rpm_min:.0f}-{rpm_max:.0f} RPM")

        return " | ".join(info_parts) if info_parts else "no data"
    except Exception:
        return "error reading"


def interactive_capture_picker(base_dir: Path) -> Path | None:
    """Show interactive menu to pick a capture directory."""
    from rich.prompt import Prompt

    captures = list_capture_dirs(base_dir)

    if not captures:
        err_console.print(f"[red]No capture directories found in {base_dir}[/red]")
        return None

    console.print("\n[bold]Available captures:[/bold]\n")

    for i, capture in enumerate(captures, 1):
        preview = get_capture_preview(capture)
        console.print(f"  [cyan]{i}[/cyan]. {capture.name}  [dim]({preview})[/dim]")

    console.print()

    choice = Prompt.ask(
        "Select capture",
        choices=[str(i) for i in range(1, len(captures) + 1)],
        default="1",
    )

    return captures[int(choice) - 1]
