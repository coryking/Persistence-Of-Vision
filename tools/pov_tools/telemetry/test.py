"""Automated RPM sweep test command."""

import json
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

import typer
from rich.console import Console

from ..serial_comm import (
    DeviceConnection,
    DeviceError,
    save_csv_files,
    enrich_accel_csv,
    DEFAULT_PORT,
)
from ..constants import ButtonCommand, CALIBRATION_EFFECT, MAX_SPEED_POSITION
from .utils import create_timestamped_dir, TELEMETRY_DIR

console = Console()
err_console = Console(stderr=True)

SPEED_STEP_INTERVAL = 5.0  # Seconds between speed increases


@dataclass
class SpeedEvent:
    """Record of a speed change event."""

    timestamp: float  # time.time()
    position: int
    accel_samples: int = 0  # Samples received during this interval
    hall_packets: int = 0  # Hall packets received during this interval


@dataclass
class TestResult:
    """Results from a test run."""

    output_dir: Path
    speed_log: list[SpeedEvent] = field(default_factory=list)
    csv_files: list[Path] = field(default_factory=list)
    analysis_json: Optional[dict] = None
    aborted: bool = False
    error: Optional[str] = None


class MotorSafetyContext:
    """Context manager that ensures motor is powered off on exit."""

    def __init__(self, conn: DeviceConnection):
        self._conn = conn
        self._motor_started = False

    def __enter__(self) -> "MotorSafetyContext":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> bool:
        if self._motor_started:
            try:
                self._conn.motor_off()
                console.print("[yellow]Motor powered off (safety)[/yellow]")
            except Exception as e:
                err_console.print(f"[red]Failed to power off motor: {e}[/red]")
        return False  # Don't suppress exceptions

    def mark_motor_on(self) -> None:
        self._motor_started = True

    def mark_motor_off(self) -> None:
        self._motor_started = False


def run_test_sequence(
    conn: DeviceConnection,
    output_dir: Path,
    step_interval: float = SPEED_STEP_INTERVAL,
) -> TestResult:
    """Execute the full test sequence.

    Sequence:
    1. Reset RX stats (fresh counters for this test)
    2. Power ON motor
    3. Set calibration effect (effect 10)
    4. Start telemetry recording
    5. Every step_interval seconds, increase speed until max (position 10)
    6. Stop recording
    7. Power OFF motor
    8. Dump telemetry data

    Returns TestResult with all collected data.
    """
    result = TestResult(output_dir=output_dir)

    with MotorSafetyContext(conn) as safety:
        # Step 0: Reset RX stats for fresh counters
        console.print("[bold]Step 0:[/bold] Resetting RX stats...")
        conn.rxreset()
        console.print("[green]RX stats reset[/green]")

        # Step 1: Power ON motor
        console.print("[bold]Step 1:[/bold] Powering on motor...")
        response = conn.motor_on()
        if not response.startswith("OK") and "Already running" not in response:
            result.error = f"Failed to power on motor: {response}"
            return result
        safety.mark_motor_on()
        console.print("[green]Motor ON[/green]")

        # Step 2: Set calibration effect
        console.print(
            f"[bold]Step 2:[/bold] Setting calibration effect ({CALIBRATION_EFFECT})..."
        )
        response = conn.button(CALIBRATION_EFFECT)
        if not response.startswith("OK"):
            result.error = f"Failed to set effect: {response}"
            return result
        console.print(f"[green]Effect set to {CALIBRATION_EFFECT}[/green]")

        # Step 3: Start recording
        console.print("[bold]Step 3:[/bold] Starting telemetry recording...")
        response = conn.start()
        if not response.startswith("OK"):
            result.error = f"Failed to start recording: {response}"
            return result
        console.print("[green]Recording started[/green]")

        # Log initial position and baseline sample counts
        status = conn.status()
        if "speed_position" not in status:
            result.error = "STATUS missing speed_position - firmware mismatch?"
            return result
        current_position = int(status["speed_position"])
        last_accel_samples = int(status.get("rx_accel_samples", 0))
        last_hall_packets = int(status.get("rx_hall_packets", 0))
        result.speed_log.append(
            SpeedEvent(timestamp=time.time(), position=current_position)
        )
        console.print(f"[dim]Initial position: {current_position}[/dim]")

        # Step 4: Ramp through speeds
        console.print(
            f"[bold]Step 4:[/bold] Ramping speed ({step_interval}s per step)..."
        )

        try:
            while current_position < MAX_SPEED_POSITION:
                # Wait for interval
                console.print(
                    f"[dim]Position {current_position}, waiting {step_interval}s...[/dim]"
                )
                time.sleep(step_interval)

                # Check sample counts after dwell - sanity check
                status = conn.status()
                curr_accel = int(status.get("rx_accel_samples", 0))
                curr_hall = int(status.get("rx_hall_packets", 0))
                accel_delta = curr_accel - last_accel_samples
                hall_delta = curr_hall - last_hall_packets

                # Update the last speed event with sample counts
                if result.speed_log:
                    result.speed_log[-1].accel_samples = accel_delta
                    result.speed_log[-1].hall_packets = hall_delta

                # Warn if no data received
                if accel_delta == 0:
                    console.print(
                        f"[yellow]Warning: No accel samples at position {current_position}![/yellow]"
                    )
                elif hall_delta == 0:
                    console.print(
                        f"[yellow]Warning: No hall packets at position {current_position}![/yellow]"
                    )
                else:
                    console.print(
                        f"[dim]  → {accel_delta} samples, {hall_delta} hall[/dim]"
                    )

                last_accel_samples = curr_accel
                last_hall_packets = curr_hall

                # Increase speed
                response = conn.button(ButtonCommand.SPEED_UP)
                if not response.startswith("OK"):
                    result.error = f"Failed to increase speed: {response}"
                    break

                # Get new position - fail safely if missing
                status = conn.status()
                if "speed_position" not in status:
                    result.error = "STATUS missing speed_position during ramp"
                    break
                current_position = int(status["speed_position"])
                result.speed_log.append(
                    SpeedEvent(timestamp=time.time(), position=current_position)
                )
                console.print(
                    f"[cyan]Speed increased to position {current_position}[/cyan]"
                )

            # Final dwell at max speed
            if current_position == MAX_SPEED_POSITION:
                console.print(
                    f"[dim]At max speed, dwelling for {step_interval}s...[/dim]"
                )
                time.sleep(step_interval)

                # Check final speed level
                status = conn.status()
                curr_accel = int(status.get("rx_accel_samples", 0))
                curr_hall = int(status.get("rx_hall_packets", 0))
                accel_delta = curr_accel - last_accel_samples
                hall_delta = curr_hall - last_hall_packets

                if result.speed_log:
                    result.speed_log[-1].accel_samples = accel_delta
                    result.speed_log[-1].hall_packets = hall_delta

                if accel_delta == 0:
                    console.print(
                        f"[yellow]Warning: No accel samples at max speed![/yellow]"
                    )
                elif hall_delta == 0:
                    console.print(
                        f"[yellow]Warning: No hall packets at max speed![/yellow]"
                    )
                else:
                    console.print(
                        f"[dim]  → {accel_delta} samples, {hall_delta} hall[/dim]"
                    )

        except KeyboardInterrupt:
            console.print("\n[yellow]Interrupted - stopping gracefully...[/yellow]")
            result.aborted = True

        # Step 5: Stop recording
        console.print("[bold]Step 5:[/bold] Stopping telemetry recording...")
        response = conn.stop()
        if not response.startswith("OK"):
            console.print(f"[yellow]Warning: stop returned {response}[/yellow]")
        else:
            console.print("[green]Recording stopped[/green]")

        # Step 6: Power OFF motor
        console.print("[bold]Step 6:[/bold] Powering off motor...")
        response = conn.motor_off()
        if response.startswith("OK") or "Already stopped" in response:
            safety.mark_motor_off()
            console.print("[green]Motor OFF[/green]")
        else:
            console.print(f"[yellow]Warning: motor_off returned {response}[/yellow]")

        # Step 7: Dump telemetry data
        console.print("[bold]Step 7:[/bold] Downloading telemetry data...")
        files = conn.dump()
        if files:
            saved = save_csv_files(files, output_dir)
            enriched = enrich_accel_csv(output_dir)
            result.csv_files = saved
            console.print(f"[green]Saved {len(saved)} files to {output_dir}[/green]")
            if enriched:
                console.print("[dim]Enriched accel CSV with rotation data[/dim]")
        else:
            console.print("[yellow]No telemetry data captured[/yellow]")

    return result


def write_speed_log(result: TestResult) -> None:
    """Write speed log CSV to output directory."""
    if not result.speed_log:
        return

    log_path = result.output_dir / "speed_log.csv"
    with open(log_path, "w") as f:
        f.write("timestamp,position,accel_samples,hall_packets\n")
        for event in result.speed_log:
            f.write(
                f"{event.timestamp:.6f},{event.position},"
                f"{event.accel_samples},{event.hall_packets}\n"
            )
    console.print(f"[dim]Speed log written to {log_path.name}[/dim]")


def run_analysis(result: TestResult) -> Optional[dict]:
    """Run analysis on captured data and return JSON output."""
    from ..analysis import load_and_enrich, run_all_analyses, generate_report
    from ..analysis.output import results_to_json

    accel_path = result.output_dir / "MSG_ACCEL_SAMPLES.csv"
    hall_path = result.output_dir / "MSG_HALL_EVENT.csv"

    if not accel_path.exists() or not hall_path.exists():
        console.print("[yellow]Skipping analysis - missing data files[/yellow]")
        return None

    try:
        ctx = load_and_enrich(result.output_dir)
        results = run_all_analyses(ctx)
        generate_report(results, ctx)
        return results_to_json(results, ctx)
    except Exception as e:
        console.print(f"[yellow]Analysis failed: {e}[/yellow]")
        return None


def test(
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p", help="Serial port"),
    output: Optional[Path] = typer.Option(
        None, "--output", "-o", help="Output directory base"
    ),
    step_interval: float = typer.Option(
        SPEED_STEP_INTERVAL, "--interval", "-i", help="Seconds between speed steps"
    ),
    json_output: bool = typer.Option(False, "--json", help="Output results as JSON"),
    skip_analysis: bool = typer.Option(
        False, "--skip-analysis", help="Skip running analysis after capture"
    ),
) -> None:
    """Run automated RPM sweep test.

    Sequence:
    1. Create timestamped output directory
    2. Reset RX stats
    3. Power ON motor
    4. Set calibration effect (effect 10)
    5. Start telemetry recording
    6. Ramp through all speed positions (1-10)
    7. Stop recording and power OFF motor
    8. Dump telemetry to output directory
    9. Run analysis (unless --skip-analysis)

    Use Ctrl+C to abort - motor will be safely powered off.
    """
    base_dir = output or TELEMETRY_DIR
    output_dir = create_timestamped_dir(base_dir)

    if not json_output:
        console.print("[bold]POV Telemetry Test[/bold]")
        console.print(f"Output: {output_dir}")
        console.print(f"Step interval: {step_interval}s")
        console.print()

    try:
        with DeviceConnection(port=port) as conn:
            result = run_test_sequence(conn, output_dir, step_interval)

    except DeviceError as e:
        if json_output:
            print(json.dumps({"success": False, "error": str(e)}))
        else:
            err_console.print(f"[red]Device error: {e}[/red]")
        raise typer.Exit(2)

    # Write speed log
    write_speed_log(result)

    # Run analysis if requested
    if not skip_analysis and not result.error:
        if not json_output:
            console.print()
            console.print("[bold]Running analysis...[/bold]")
        result.analysis_json = run_analysis(result)

    # Output results
    if json_output:
        output_data = {
            "success": result.error is None and not result.aborted,
            "output_dir": str(result.output_dir),
            "files": [str(p) for p in result.csv_files],
            "speed_log": [
                {
                    "timestamp": e.timestamp,
                    "position": e.position,
                    "accel_samples": e.accel_samples,
                    "hall_packets": e.hall_packets,
                }
                for e in result.speed_log
            ],
            "aborted": result.aborted,
            "error": result.error,
            "analysis": result.analysis_json,
        }
        print(json.dumps(output_data, indent=2))
    else:
        console.print()
        if result.error:
            err_console.print(f"[red]Test failed: {result.error}[/red]")
            raise typer.Exit(1)
        elif result.aborted:
            console.print("[yellow]Test aborted by user[/yellow]")
            raise typer.Exit(130)  # Standard Ctrl+C exit code
        else:
            console.print("[green]Test completed successfully[/green]")
            console.print(f"[dim]Results in: {result.output_dir}[/dim]")
