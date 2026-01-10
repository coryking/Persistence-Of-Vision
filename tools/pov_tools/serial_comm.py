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
