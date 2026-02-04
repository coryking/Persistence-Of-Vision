"""Interactive command - enter new measurements."""

import typer
from rich.console import Console
from rich.prompt import Prompt, Confirm
from rich.panel import Panel
from rich.table import Table
from rich.syntax import Syntax

from .core import (
    calibrate,
    compute_adjustments,
    DEFAULT_STRIP_POSITION,
    DEFAULT_TRIANGLE,
    IDEAL_RING_PITCH_MM,
)

console = Console()


def interactive() -> None:
    """Enter new measurements interactively and see calibration results."""
    console.print(Panel.fit(
        "[bold]LED Geometry Calibration[/bold]\n"
        "[dim]See docs/led_display/HARDWARE.md for measurement methodology[/dim]"
    ))
    console.print()

    # Strip Position measurements
    console.print("[bold]Strip Position[/bold] (tip to innermost LED inner edge)")
    strip_position = {}
    for arm in ['ARM1', 'ARM2', 'ARM3']:
        default = DEFAULT_STRIP_POSITION[arm]
        val = Prompt.ask(f"  {arm}", default=str(default))
        strip_position[arm] = float(val)
    console.print()

    # Triangle measurements (optional)
    triangle = None
    if Confirm.ask("Enter Triangle measurements for cross-validation?", default=False):
        console.print()
        console.print("[bold]Triangle[/bold] (center-to-center between innermost LEDs)")
        triangle = {}
        for (a1, a2), default in DEFAULT_TRIANGLE.items():
            val = Prompt.ask(f"  {a1}↔{a2}", default=str(default))
            triangle[(a1, a2)] = float(val)
    console.print()

    # Run calibration
    result = calibrate(strip_position, triangle)
    adjustments = compute_adjustments(result.innermost_centers)

    # Results
    console.print(Panel.fit("[bold green]Calibration Results[/bold green]"))
    console.print()

    # Innermost centers table
    table = Table(title="Innermost LED Centers")
    table.add_column("Arm")
    table.add_column("Radius (mm)", justify="right")
    table.add_column("Ring Assignment")
    for arm, rings in [('ARM3', '0, 3, 6, 9...'), ('ARM2', '1, 4, 7, 10...'), ('ARM1', '2, 5, 8, 11...')]:
        table.add_row(arm, f"{result.innermost_centers[arm]:.2f}", rings)
    console.print(table)
    console.print()

    # Display boundaries
    console.print(f"[bold]Display Boundaries:[/bold]")
    console.print(f"  Inner edge: {result.inner_edge:.1f}mm  |  Outer edge: {result.outer_edge:.1f}mm")
    console.print(f"  Inner hole Ø: {2 * result.inner_edge:.1f}mm  |  Span: {result.outer_edge - result.inner_edge:.1f}mm")
    console.print()

    # Interlacing errors
    if result.std_dev > 0.1:
        console.print(f"[bold yellow]Interlacing Error:[/bold yellow] std dev = {result.std_dev:.2f}mm")
        console.print(f"  [green]Fix by moving ARM3 strip {adjustments['arm3_only']:+.2f}mm toward tip[/green]")
    else:
        console.print("[bold green]Interlacing: Good![/bold green]")
    console.print()

    # C++ constants
    console.print(Panel.fit("[bold]C++ Constants[/bold] (copy to geometry.h)"))
    code = f"""\
constexpr float ARM3_INNER_RADIUS_MM = {result.innermost_centers['ARM3']:.2f}f;
constexpr float ARM2_INNER_RADIUS_MM = {result.innermost_centers['ARM2']:.2f}f;
constexpr float ARM1_INNER_RADIUS_MM = {result.innermost_centers['ARM1']:.2f}f;"""
    syntax = Syntax(code, "cpp", theme="monokai", line_numbers=False)
    console.print(syntax)
