"""LED geometry calibration calculations.

See docs/led_display/HARDWARE.md for measurement methodology.

Usage:
    pov calibration report       # Show current calibration
    pov calibration interactive  # Enter new measurements
"""

import math
from dataclasses import dataclass

# === CONSTANTS ===
ARM_TIP_RADIUS_MM = 104.5  # Physical arm length from rotation center to tip
LED_CHIP_SIZE_MM = 5.0     # LED chip width
LED_PITCH_MM = 7.0         # Center-to-center spacing (5mm chip + 2mm gap)
IDEAL_RING_PITCH_MM = LED_PITCH_MM / 3  # 2.333mm

LED_COUNT = {
    'ARM1': 13,
    'ARM2': 13,
    'ARM3': 14,
}

# === CALIBRATION MEASUREMENTS ===
# "Strip Position" measurement (primary): Arm tip to inner edge of innermost LED (mm)
DEFAULT_STRIP_POSITION = {
    'ARM1': 91.9,
    'ARM2': 93.9,
    'ARM3': 97.0,
}

# "Triangle" measurement (cross-validation): Distances between innermost LED centers (mm)
DEFAULT_TRIANGLE = {
    ('ARM1', 'ARM2'): 23.4,
    ('ARM1', 'ARM3'): 22.0,
    ('ARM2', 'ARM3'): 19.2,
}


@dataclass
class LED:
    """Represents a single LED position."""
    arm: str
    led_index: int
    radius: float


@dataclass
class CalibrationResult:
    """Complete calibration analysis results."""
    # Per-arm innermost LED centers
    innermost_centers: dict[str, float]

    # All 40 LEDs sorted by radius
    all_leds: list[LED]

    # Display boundaries
    inner_edge: float
    outer_edge: float

    # Interlacing analysis
    gaps: list[dict]
    min_gap: float
    max_gap: float
    avg_gap: float
    std_dev: float

    # Triangle validation (if provided)
    triangle_validation: dict | None


def compute_innermost_center(arm: str, strip_position: dict[str, float]) -> float:
    """Compute innermost LED center radius from Strip Position measurement."""
    inner_edge = ARM_TIP_RADIUS_MM - strip_position[arm]
    return inner_edge + (LED_CHIP_SIZE_MM / 2)


def compute_all_led_positions(strip_position: dict[str, float]) -> list[LED]:
    """Build sorted list of all 40 LED positions."""
    innermost = {arm: compute_innermost_center(arm, strip_position) for arm in LED_COUNT}

    all_leds = []
    for arm, count in LED_COUNT.items():
        inner_r = innermost[arm]
        for i in range(count):
            r = inner_r + i * LED_PITCH_MM
            all_leds.append(LED(arm=arm, led_index=i, radius=r))

    all_leds.sort(key=lambda x: x.radius)
    return all_leds


def triangle_distance(r1: float, r2: float) -> float:
    """Distance between two points at radii r1, r2 separated by 120 degrees."""
    return math.sqrt(r1**2 + r2**2 + r1*r2)


def analyze_interlacing(all_leds: list[LED]) -> dict:
    """Analyze the interlacing pattern and compute errors."""
    gaps = []
    for i in range(1, len(all_leds)):
        gap = all_leds[i].radius - all_leds[i-1].radius
        gaps.append({
            'from_ring': i - 1,
            'to_ring': i,
            'from_arm': all_leds[i-1].arm,
            'to_arm': all_leds[i].arm,
            'gap': gap,
            'error': gap - IDEAL_RING_PITCH_MM,
        })

    gap_values = [g['gap'] for g in gaps]
    return {
        'gaps': gaps,
        'min_gap': min(gap_values),
        'max_gap': max(gap_values),
        'avg_gap': sum(gap_values) / len(gap_values),
        'std_dev': (sum((g - IDEAL_RING_PITCH_MM)**2 for g in gap_values) / len(gap_values))**0.5,
    }


def compute_triangle_validation(
    innermost: dict[str, float],
    triangle: dict[tuple[str, str], float]
) -> dict:
    """Validate computed radii against triangle measurements."""
    results = {}
    for (a1, a2), measured in triangle.items():
        computed = triangle_distance(innermost[a1], innermost[a2])
        results[(a1, a2)] = {
            'measured': measured,
            'computed': computed,
            'error': computed - measured,
        }
    return results


def calibrate(
    strip_position: dict[str, float] | None = None,
    triangle: dict[tuple[str, str], float] | None = None,
) -> CalibrationResult:
    """Run full calibration analysis."""
    if strip_position is None:
        strip_position = DEFAULT_STRIP_POSITION

    innermost = {arm: compute_innermost_center(arm, strip_position) for arm in LED_COUNT}
    all_leds = compute_all_led_positions(strip_position)
    analysis = analyze_interlacing(all_leds)

    inner_edge = all_leds[0].radius - LED_CHIP_SIZE_MM / 2
    outer_edge = all_leds[-1].radius + LED_CHIP_SIZE_MM / 2

    triangle_validation = None
    if triangle:
        triangle_validation = compute_triangle_validation(innermost, triangle)

    return CalibrationResult(
        innermost_centers=innermost,
        all_leds=all_leds,
        inner_edge=inner_edge,
        outer_edge=outer_edge,
        gaps=analysis['gaps'],
        min_gap=analysis['min_gap'],
        max_gap=analysis['max_gap'],
        avg_gap=analysis['avg_gap'],
        std_dev=analysis['std_dev'],
        triangle_validation=triangle_validation,
    )


def compute_adjustments(innermost: dict[str, float]) -> dict:
    """Compute recommended strip adjustments."""
    arm3_inner = innermost['ARM3']
    arm2_offset = innermost['ARM2'] - arm3_inner
    arm1_offset = innermost['ARM1'] - arm3_inner

    # Best assignment: ARM2 at +2.33mm, ARM1 at +4.67mm from ARM3
    arm2_adj = IDEAL_RING_PITCH_MM - arm2_offset
    arm1_adj = 2 * IDEAL_RING_PITCH_MM - arm1_offset
    arm3_adj = -arm2_adj  # Equivalent to moving ARM3 instead

    return {
        'arm3_only': arm3_adj,
        'arm2': arm2_adj,
        'arm1': arm1_adj,
    }
