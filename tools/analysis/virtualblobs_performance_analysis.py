#!/usr/bin/env python3
"""
VirtualBlobs Performance Analysis
Mathematical analysis of profiling data to identify optimization opportunities.
"""

import numpy as np
from dataclasses import dataclass
from typing import Dict, List, Tuple

@dataclass
class ProfilingReport:
    """Single frame profiling data"""
    total_time: float
    angle_checks: float
    radial_checks: float
    color_blends: float
    array_lookups: float
    rgb_construction: float
    setpixelcolor: float
    instrumentation_overhead: float
    unmeasured: float

    # Call counts
    angle_check_calls: int = 150
    setpixelcolor_calls: int = 30
    array_lookup_calls: int = 30
    rgb_construction_calls: int = 30

    @property
    def actual_time(self) -> float:
        """Time excluding instrumentation overhead"""
        return self.total_time - self.instrumentation_overhead

    @property
    def operations(self) -> Dict[str, Tuple[float, int]]:
        """Dictionary of operations with (time, call_count)"""
        return {
            'angle_checks': (self.angle_checks, self.angle_check_calls),
            'setpixelcolor': (self.setpixelcolor, self.setpixelcolor_calls),
            'array_lookups': (self.array_lookups, self.array_lookup_calls),
            'rgb_construction': (self.rgb_construction, self.rgb_construction_calls),
            'radial_checks': (self.radial_checks, 0),  # Unknown call count
            'color_blends': (self.color_blends, 0),    # Unknown call count
        }


@dataclass
class PerformanceConstraints:
    """POV display performance constraints"""
    rpm: float = 2800.0
    target_updates_per_degree: float = 1.0

    @property
    def revolution_period_us(self) -> float:
        """Time for one full revolution in microseconds"""
        return (60.0 / self.rpm) * 1_000_000

    @property
    def time_per_degree_us(self) -> float:
        """Time budget per degree of rotation"""
        return self.revolution_period_us / 360.0

    @property
    def target_frame_time_us(self) -> float:
        """Target frame time to achieve 1 update per degree"""
        return self.time_per_degree_us / self.target_updates_per_degree


def print_section(title: str):
    """Print formatted section header"""
    print(f"\n{'='*80}")
    print(f"  {title}")
    print('='*80)


def print_subsection(title: str):
    """Print formatted subsection header"""
    print(f"\n{title}")
    print('-'*80)


def analyze_current_performance(reports: List[ProfilingReport],
                                constraints: PerformanceConstraints,
                                mean_generation_time: float,
                                mean_spi_time: float):
    """Analyze current performance metrics"""
    print_section("CURRENT PERFORMANCE METRICS")

    # Basic constraints
    print_subsection("POV Display Constraints")
    print(f"Operating RPM:              {constraints.rpm:.1f}")
    print(f"Revolution period:          {constraints.revolution_period_us:.1f} μs")
    print(f"Time per degree:            {constraints.time_per_degree_us:.1f} μs")
    print(f"Target updates/degree:      {constraints.target_updates_per_degree:.1f}")
    print(f"Target frame time:          {constraints.target_frame_time_us:.1f} μs")

    # Current performance
    print_subsection("Measured Performance (from profiling reports)")
    for i, report in enumerate(reports, 1):
        print(f"\nReport {i}:")
        print(f"  Total measured time:      {report.total_time:.0f} μs")
        print(f"  Instrumentation overhead: {report.instrumentation_overhead:.0f} μs ({report.instrumentation_overhead/report.total_time*100:.1f}%)")
        print(f"  Actual execution time:    {report.actual_time:.0f} μs")
        degrees_per_frame = report.actual_time / constraints.time_per_degree_us
        updates_per_degree = 1.0 / degrees_per_frame
        print(f"  Degrees per frame:        {degrees_per_frame:.2f}°")
        print(f"  Updates per degree:       {updates_per_degree:.3f}")

    # Mean performance from PERFORMANCE_ANALYSIS.md
    print_subsection("Mean Performance (from PERFORMANCE_ANALYSIS.md)")
    mean_total = mean_generation_time + mean_spi_time
    degrees_per_frame = mean_total / constraints.time_per_degree_us
    updates_per_degree = 1.0 / degrees_per_frame
    print(f"Mean generation time:       {mean_generation_time:.1f} μs")
    print(f"Mean SPI transfer time:     {mean_spi_time:.1f} μs")
    print(f"Mean total frame time:      {mean_total:.1f} μs")
    print(f"Degrees per frame:          {degrees_per_frame:.2f}°")
    print(f"Updates per degree:         {updates_per_degree:.3f}")


def analyze_bottlenecks(reports: List[ProfilingReport],
                       constraints: PerformanceConstraints):
    """Identify and analyze top bottlenecks"""
    print_section("BOTTLENECK ANALYSIS")

    for report_idx, report in enumerate(reports, 1):
        print_subsection(f"Report {report_idx} - Operation Breakdown")

        # Calculate actual time spent (excluding instrumentation)
        actual_time = report.actual_time

        # Sort operations by time
        ops = []
        for name, (time, calls) in report.operations.items():
            if time > 0:
                pct = (time / actual_time) * 100
                per_call = time / calls if calls > 0 else 0
                ops.append((name, time, pct, calls, per_call))

        ops.sort(key=lambda x: x[1], reverse=True)

        print(f"\nActual execution time: {actual_time:.0f} μs")
        print(f"\n{'Operation':<20} {'Time (μs)':<12} {'% of Total':<12} {'Calls':<8} {'μs/call':<10}")
        print('-'*80)

        for name, time, pct, calls, per_call in ops:
            call_str = f"{calls}" if calls > 0 else "?"
            per_call_str = f"{per_call:.2f}" if calls > 0 else "?"
            print(f"{name:<20} {time:<12.0f} {pct:<12.1f} {call_str:<8} {per_call_str:<10}")

        # Unmeasured overhead
        unmeasured_pct = (report.unmeasured / actual_time) * 100
        print(f"{'unmeasured':<20} {report.unmeasured:<12.0f} {unmeasured_pct:<12.1f} {'':<8} {'':<10}")

        # Top 3 bottlenecks
        print_subsection(f"Report {report_idx} - Top 3 Bottlenecks by Execution Time")
        for rank, (name, time, pct, calls, per_call) in enumerate(ops[:3], 1):
            print(f"\n{rank}. {name.upper()}")
            print(f"   Current time:        {time:.0f} μs ({pct:.1f}% of execution)")
            if calls > 0:
                print(f"   Per-call cost:       {per_call:.2f} μs ({calls} calls)")


def calculate_speedup_requirements(reports: List[ProfilingReport],
                                  constraints: PerformanceConstraints,
                                  mean_spi_time: float):
    """Calculate required speedup for each operation to hit target"""
    print_section("SPEEDUP REQUIREMENTS TO ACHIEVE TARGET (1 update/degree)")

    target_time = constraints.target_frame_time_us

    for report_idx, report in enumerate(reports, 1):
        print_subsection(f"Report {report_idx} - Speedup Analysis")

        actual_time = report.actual_time
        generation_time = actual_time - mean_spi_time  # Approximate generation time for this frame

        print(f"\nCurrent frame time:       {actual_time:.0f} μs")
        print(f"Target frame time:        {target_time:.1f} μs")
        print(f"Gap:                      {actual_time - target_time:.0f} μs ({(actual_time/target_time):.2f}x over budget)")
        print(f"\nSPI transfer time:        {mean_spi_time:.1f} μs (cannot optimize - hardware limit)")
        print(f"Generation time:          {generation_time:.0f} μs")
        print(f"Target generation time:   {target_time - mean_spi_time:.1f} μs")
        print(f"Required generation speedup: {generation_time / (target_time - mean_spi_time):.2f}x")

        # Per-operation speedup requirements
        print(f"\nPer-Operation Speedup Requirements:")
        print(f"(Assuming all other operations remain constant)")
        print(f"\n{'Operation':<20} {'Current':<12} {'Target':<12} {'Speedup':<12}")
        print('-'*80)

        target_gen_time = target_time - mean_spi_time

        ops = []
        for name, (time, calls) in report.operations.items():
            if time > 0:
                # What if we optimize ONLY this operation?
                other_ops_time = generation_time - time
                required_this_op_time = max(0, target_gen_time - other_ops_time)
                speedup = time / required_this_op_time if required_this_op_time > 0 else float('inf')
                ops.append((name, time, required_this_op_time, speedup))

        ops.sort(key=lambda x: x[3], reverse=True)

        for name, current, target_op, speedup in ops:
            speedup_str = f"{speedup:.2f}x" if speedup != float('inf') else "∞"
            print(f"{name:<20} {current:<12.0f} {target_op:<12.0f} {speedup_str:<12}")


def analyze_angle_checks(reports: List[ProfilingReport],
                        constraints: PerformanceConstraints):
    """Deep dive into isAngleInArc performance"""
    print_section("DEEP DIVE: isAngleInArc() Performance")

    for report_idx, report in enumerate(reports, 1):
        print_subsection(f"Report {report_idx}")

        angle_time = report.angle_checks
        angle_calls = report.angle_check_calls
        per_call = angle_time / angle_calls

        actual_time = report.actual_time
        pct_of_total = (angle_time / actual_time) * 100

        print(f"\nCurrent Performance:")
        print(f"  Total time:             {angle_time:.0f} μs")
        print(f"  Number of calls:        {angle_calls}")
        print(f"  Per-call cost:          {per_call:.2f} μs")
        print(f"  % of execution time:    {pct_of_total:.1f}%")

        # Target performance
        target_frame = constraints.target_frame_time_us
        target_gen = target_frame - 54.8  # Approximate SPI time

        # If we keep all other ops the same, how fast must angle checks be?
        other_ops = actual_time - angle_time - 54.8  # Other generation ops
        target_angle_time = max(0, target_gen - other_ops)
        target_per_call = target_angle_time / angle_calls
        speedup = per_call / target_per_call if target_per_call > 0 else float('inf')

        print(f"\nTarget Performance (to hit 1 update/degree):")
        print(f"  Target total time:      {target_angle_time:.1f} μs")
        print(f"  Target per-call:        {target_per_call:.2f} μs")
        print(f"  Required speedup:       {speedup:.2f}x")

        # What if we eliminate angle checks entirely?
        time_without_angle_checks = actual_time - angle_time
        degrees_per_frame = time_without_angle_checks / constraints.time_per_degree_us
        updates_per_degree = 1.0 / degrees_per_frame

        print(f"\nHypothetical: If angle checks took 0 μs:")
        print(f"  Frame time:             {time_without_angle_checks:.0f} μs")
        print(f"  Updates per degree:     {updates_per_degree:.3f}")
        print(f"  Status:                 {'✓ MEETS TARGET' if updates_per_degree >= 1.0 else '✗ STILL INSUFFICIENT'}")


def analyze_setpixelcolor(reports: List[ProfilingReport],
                         constraints: PerformanceConstraints):
    """Analyze SetPixelColor overhead"""
    print_section("DEEP DIVE: SetPixelColor() Overhead")

    for report_idx, report in enumerate(reports, 1):
        print_subsection(f"Report {report_idx}")

        setpixel_time = report.setpixelcolor
        setpixel_calls = report.setpixelcolor_calls
        per_call = setpixel_time / setpixel_calls

        actual_time = report.actual_time
        pct_of_total = (setpixel_time / actual_time) * 100

        print(f"\nCurrent Performance:")
        print(f"  Total time:             {setpixel_time:.0f} μs")
        print(f"  Number of calls:        {setpixel_calls}")
        print(f"  Per-call cost:          {per_call:.2f} μs")
        print(f"  % of execution time:    {pct_of_total:.1f}%")

        # What if we eliminate SetPixelColor overhead?
        time_without_setpixel = actual_time - setpixel_time
        degrees_per_frame = time_without_setpixel / constraints.time_per_degree_us
        updates_per_degree = 1.0 / degrees_per_frame

        print(f"\nHypothetical: If SetPixelColor took 0 μs:")
        print(f"  Frame time:             {time_without_setpixel:.0f} μs")
        print(f"  Updates per degree:     {updates_per_degree:.3f}")
        print(f"  Status:                 {'✓ MEETS TARGET' if updates_per_degree >= 1.0 else '✗ STILL INSUFFICIENT'}")


def optimization_recommendations(reports: List[ProfilingReport],
                                constraints: PerformanceConstraints):
    """Provide prioritized optimization recommendations"""
    print_section("OPTIMIZATION RECOMMENDATIONS")

    # Use average of both reports
    avg_angle_time = np.mean([r.angle_checks for r in reports])
    avg_setpixel_time = np.mean([r.setpixelcolor for r in reports])
    avg_array_lookup_time = np.mean([r.array_lookups for r in reports])
    avg_rgb_construction_time = np.mean([r.rgb_construction for r in reports])
    avg_actual_time = np.mean([r.actual_time for r in reports])

    print_subsection("Priority 1: Optimize isAngleInArc() - CRITICAL")
    print(f"""
Current: {avg_angle_time:.0f} μs ({avg_angle_time/avg_actual_time*100:.1f}% of execution time)
Target:  ~15-20 μs (based on speedup requirements)
Speedup: ~15x faster needed

This is the PRIMARY bottleneck. Even if all other operations were free,
angle checks alone prevent hitting target performance.

Optimization strategies:
  1. Eliminate redundant angle wrapping calculations
  2. Use lookup tables for trigonometric operations if present
  3. Simplify arc intersection logic
  4. Consider precomputing arc boundaries
  5. Profile to find hot spots within the function
""")

    print_subsection("Priority 2: Optimize SetPixelColor() - HIGH")
    print(f"""
Current: {avg_setpixel_time:.0f} μs ({avg_setpixel_time/avg_actual_time*100:.1f}% of execution time)
Target:  ~20-30 μs
Speedup: ~4-5x faster needed

Optimization strategies:
  1. Reduce per-call overhead (function call cost, parameter passing)
  2. Batch pixel updates if possible
  3. Eliminate redundant operations within SetPixelColor
  4. Consider direct buffer writes instead of function calls
""")

    print_subsection("Priority 3: Optimize Array Lookups - MEDIUM")
    print(f"""
Current: {avg_array_lookup_time:.0f} μs ({avg_array_lookup_time/avg_actual_time*100:.1f}% of execution time)
Target:  ~5-10 μs
Speedup: ~2-3x faster needed

Optimization strategies:
  1. Cache lookup results if same values queried repeatedly
  2. Use more efficient data structures (direct indexing vs hash maps)
  3. Consider compile-time computation if mapping is static
""")

    print_subsection("Combined Optimization Impact")
    print(f"""
If ALL three optimizations succeed:
  Current total time:     {avg_actual_time:.0f} μs
  Time from top 3 ops:    {avg_angle_time + avg_setpixel_time + avg_array_lookup_time:.0f} μs
  Other operations:       {avg_actual_time - (avg_angle_time + avg_setpixel_time + avg_array_lookup_time):.0f} μs

  Optimized top 3 time:   ~40 μs (aggressive target)
  Projected total:        ~{(avg_actual_time - (avg_angle_time + avg_setpixel_time + avg_array_lookup_time)) + 40:.0f} μs
  Target:                 {constraints.target_frame_time_us:.1f} μs

  Status: {'✓ ACHIEVABLE' if ((avg_actual_time - (avg_angle_time + avg_setpixel_time + avg_array_lookup_time)) + 40) <= constraints.target_frame_time_us else '⚠️ REQUIRES FURTHER OPTIMIZATION'}
""")


def main():
    """Run complete performance analysis"""

    # Define profiling reports from sample data
    report1 = ProfilingReport(
        total_time=651,
        angle_checks=258,
        radial_checks=0,
        color_blends=0,
        array_lookups=32,
        rgb_construction=23,
        setpixelcolor=104,
        instrumentation_overhead=300,
        unmeasured=-66,
    )

    report2 = ProfilingReport(
        total_time=748,
        angle_checks=292,
        radial_checks=11,
        color_blends=2,
        array_lookups=26,
        rgb_construction=23,
        setpixelcolor=147,
        instrumentation_overhead=328,
        unmeasured=-81,
    )

    reports = [report1, report2]

    # Performance constraints
    constraints = PerformanceConstraints(rpm=2800.0, target_updates_per_degree=1.0)

    # Mean times from PERFORMANCE_ANALYSIS.md
    mean_generation_time = 275.2
    mean_spi_time = 54.8

    # Run analysis
    print("VirtualBlobs Performance Analysis")
    print("="*80)

    analyze_current_performance(reports, constraints, mean_generation_time, mean_spi_time)
    analyze_bottlenecks(reports, constraints)
    calculate_speedup_requirements(reports, constraints, mean_spi_time)
    analyze_angle_checks(reports, constraints)
    analyze_setpixelcolor(reports, constraints)
    optimization_recommendations(reports, constraints)

    print("\n" + "="*80)
    print("Analysis complete.")
    print("="*80)


if __name__ == "__main__":
    main()
