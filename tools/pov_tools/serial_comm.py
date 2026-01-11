"""Serial communication with motor controller."""

import serial
from dataclasses import dataclass
from typing import Optional
from pathlib import Path


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
class RxStats:
    """ESP-NOW receive statistics."""
    hall_packets: int
    accel_packets: int
    accel_samples: int
    last_accel_len: int


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
        """Read lines until OK or ERR: response."""
        while True:
            line = self._read_line()
            if line == "OK":
                return "OK"
            if line.startswith("ERR:"):
                return line
            if not line:
                raise DeviceError("Timeout waiting for response")

    def status(self) -> str:
        """Get capture state: IDLE, RECORDING, or FULL."""
        self._send_command("STATUS")
        line = self._read_line()
        if line in ("IDLE", "RECORDING", "FULL"):
            return line
        raise DeviceError(f"Unexpected status response: {line}")

    def start(self) -> str:
        """Start recording. Returns OK or error message."""
        self._send_command("START")
        return self._read_until_ok_or_err()

    def stop(self) -> str:
        """Stop recording. Returns OK or error message."""
        self._send_command("STOP")
        return self._read_until_ok_or_err()

    def delete(self) -> str:
        """Delete all telemetry files. Returns OK."""
        self._send_command("DELETE")
        return self._read_until_ok_or_err()

    def list_files(self) -> list[FileInfo]:
        """List telemetry files on device."""
        self._send_command("LIST")
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

    def rxstats(self) -> RxStats:
        """Get ESP-NOW receive statistics."""
        self._send_command("RXSTATS")
        line = self._read_line()
        # Parse: [ESPNOW] RX stats: hall=N, accel_pkts=N, accel_samples=N, last_len=N
        import re
        match = re.search(
            r'hall=(\d+),\s*accel_pkts=(\d+),\s*accel_samples=(\d+),\s*last_len=(\d+)',
            line
        )
        if not match:
            raise DeviceError(f"Unexpected RXSTATS response: {line}")
        return RxStats(
            hall_packets=int(match.group(1)),
            accel_packets=int(match.group(2)),
            accel_samples=int(match.group(3)),
            last_accel_len=int(match.group(4))
        )

    def rxreset(self) -> str:
        """Reset ESP-NOW receive statistics."""
        self._send_command("RXRESET")
        return self._read_until_ok_or_err()

    def dump(self) -> list[DumpedFile]:
        """Dump all telemetry files as CSV data."""
        self._send_command("DUMP")
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


def save_csv_files(files: list[DumpedFile], output_dir: Path) -> list[Path]:
    """Save dumped files as CSV to output directory."""
    output_dir.mkdir(parents=True, exist_ok=True)
    saved = []

    for f in files:
        # Convert .bin to .csv
        csv_name = f.filename.replace(".bin", ".csv")
        path = output_dir / csv_name

        with open(path, "w") as out:
            out.write(f.header + "\n")
            for row in f.rows:
                out.write(row + "\n")

        saved.append(path)

    return saved


def enrich_accel_csv(output_dir: Path) -> bool:
    """Add rotation_num and micros_since_hall computed from hall event timestamps.

    These columns were previously computed in firmware but are now computed in
    post-processing to decouple telemetry from the LED render loop.

    Returns True if enrichment was performed, False if files not found.
    """
    import numpy as np
    import pandas as pd

    accel_path = output_dir / "MSG_ACCEL_SAMPLES.csv"
    hall_path = output_dir / "MSG_HALL_EVENT.csv"

    if not accel_path.exists() or not hall_path.exists():
        return False  # Nothing to enrich

    accel = pd.read_csv(accel_path)
    hall = pd.read_csv(hall_path)

    if accel.empty or hall.empty:
        return False

    # Sort hall events by timestamp
    hall = hall.sort_values('timestamp_us').reset_index(drop=True)

    # Get timestamp arrays
    hall_timestamps = hall['timestamp_us'].values
    accel_timestamps = accel['timestamp_us'].values

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

    # Reorder columns to match original format expected by analysis tools
    cols = ['timestamp_us', 'sequence_num', 'rotation_num', 'micros_since_hall', 'x', 'y', 'z']
    accel = accel[cols]

    # Overwrite with enriched data
    accel.to_csv(accel_path, index=False)

    return True
