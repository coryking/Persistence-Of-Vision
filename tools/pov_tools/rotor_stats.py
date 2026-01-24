"""RotorStats dataclass for parsing ROTOR_STATS serial output."""

import re
from dataclasses import dataclass
from typing import Optional


@dataclass
class RotorStats:
    """Parsed ROTOR_STATS message from LED display.

    Format matches motor_controller/src/espnow_comm.cpp:61:
    ROTOR_STATS seq=%lu created=%llu updated=%llu hall=%lu outliers=%lu
                last_outlier_us=%lu hall_avg_us=%lu rpm=%lu espnow_ok=%lu espnow_fail=%lu
                render=%u skip=%u not_rot=%u effect=%u brightness=%u
    """

    seq: int
    created: int
    updated: int
    hall: int
    outliers: int
    last_outlier_us: int
    hall_avg_us: int
    rpm: int
    espnow_ok: int
    espnow_fail: int
    render: int
    skip: int
    not_rot: int
    effect: int
    brightness: int

    # Pattern for key=value pairs (handles both %lu and %u formats)
    _KV_PATTERN = re.compile(r"(\w+)=(\d+)")

    @classmethod
    def from_line(cls, line: str) -> Optional["RotorStats"]:
        """Parse a ROTOR_STATS line. Returns None if not a ROTOR_STATS line."""
        if not line.startswith("ROTOR_STATS "):
            return None

        # Extract all key=value pairs
        matches = cls._KV_PATTERN.findall(line)
        if not matches:
            return None

        data = {k: int(v) for k, v in matches}

        # Map firmware field names to dataclass fields
        try:
            return cls(
                seq=data["seq"],
                created=data["created"],
                updated=data["updated"],
                hall=data["hall"],
                outliers=data["outliers"],
                last_outlier_us=data["last_outlier_us"],
                hall_avg_us=data["hall_avg_us"],
                rpm=data["rpm"],
                espnow_ok=data["espnow_ok"],
                espnow_fail=data["espnow_fail"],
                render=data["render"],
                skip=data["skip"],
                not_rot=data["not_rot"],
                effect=data["effect"],
                brightness=data["brightness"],
            )
        except KeyError:
            # Missing required field - incomplete line
            return None

    def __str__(self) -> str:
        """Format for display (single line, key info)."""
        return (
            f"ROTOR_STATS seq={self.seq} rpm={self.rpm} effect={self.effect} "
            f"brightness={self.brightness} espnow={self.espnow_ok}/{self.espnow_ok + self.espnow_fail}"
        )
