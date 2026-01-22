"""Automated per-step telemetry capture command."""

import json
import tarfile
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Optional

import typer
from rich.console import Console
from rich.prompt import Confirm

from ..serial_comm import (
    DeviceConnection,
    DeviceError,
    save_csv_files,
    enrich_accel_csv,
    extract_rpm_from_dump,
    DEFAULT_PORT,
)
from ..constants import ButtonCommand, CALIBRATION_EFFECT, MAX_SPEED_POSITION
from .utils import create_timestamped_dir, TELEMETRY_DIR

console = Console()
err_console = Console(stderr=True)

DEFAULT_SETTLE_TIME = 3.0  # Seconds to wait after speed change
DEFAULT_RECORD_TIME = 5.0  # Seconds to record at each speed


@dataclass
class StepMetrics:
    """Computed metrics for one speed step."""

    rpm_mean: float
    rpm_min: float
    rpm_max: float
    rpm_cv_pct: float  # coefficient of variation
    wobble_mean: float
    wobble_peak: float
    dropped_samples: int
    samples_per_rot_mean: float
    x_saturation_pct: float
    gz_saturation_pct: float


@dataclass
class StepResult:
    """Result from capturing one speed step."""

    position: int
    file_suffix: str
    csv_files: list[Path] = field(default_factory=list)
    accel_samples: int = 0
    hall_packets: int = 0
    rpm: Optional[float] = None
    success: bool = True
    error: Optional[str] = None
    metrics: Optional[StepMetrics] = None


@dataclass
class TestResult:
    """Results from a full test run."""

    output_dir: Path
    steps: list[StepResult] = field(default_factory=list)
    aborted: bool = False
    error: Optional[str] = None
    started_at: Optional[datetime] = None
    completed_at: Optional[datetime] = None
    settle_time: float = DEFAULT_SETTLE_TIME
    record_time: float = DEFAULT_RECORD_TIME


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


def compute_step_metrics(output_dir: Path, file_suffix: str) -> Optional[StepMetrics]:
    """Compute metrics from the enriched accel CSV for a step.

    Returns StepMetrics if the enriched CSV exists, None otherwise.
    """
    import pandas as pd

    # Find the enriched accel CSV
    pattern = f"MSG_ACCEL_SAMPLES{file_suffix}.csv"
    accel_files = list(output_dir.glob(pattern))
    if not accel_files:
        return None

    accel_path = accel_files[0]
    try:
        df = pd.read_csv(accel_path)
    except Exception:
        return None

    # Check for required columns (enriched CSV)
    required_cols = ["rpm", "gyro_wobble_dps", "sequence_num"]
    if not all(col in df.columns for col in required_cols):
        return None

    # RPM stats
    rpm_series = df["rpm"].dropna()
    if len(rpm_series) == 0:
        return None

    rpm_mean = rpm_series.mean()
    rpm_min = rpm_series.min()
    rpm_max = rpm_series.max()
    rpm_std = rpm_series.std()
    rpm_cv_pct = (rpm_std / rpm_mean * 100) if rpm_mean > 0 else 0.0

    # Wobble stats
    wobble_series = df["gyro_wobble_dps"].dropna()
    wobble_mean = wobble_series.mean() if len(wobble_series) > 0 else 0.0
    wobble_peak = wobble_series.max() if len(wobble_series) > 0 else 0.0

    # Dropped samples (gaps in sequence_num)
    seq = df["sequence_num"].values
    if len(seq) > 1:
        diffs = seq[1:] - seq[:-1]
        # Account for wraparound at 256 (uint8)
        gaps = sum(1 for d in diffs if d != 1 and d != -255)
    else:
        gaps = 0

    # Samples per rotation
    if "rotation_num" in df.columns:
        rot_counts = df.groupby("rotation_num").size()
        samples_per_rot_mean = rot_counts.mean() if len(rot_counts) > 0 else 0.0
    else:
        samples_per_rot_mean = 0.0

    # Saturation percentages
    total_rows = len(df)
    if total_rows > 0:
        if "is_x_saturated" in df.columns:
            x_sat_count = df["is_x_saturated"].sum()
            x_saturation_pct = (x_sat_count / total_rows) * 100
        else:
            x_saturation_pct = 0.0

        if "is_gz_saturated" in df.columns:
            gz_sat_count = df["is_gz_saturated"].sum()
            gz_saturation_pct = (gz_sat_count / total_rows) * 100
        else:
            gz_saturation_pct = 0.0
    else:
        x_saturation_pct = 0.0
        gz_saturation_pct = 0.0

    return StepMetrics(
        rpm_mean=float(rpm_mean),
        rpm_min=float(rpm_min),
        rpm_max=float(rpm_max),
        rpm_cv_pct=float(rpm_cv_pct),
        wobble_mean=float(wobble_mean),
        wobble_peak=float(wobble_peak),
        dropped_samples=int(gaps),
        samples_per_rot_mean=float(samples_per_rot_mean),
        x_saturation_pct=float(x_saturation_pct),
        gz_saturation_pct=float(gz_saturation_pct),
    )


def print_step_summary(
    current: StepMetrics, previous: Optional[StepMetrics] = None
) -> None:
    """Print a two-line summary of step metrics with optional delta from previous."""
    # Line 1: RPM and wobble
    console.print(
        f"[dim]RPM: {current.rpm_mean:.0f} ({current.rpm_min:.0f}-{current.rpm_max:.0f}, "
        f"CV={current.rpm_cv_pct:.1f}%) | "
        f"Wobble: {current.wobble_mean:.1f}°/s mean, {current.wobble_peak:.1f}°/s peak[/dim]"
    )

    # Line 2: Quality metrics
    console.print(
        f"[dim]Quality: {current.dropped_samples} gaps, "
        f"{current.samples_per_rot_mean:.0f} samples/rot, "
        f"X-sat={current.x_saturation_pct:.0f}%, GZ-sat={current.gz_saturation_pct:.0f}%[/dim]"
    )

    # Line 3: Delta from previous (if available)
    if previous is not None:
        rpm_delta = current.rpm_mean - previous.rpm_mean
        wobble_delta = current.wobble_mean - previous.wobble_mean
        rpm_sign = "+" if rpm_delta >= 0 else ""
        wobble_sign = "+" if wobble_delta >= 0 else ""
        console.print(
            f"[cyan]Δ from previous: {rpm_sign}{rpm_delta:.0f} RPM, "
            f"{wobble_sign}{wobble_delta:.1f}°/s wobble[/cyan]"
        )


def run_test_sequence(
    conn: DeviceConnection,
    output_dir: Path,
    settle_time: float = DEFAULT_SETTLE_TIME,
    record_time: float = DEFAULT_RECORD_TIME,
) -> TestResult:
    """Execute per-step telemetry capture.

    For each speed position (1 to MAX_SPEED_POSITION):
    1. Delete telemetry files on device
    2. Increase speed to target position
    3. Wait settle_time seconds
    4. Start recording
    5. Wait record_time seconds
    6. Stop recording
    7. Dump to speed-specific subdirectory

    Returns TestResult with all collected data.
    """
    result = TestResult(
        output_dir=output_dir,
        settle_time=settle_time,
        record_time=record_time,
        started_at=datetime.now(),
    )

    with MotorSafetyContext(conn) as safety:
        # Startup: Reset RX stats, power on, set effect
        console.print("[bold]Startup:[/bold] Initializing...")
        conn.rxreset()

        response = conn.motor_on()
        if not response.startswith("OK") and "Already running" not in response:
            result.error = f"Failed to power on motor: {response}"
            return result
        safety.mark_motor_on()
        console.print("[green]Motor ON[/green]")

        response = conn.button(CALIBRATION_EFFECT)
        if not response.startswith("OK"):
            result.error = f"Failed to set effect: {response}"
            return result
        console.print(f"[green]Effect set to {CALIBRATION_EFFECT}[/green]")

        # Get initial position
        status = conn.status()
        if "speed_position" not in status:
            result.error = "STATUS missing speed_position - firmware mismatch?"
            return result
        current_position = int(status["speed_position"])
        console.print(f"[dim]Initial position: {current_position}[/dim]")
        console.print()

        # Track previous step metrics for delta calculation
        previous_metrics: Optional[StepMetrics] = None

        try:
            for target_position in range(1, MAX_SPEED_POSITION + 1):
                console.print(
                    f"[bold cyan]═══ Speed Position {target_position}/{MAX_SPEED_POSITION} ═══[/bold cyan]"
                )

                step_result = StepResult(
                    position=target_position,
                    file_suffix="",  # Will be set after RPM is known
                )

                # 1. Delete existing telemetry files
                conn.delete()
                console.print("[dim]Cleared device telemetry[/dim]")

                # 2. Ramp to target position
                while current_position < target_position:
                    response = conn.button(ButtonCommand.SPEED_UP)
                    if not response.startswith("OK"):
                        step_result.error = f"Failed to increase speed: {response}"
                        step_result.success = False
                        result.steps.append(step_result)
                        result.error = step_result.error
                        break
                    status = conn.status()
                    current_position = int(status["speed_position"])

                if not step_result.success:
                    break

                console.print(f"[dim]At position {current_position}[/dim]")

                # 3. Settle
                console.print(f"[dim]Settling for {settle_time}s...[/dim]")
                time.sleep(settle_time)

                # Safety check: confirm before continuing
                if not Confirm.ask("Continue?", default=False):
                    console.print("[yellow]Aborted by user[/yellow]")
                    result.aborted = True
                    break

                # Get baseline sample counts before recording
                status = conn.status()
                accel_before = int(status.get("rx_accel_samples", 0))
                hall_before = int(status.get("rx_hall_packets", 0))

                # 4. Start recording
                response = conn.start()
                if not response.startswith("OK"):
                    step_result.error = f"Failed to start recording: {response}"
                    step_result.success = False
                    result.steps.append(step_result)
                    result.error = step_result.error
                    break
                console.print(f"[green]Recording for {record_time}s...[/green]")

                # 5. Wait for record duration
                time.sleep(record_time)

                # 6. Stop recording
                response = conn.stop()
                if not response.startswith("OK"):
                    console.print(
                        f"[yellow]Warning: stop returned {response}[/yellow]"
                    )

                # Get sample counts and RPM after recording
                status = conn.status()
                accel_after = int(status.get("rx_accel_samples", 0))
                hall_after = int(status.get("rx_hall_packets", 0))
                step_result.accel_samples = accel_after - accel_before
                step_result.hall_packets = hall_after - hall_before

                # Warn if no samples collected
                if step_result.accel_samples == 0:
                    console.print(
                        f"[yellow]Warning: No accel samples collected![/yellow]"
                    )
                if step_result.hall_packets == 0:
                    console.print(
                        f"[yellow]Warning: No hall packets collected![/yellow]"
                    )

                # 7. Dump files, extract RPM, save with suffix
                files = conn.dump()
                if files:
                    # Extract RPM from hall event data
                    step_result.rpm = extract_rpm_from_dump(files)

                    # Build file suffix with step and RPM
                    rpm_part = f"_{int(step_result.rpm)}rpm" if step_result.rpm else ""
                    step_result.file_suffix = f"_step_{target_position:02d}{rpm_part}"

                    # Display sample counts
                    rpm_str = f", {step_result.rpm:.0f} RPM" if step_result.rpm else ""
                    console.print(
                        f"[dim]Collected {step_result.accel_samples} samples, "
                        f"{step_result.hall_packets} hall{rpm_str}[/dim]"
                    )

                    # Save files
                    saved = save_csv_files(files, output_dir, step_result.file_suffix)
                    enriched = enrich_accel_csv(output_dir, step_result.file_suffix)
                    step_result.csv_files = saved
                    for path in saved:
                        console.print(f"[green]Saved: {path.name}[/green]")
                    if enriched:
                        console.print("[dim]Enriched accel CSV[/dim]")

                        # Compute and display step metrics
                        metrics = compute_step_metrics(
                            output_dir, step_result.file_suffix
                        )
                        if metrics:
                            step_result.metrics = metrics
                            print_step_summary(metrics, previous_metrics)
                            previous_metrics = metrics
                else:
                    console.print("[yellow]No telemetry data captured[/yellow]")
                    step_result.success = False

                result.steps.append(step_result)
                console.print()

        except KeyboardInterrupt:
            console.print("\n[yellow]Interrupted - stopping gracefully...[/yellow]")
            result.aborted = True
            # Try to stop recording if it was in progress
            try:
                conn.stop()
            except Exception:
                pass

        # Shutdown: Power OFF motor
        console.print("[bold]Shutdown:[/bold] Powering off motor...")
        response = conn.motor_off()
        if response.startswith("OK") or "Already stopped" in response:
            safety.mark_motor_off()
            console.print("[green]Motor OFF[/green]")
        else:
            console.print(f"[yellow]Warning: motor_off returned {response}[/yellow]")

    result.completed_at = datetime.now()
    return result


def _metrics_to_dict(m: Optional[StepMetrics]) -> Optional[dict[str, float | int]]:
    """Convert StepMetrics to a dict for JSON serialization."""
    if m is None:
        return None
    return {
        "rpm_mean": m.rpm_mean,
        "rpm_min": m.rpm_min,
        "rpm_max": m.rpm_max,
        "rpm_cv_pct": m.rpm_cv_pct,
        "wobble_mean": m.wobble_mean,
        "wobble_peak": m.wobble_peak,
        "dropped_samples": m.dropped_samples,
        "samples_per_rot_mean": m.samples_per_rot_mean,
        "x_saturation_pct": m.x_saturation_pct,
        "gz_saturation_pct": m.gz_saturation_pct,
    }


def write_manifest(result: TestResult) -> None:
    """Write manifest.json with run metadata."""
    manifest = {
        "settle_time": result.settle_time,
        "record_time": result.record_time,
        "speeds_captured": [s.position for s in result.steps if s.success],
        "started_at": result.started_at.isoformat() if result.started_at else None,
        "completed_at": result.completed_at.isoformat() if result.completed_at else None,
        "aborted": result.aborted,
        "error": result.error,
        "steps": [
            {
                "position": s.position,
                "accel_samples": s.accel_samples,
                "hall_packets": s.hall_packets,
                "rpm": s.rpm,
                "success": s.success,
                "metrics": _metrics_to_dict(s.metrics),
            }
            for s in result.steps
        ],
    }

    manifest_path = result.output_dir / "manifest.json"
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)
    console.print(f"[dim]Manifest written to {manifest_path.name}[/dim]")


def create_archive(result: TestResult) -> Path:
    """Create a .tgz archive of all telemetry files."""
    archive_name = f"{result.output_dir.name}.tgz"
    archive_path = result.output_dir / archive_name

    with tarfile.open(archive_path, "w:gz") as tar:
        # Add all CSV files
        for csv_file in result.output_dir.glob("*.csv"):
            tar.add(csv_file, arcname=csv_file.name)
        # Add manifest
        manifest_path = result.output_dir / "manifest.json"
        if manifest_path.exists():
            tar.add(manifest_path, arcname="manifest.json")

    console.print(f"[green]Archive created: {archive_name}[/green]")
    return archive_path


def test(
    port: str = typer.Option(DEFAULT_PORT, "--port", "-p", help="Serial port"),
    output: Optional[Path] = typer.Option(
        None, "--output", "-o", help="Output directory base"
    ),
    settle_time: float = typer.Option(
        DEFAULT_SETTLE_TIME, "--settle", "-s", help="Seconds to settle after speed change"
    ),
    record_time: float = typer.Option(
        DEFAULT_RECORD_TIME, "--record", "-r", help="Seconds to record at each speed"
    ),
    json_output: bool = typer.Option(False, "--json", help="Output results as JSON"),
) -> None:
    """Run per-step telemetry capture.

    For each speed position (1-10):
    1. Delete telemetry files on device
    2. Increase speed to target position
    3. Wait SETTLE seconds (default 3)
    4. Record for RECORD seconds (default 5)
    5. Dump to flat files with step and RPM suffix

    Output structure:
      telemetry/<timestamp>/
        MSG_ACCEL_SAMPLES_step_01_245rpm.csv
        MSG_HALL_EVENT_step_01_245rpm.csv
        MSG_ACCEL_SAMPLES_step_02_312rpm.csv
        ...
        manifest.json

    Use Ctrl+C to abort - motor will be safely powered off.
    """
    base_dir = output or TELEMETRY_DIR
    output_dir = create_timestamped_dir(base_dir)

    if not json_output:
        console.print("[bold]POV Per-Step Telemetry Capture[/bold]")
        console.print(f"Output: {output_dir}")
        console.print(f"Settle time: {settle_time}s, Record time: {record_time}s")
        console.print()

    try:
        with DeviceConnection(port=port) as conn:
            result = run_test_sequence(conn, output_dir, settle_time, record_time)

    except DeviceError as e:
        if json_output:
            print(json.dumps({"success": False, "error": str(e)}))
        else:
            err_console.print(f"[red]Device error: {e}[/red]")
        raise typer.Exit(2)

    # Write manifest and create archive
    write_manifest(result)
    archive_path = create_archive(result)

    # Output results
    if json_output:
        output_data = {
            "success": result.error is None and not result.aborted,
            "output_dir": str(result.output_dir),
            "archive": str(archive_path),
            "steps": [
                {
                    "position": s.position,
                    "file_suffix": s.file_suffix,
                    "files": [str(p) for p in s.csv_files],
                    "accel_samples": s.accel_samples,
                    "hall_packets": s.hall_packets,
                    "rpm": s.rpm,
                    "success": s.success,
                    "error": s.error,
                    "metrics": _metrics_to_dict(s.metrics),
                }
                for s in result.steps
            ],
            "aborted": result.aborted,
            "error": result.error,
            "settle_time": result.settle_time,
            "record_time": result.record_time,
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
            successful = sum(1 for s in result.steps if s.success)
            console.print(
                f"[green]Captured {successful}/{len(result.steps)} speed positions[/green]"
            )
            console.print(f"[dim]Results in: {result.output_dir}[/dim]")
            console.print(f"[dim]Archive: {archive_path}[/dim]")
