"""Report command - show calibration analysis."""

import json

import typer
from rich.console import Console
from rich.table import Table
from rich.panel import Panel
from rich.syntax import Syntax

from .core import (
    calibrate,
    compute_adjustments,
    DEFAULT_STRIP_POSITION,
    DEFAULT_TRIANGLE,
    IDEAL_RING_PITCH_MM,
    LED_PITCH_MM,
    LED_CHIP_SIZE_MM,
)

console = Console()


def report(
    json_output: bool = typer.Option(False, "--json", help="Output as JSON"),
) -> None:
    """Show LED geometry calibration report using stored measurements."""
    result = calibrate(DEFAULT_STRIP_POSITION, DEFAULT_TRIANGLE)
    adjustments = compute_adjustments(result.innermost_centers)

    if json_output:
        output = {
            "innermost_centers": result.innermost_centers,
            "display": {
                "inner_edge_mm": result.inner_edge,
                "outer_edge_mm": result.outer_edge,
                "inner_hole_diameter_mm": 2 * result.inner_edge,
                "display_span_mm": result.outer_edge - result.inner_edge,
            },
            "interlacing": {
                "ideal_pitch_mm": IDEAL_RING_PITCH_MM,
                "min_gap_mm": result.min_gap,
                "max_gap_mm": result.max_gap,
                "std_dev_mm": result.std_dev,
            },
            "adjustments": adjustments,
            "rings": [
                {"ring": i, "radius_mm": led.radius, "arm": led.arm, "led_index": led.led_index}
                for i, led in enumerate(result.all_leds)
            ],
        }
        print(json.dumps(output, indent=2))
        return

    # Innermost LED centers
    console.print(Panel.fit("[bold]Innermost LED Centers[/bold]"))
    for arm in ['ARM3', 'ARM2', 'ARM1']:
        console.print(f"  {arm}: [cyan]{result.innermost_centers[arm]:.2f}mm[/cyan]")
    console.print()

    # Triangle validation
    if result.triangle_validation:
        console.print(Panel.fit("[bold]Triangle Validation[/bold]"))
        for (a1, a2), data in result.triangle_validation.items():
            err_color = "green" if abs(data['error']) < 0.5 else "yellow" if abs(data['error']) < 1.0 else "red"
            console.print(
                f"  {a1}↔{a2}: measured={data['measured']:.1f}mm, "
                f"computed={data['computed']:.1f}mm, "
                f"error=[{err_color}]{data['error']:+.1f}mm[/{err_color}]"
            )
        console.print()

    # Display boundaries
    console.print(Panel.fit("[bold]Display Boundaries[/bold]"))
    console.print(f"  Inner edge:     [cyan]{result.inner_edge:.1f}mm[/cyan]")
    console.print(f"  Outer edge:     [cyan]{result.outer_edge:.1f}mm[/cyan]")
    console.print(f"  Inner hole Ø:   [cyan]{2 * result.inner_edge:.1f}mm[/cyan]")
    console.print(f"  Display span:   [cyan]{result.outer_edge - result.inner_edge:.1f}mm[/cyan]")
    console.print()

    # Interlacing analysis
    console.print(Panel.fit("[bold]Interlacing Analysis[/bold]"))
    console.print(f"  Ideal ring pitch: [dim]{IDEAL_RING_PITCH_MM:.3f}mm[/dim]")
    console.print(f"  Actual gaps: min=[cyan]{result.min_gap:.2f}mm[/cyan], max=[cyan]{result.max_gap:.2f}mm[/cyan]")
    console.print(f"  Std deviation: [cyan]{result.std_dev:.2f}mm[/cyan]")
    console.print()

    # Transition errors
    console.print(Panel.fit("[bold]Transition Errors[/bold]"))
    transitions = {}
    for g in result.gaps:
        key = (g['from_arm'], g['to_arm'])
        if key not in transitions:
            transitions[key] = g

    for (from_arm, to_arm), g in sorted(transitions.items(), key=lambda x: abs(x[1]['error']), reverse=True):
        err_color = "green" if abs(g['error']) < 0.2 else "yellow" if abs(g['error']) < 0.5 else "red"
        console.print(
            f"  {from_arm}→{to_arm}: {g['gap']:.2f}mm "
            f"(error: [{err_color}]{g['error']:+.2f}mm[/{err_color}])"
        )
    console.print()

    # Adjustment recommendations
    console.print(Panel.fit("[bold]Adjustment Recommendations[/bold]"))
    console.print("  [green]To fix by adjusting ARM3 only (easiest):[/green]")
    direction = "toward tip" if adjustments['arm3_only'] > 0 else "toward center"
    console.print(f"    Move ARM3 strip [cyan]{adjustments['arm3_only']:+.2f}mm[/cyan] ({direction})")
    console.print()
    console.print("  [dim]Alternative - adjust ARM1 and ARM2:[/dim]")
    console.print(f"    Move ARM2 strip [dim]{adjustments['arm2']:+.2f}mm[/dim]")
    console.print(f"    Move ARM1 strip [dim]{adjustments['arm1']:+.2f}mm[/dim]")
    console.print()

    # C++ constants
    console.print(Panel.fit("[bold]C++ Constants (for geometry.h)[/bold]"))
    code = f"""\
constexpr float ARM3_INNER_RADIUS_MM = {result.innermost_centers['ARM3']:.2f}f;
constexpr float ARM2_INNER_RADIUS_MM = {result.innermost_centers['ARM2']:.2f}f;
constexpr float ARM1_INNER_RADIUS_MM = {result.innermost_centers['ARM1']:.2f}f;"""
    syntax = Syntax(code, "cpp", theme="monokai", line_numbers=False)
    console.print(syntax)
