"""Serial communication with motor controller."""

import serial
from dataclasses import dataclass
from typing import Callable, Optional
from pathlib import Path

from .rotor_stats import RotorStats


# =============================================================================
# MPU-9250 Conversion Constants
#
# IMPORTANT: These values must match the firmware configuration.
# Source: led_display/src/Imu.cpp lines 100-109
#   - ACC_RANGE = MPU9250_ACC_RANGE_16G (±16g)
#   - GYR_RANGE = MPU9250_GYRO_RANGE_2000 (±2000°/s)
#
# Formula: physical_value = raw_value * (full_scale_range / 32768)
# =============================================================================
ACCEL_RANGE_G = 16.0      # Must match MPU9250_ACC_RANGE_16G in Imu.cpp
GYRO_RANGE_DPS = 2000.0   # Must match MPU9250_GYRO_RANGE_2000 in Imu.cpp

ACCEL_G_PER_LSB = ACCEL_RANGE_G / 32768.0      # 0.00048828125
GYRO_DPS_PER_LSB = GYRO_RANGE_DPS / 32768.0    # 0.06103515625

# Saturation threshold: values at ±32700 or beyond are clipped
SATURATION_THRESHOLD = 32700


DEFAULT_PORT = "/dev/cu.usbmodem2101"
DEFAULT_BAUD = 921600
DEFAULT_TIMEOUT = 2.0


class DeviceError(Exception):
    """Error communicating with device."""
    pass


@dataclass
class FileInfo:
    """Information about a telemetry file on device."""
    filename: str
    records: int
    bytes: int


@dataclass
class DumpedFile:
    """A dumped telemetry file with CSV data."""
    filename: str
    header: str
    rows: list[str]


class DeviceConnection:
    """Connection to motor controller via serial."""

    def __init__(self, port: str = DEFAULT_PORT, baud: int = DEFAULT_BAUD,
                 timeout: float = DEFAULT_TIMEOUT):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self._serial: Optional[serial.Serial] = None
        self.on_rotor_stats: Optional[Callable[[RotorStats], None]] = None

    def __enter__(self) -> "DeviceConnection":
        try:
            self._serial = serial.Serial(
                self.port,
                self.baud,
                timeout=self.timeout
            )
            # Clear any pending data
            self._serial.reset_input_buffer()
        except serial.SerialException as e:
            raise DeviceError(f"Cannot connect to {self.port}: {e}") from e
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self._serial:
            self._serial.close()
            self._serial = None

    def _send_command(self, cmd: str) -> None:
        """Send a command to the device."""
        if not self._serial:
            raise DeviceError("Not connected")
        self._serial.write(f"{cmd}\n".encode())
        self._serial.flush()

    def _read_line(self) -> str:
        """Read a single line from device."""
        if not self._serial:
            raise DeviceError("Not connected")
        line = self._serial.readline().decode().strip()
        return line

    def _read_until_ok_or_err(self) -> str:
        """Read lines until OK or ERR: response, printing debug output."""
        from rich.console import Console
        console = Console(stderr=True)

        while True:
            line = self._read_line()
            if line == "OK":
                return "OK"
            if line.startswith("ERR:"):
                return line
            if not line:
                raise DeviceError("Timeout waiting for response")
            # Try to parse as ROTOR_STATS
            parsed = RotorStats.from_line(line)
            if parsed:
                console.print(f"[dim]{parsed}[/dim]")
                if self.on_rotor_stats:
                    self.on_rotor_stats(parsed)
            else:
                # Print raw debug output from device
                console.print(f"[dim]{line}[/dim]")

    def status(self) -> dict[str, str]:
        """Get unified device status as key: value dict.

        Returns raw strings - caller converts types as needed.
        Firmware can add new fields without Python changes.
        """
        self._send_command("STATUS")
        data: dict[str, str] = {}
        for _ in range(20):  # Max lines to read
            line = self._read_line()
            if not line:
                break
            if ": " in line:
                key, value = line.split(": ", 1)
                data[key] = value
        return data

    def start_capture(self) -> str:
        """Start recording. Returns OK or error message."""
        self._send_command("START_CAPTURE")
        return self._read_until_ok_or_err()

    def start_capture_with_retry(
        self,
        max_retries: int = 5,
        retry_delay: float = 0.5,
        on_retry: callable = None,
    ) -> str:
        """Start recording with retry logic for 'Task busy' errors.

        The capture task may still be processing queued data after STOP.
        This method retries with exponential backoff.

        Args:
            max_retries: Maximum number of retry attempts
            retry_delay: Initial delay between retries (doubles each attempt)
            on_retry: Optional callback(attempt, delay, error) for logging

        Returns:
            "OK" on success, or final error message after all retries exhausted.
        """
        import time

        delay = retry_delay
        for attempt in range(max_retries + 1):
            response = self.start_capture()
            if response.startswith("OK"):
                return response
            if "Task busy" not in response:
                # Some other error - don't retry
                return response
            if attempt < max_retries:
                if on_retry:
                    on_retry(attempt + 1, delay, response)
                time.sleep(delay)
                delay *= 1.5  # Exponential backoff
        return response

    def stop_capture(self) -> str:
        """Stop recording. Returns OK or error message."""
        self._send_command("STOP_CAPTURE")
        return self._read_until_ok_or_err()

    def delete_all_captures(self, timeout: float = 10.0) -> str:
        """Delete all telemetry files. Takes ~5s for flash erase.

        Args:
            timeout: Read timeout for erase operation (default 10s).
        """
        old_timeout = self._serial.timeout
        self._serial.timeout = timeout
        try:
            self._send_command("DELETE_ALL_CAPTURES")
            return self._read_until_ok_or_err()
        finally:
            self._serial.timeout = old_timeout

    def list_captures(self) -> list[FileInfo]:
        """List telemetry files on device."""
        self._send_command("LIST_CAPTURES")
        files = []
        while True:
            line = self._read_line()
            if not line:  # Blank line = end of list
                break
            parts = line.split("\t")
            if len(parts) == 3:
                files.append(FileInfo(
                    filename=parts[0],
                    records=int(parts[1]),
                    bytes=int(parts[2])
                ))
        return files

    def motor_on(self) -> str:
        """Power on motor (idempotent). Returns OK or ERR: Already running."""
        self._send_command("MOTOR_ON")
        return self._read_until_ok_or_err()

    def motor_off(self) -> str:
        """Power off motor (idempotent). Returns OK or ERR: Already stopped."""
        self._send_command("MOTOR_OFF")
        return self._read_until_ok_or_err()

    def button(self, cmd_num: int) -> str:
        """Trigger a Command enum value (emulates IR button press).

        See motor_controller/src/commands.h for command numbers:
          1-10: Effect1-Effect10 (10 = calibration)
          11: BrightnessUp, 12: BrightnessDown
          13: PowerToggle, 14: SpeedUp, 15: SpeedDown
          16-19: Effect mode/param controls
          20-23: Capture controls
        """
        self._send_command(f"BUTTON {cmd_num}")
        return self._read_until_ok_or_err()

    def rxreset(self) -> str:
        """Reset ESP-NOW receive statistics."""
        self._send_command("RXRESET")
        return self._read_until_ok_or_err()

    def reset_rotor_stats(self) -> str:
        """Send reset command to LED display to zero rotor diagnostic stats."""
        self._send_command("RESET_ROTOR_STATS")
        return self._read_until_ok_or_err()

    def dump_captures(self) -> list[DumpedFile]:
        """Dump all telemetry files as CSV data."""
        self._send_command("DUMP_CAPTURES")
        files = []
        current_file: Optional[DumpedFile] = None

        while True:
            line = self._read_line()

            if line == ">>>":
                # End of dump
                if current_file:
                    files.append(current_file)
                break

            if line.startswith(">>> "):
                # Start of new file
                if current_file:
                    files.append(current_file)
                filename = line[4:]  # Remove ">>> " prefix
                current_file = DumpedFile(filename=filename, header="", rows=[])

            elif current_file:
                if not current_file.header:
                    current_file.header = line
                else:
                    current_file.rows.append(line)

        return files


def extract_rpm_from_dump(files: list[DumpedFile]) -> float | None:
    """Extract average RPM from hall event data in dump.

    Parses MSG_HALL_EVENT rows to get period_us values and computes RPM.
    """
    for f in files:
        if "HALL_EVENT" in f.filename:
            periods = []
            for row in f.rows:
                parts = row.split(",")
                if len(parts) >= 2:
                    try:
                        period_us = float(parts[1])
                        if period_us > 0:
                            periods.append(period_us)
                    except ValueError:
                        continue
            if periods:
                avg_period = sum(periods) / len(periods)
                return 60_000_000.0 / avg_period
    return None


def save_csv_files(
    files: list[DumpedFile], output_dir: Path, filename_suffix: str = ""
) -> list[Path]:
    """Save dumped files as CSV to output directory.

    Args:
        files: List of dumped file objects to save
        output_dir: Directory to save files to
        filename_suffix: Optional suffix before .csv (e.g., "_step_01")
    """
    output_dir.mkdir(parents=True, exist_ok=True)
    saved = []

    for f in files:
        # Convert .bin to .csv, with optional suffix before extension
        base_name = f.filename.replace(".bin", "")
        csv_name = f"{base_name}{filename_suffix}.csv"
        path = output_dir / csv_name

        with open(path, "w") as out:
            out.write(f.header + "\n")
            for row in f.rows:
                out.write(row + "\n")

        saved.append(path)

    return saved


def enrich_accel_csv(output_dir: Path, filename_suffix: str = "") -> bool:
    """Enrich accelerometer CSV with physical units and derived values.

    Computes:
    - rotation_num, micros_since_hall (from hall event timestamps)
    - angle_deg (0-360° position relative to hall sensor)
    - rpm (from hall period)
    - Physical units: x_g, y_g, z_g (g), gx_dps, gy_dps, gz_dps (°/s)
    - gyro_wobble_dps (wobble magnitude from non-saturating axes)
    - is_x_saturated, is_gz_saturated (saturation flags)

    Args:
        output_dir: Directory containing CSV files
        filename_suffix: Optional suffix before .csv (e.g., "_step_01")

    Returns True if enrichment was performed, False if files not found.
    """
    import numpy as np
    import pandas as pd

    accel_path = output_dir / f"MSG_ACCEL_SAMPLES{filename_suffix}.csv"
    hall_path = output_dir / f"MSG_HALL_EVENT{filename_suffix}.csv"

    if not accel_path.exists() or not hall_path.exists():
        return False  # Nothing to enrich

    accel = pd.read_csv(accel_path)
    hall = pd.read_csv(hall_path)

    if accel.empty or hall.empty:
        return False

    # Sort hall events by timestamp
    hall = hall.sort_values('timestamp_us').reset_index(drop=True)

    # Get timestamp arrays as numpy arrays for searchsorted
    hall_timestamps = hall['timestamp_us'].to_numpy()
    accel_timestamps = accel['timestamp_us'].to_numpy()

    # For each accel sample, find which hall interval it belongs to
    # searchsorted returns insertion point; -1 gives the hall event before each sample
    indices = np.searchsorted(hall_timestamps, accel_timestamps, side='right') - 1
    indices_clipped = np.clip(indices, 0, len(hall) - 1)

    # Assign rotation_num from the hall event that precedes each sample
    accel['rotation_num'] = hall['rotation_num'].values[indices_clipped]

    # Compute microseconds since that hall event
    accel['micros_since_hall'] = accel_timestamps - hall_timestamps[indices_clipped]

    # Handle samples before first hall event (shouldn't happen in normal operation)
    before_first = indices < 0
    accel.loc[before_first, 'rotation_num'] = 0
    accel.loc[before_first, 'micros_since_hall'] = 0

    # --- Merge period_us from hall data for angle calculation ---
    accel['period_us'] = hall['period_us'].values[indices_clipped]

    # --- Angular position (0-360°) relative to hall sensor ---
    # Assumes constant angular velocity within each rotation
    # Use modulo to handle samples that span multiple rotations (hall events are sparse)
    accel['angle_deg'] = ((accel['micros_since_hall'] / accel['period_us']) * 360.0) % 360.0
    accel.loc[before_first, 'angle_deg'] = 0.0

    # --- RPM from hall period ---
    accel['rpm'] = 60_000_000.0 / accel['period_us']

    # --- Saturation flags (only axes that can saturate) ---
    # X is radial axis (saturates from centrifugal force at ~720 RPM)
    accel['is_x_saturated'] = accel['x'].abs() >= SATURATION_THRESHOLD
    accel['is_gz_saturated'] = accel['gz'].abs() >= SATURATION_THRESHOLD

    # --- Convert raw to physical units ---
    accel['x_g'] = accel['x'] * ACCEL_G_PER_LSB
    accel['y_g'] = accel['y'] * ACCEL_G_PER_LSB
    accel['z_g'] = accel['z'] * ACCEL_G_PER_LSB
    accel['gx_dps'] = accel['gx'] * GYRO_DPS_PER_LSB
    accel['gy_dps'] = accel['gy'] * GYRO_DPS_PER_LSB
    accel['gz_dps'] = accel['gz'] * GYRO_DPS_PER_LSB

    # --- Derived values from non-saturating axes ---
    # Gyro wobble: rotation rate around non-spin axes (gx, gy)
    # This measures precession/wobble - key metric for balance analysis
    accel['gyro_wobble_dps'] = np.sqrt(accel['gx_dps']**2 + accel['gy_dps']**2)

    # --- Drop raw columns and intermediate values ---
    accel = accel.drop(columns=['x', 'y', 'z', 'gx', 'gy', 'gz', 'period_us'])

    # --- Final column order ---
    cols = [
        'timestamp_us', 'sequence_num', 'rotation_num', 'micros_since_hall',
        'angle_deg', 'rpm',
        'x_g', 'y_g', 'z_g', 'gx_dps', 'gy_dps', 'gz_dps',
        'gyro_wobble_dps',
        'is_x_saturated', 'is_gz_saturated'
    ]
    accel = accel[cols]

    # Overwrite with enriched data
    accel.to_csv(accel_path, index=False)

    return True
