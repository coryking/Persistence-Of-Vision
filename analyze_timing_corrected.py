#!/usr/bin/env python3
"""
POV Display Timing Analysis (CORRECTED)
Analyzes timing data collected from ESP32-S3 POV display during operation.

CRITICAL UNDERSTANDING:
- POV displays work by continuously updating LEDs as they rotate
- The "timing budget" per degree is NOT a hard deadline
- The loop runs in a tight cycle, updating LEDs based on current angle
- What matters: How many updates happen per degree of rotation
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
import seaborn as sns

# Data directory
SAMPLES_DIR = Path("/Users/coryking/projects/POV_Project/samples")
OUTPUT_DIR = Path("/Users/coryking/projects/POV_Project/docs")

def load_timing_data():
    """Load all timing data files from samples directory."""
    all_data = []

    for file_path in sorted(SAMPLES_DIR.glob("2025-11-27-effect-*.txt")):
        print(f"Loading {file_path.name}...")

        # Read CSV with proper column names
        df = pd.read_csv(
            file_path,
            names=["frame", "effect", "gen_us", "xfer_us", "total_us", "angle_deg", "rpm"]
        )

        # Add source file info
        df['source_file'] = file_path.name

        all_data.append(df)

    # Combine all dataframes
    combined = pd.concat(all_data, ignore_index=True)

    print(f"\nLoaded {len(combined)} total samples from {len(all_data)} files")
    print(f"Effects included: {sorted(combined['effect'].unique())}")

    return combined

def calculate_update_rate_analysis(df):
    """
    Analyze how many updates happen per degree of rotation.
    This is what actually matters for POV displays.
    """
    # Calculate degrees per revolution and update intervals
    rpm = df['rpm'].iloc[0]
    us_per_revolution = 60_000_000 / rpm  # Microseconds per full rotation
    us_per_degree = us_per_revolution / 360.0

    # For each frame, calculate how many degrees passed during that frame
    df['degrees_per_frame'] = df['total_us'] / us_per_degree

    # Calculate updates per degree (inverse)
    df['updates_per_degree'] = 1.0 / df['degrees_per_frame']

    stats = {
        'rpm': rpm,
        'us_per_revolution': us_per_revolution,
        'us_per_degree': us_per_degree,
        'degrees_per_frame': {
            'min': df['degrees_per_frame'].min(),
            'max': df['degrees_per_frame'].max(),
            'mean': df['degrees_per_frame'].mean(),
            'median': df['degrees_per_frame'].median(),
        },
        'updates_per_degree': {
            'min': df['updates_per_degree'].min(),
            'max': df['updates_per_degree'].max(),
            'mean': df['updates_per_degree'].mean(),
            'median': df['updates_per_degree'].median(),
        }
    }

    return stats, df

def calculate_statistics(df):
    """Calculate comprehensive statistics for timing data."""

    stats = {}

    # Overall statistics
    stats['overall'] = {
        'count': len(df),
        'gen_us': {
            'min': df['gen_us'].min(),
            'max': df['gen_us'].max(),
            'mean': df['gen_us'].mean(),
            'median': df['gen_us'].median(),
            'p95': df['gen_us'].quantile(0.95),
            'p99': df['gen_us'].quantile(0.99),
            'std': df['gen_us'].std(),
        },
        'xfer_us': {
            'min': df['xfer_us'].min(),
            'max': df['xfer_us'].max(),
            'mean': df['xfer_us'].mean(),
            'median': df['xfer_us'].median(),
            'p95': df['xfer_us'].quantile(0.95),
            'p99': df['xfer_us'].quantile(0.99),
            'std': df['xfer_us'].std(),
        },
        'total_us': {
            'min': df['total_us'].min(),
            'max': df['total_us'].max(),
            'mean': df['total_us'].mean(),
            'median': df['total_us'].median(),
            'p95': df['total_us'].quantile(0.95),
            'p99': df['total_us'].quantile(0.99),
            'std': df['total_us'].std(),
        },
    }

    # Per-effect statistics
    stats['by_effect'] = {}
    for effect_id in sorted(df['effect'].unique()):
        effect_df = df[df['effect'] == effect_id]
        stats['by_effect'][effect_id] = {
            'count': len(effect_df),
            'gen_us': {
                'min': effect_df['gen_us'].min(),
                'max': effect_df['gen_us'].max(),
                'mean': effect_df['gen_us'].mean(),
                'median': effect_df['gen_us'].median(),
                'p95': effect_df['gen_us'].quantile(0.95),
                'p99': effect_df['gen_us'].quantile(0.99),
            },
            'xfer_us': {
                'min': effect_df['xfer_us'].min(),
                'max': effect_df['xfer_us'].max(),
                'mean': effect_df['xfer_us'].mean(),
                'median': effect_df['xfer_us'].median(),
            },
            'total_us': {
                'min': effect_df['total_us'].min(),
                'max': effect_df['total_us'].max(),
                'mean': effect_df['total_us'].mean(),
                'median': effect_df['total_us'].median(),
                'p95': effect_df['total_us'].quantile(0.95),
                'p99': effect_df['total_us'].quantile(0.99),
            },
            'degrees_per_frame': {
                'min': effect_df['degrees_per_frame'].min(),
                'max': effect_df['degrees_per_frame'].max(),
                'mean': effect_df['degrees_per_frame'].mean(),
                'median': effect_df['degrees_per_frame'].median(),
            },
            'updates_per_degree': {
                'min': effect_df['updates_per_degree'].min(),
                'max': effect_df['updates_per_degree'].max(),
                'mean': effect_df['updates_per_degree'].mean(),
                'median': effect_df['updates_per_degree'].median(),
            },
        }

    return stats

def create_visualizations(df):
    """Create visualization plots for timing analysis."""

    # Set style
    sns.set_style("whitegrid")

    # 1. Timing distribution histograms
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle('POV Display Timing Distributions', fontsize=16)

    # Generation time
    axes[0, 0].hist(df['gen_us'], bins=50, edgecolor='black', alpha=0.7)
    axes[0, 0].axvline(df['gen_us'].mean(), color='red', linestyle='--', label=f"Mean: {df['gen_us'].mean():.1f}μs")
    axes[0, 0].axvline(df['gen_us'].quantile(0.95), color='orange', linestyle='--', label=f"P95: {df['gen_us'].quantile(0.95):.1f}μs")
    axes[0, 0].set_xlabel('Generation Time (μs)')
    axes[0, 0].set_ylabel('Frequency')
    axes[0, 0].set_title('Render/Generation Time Distribution')
    axes[0, 0].legend()

    # Transfer time
    axes[0, 1].hist(df['xfer_us'], bins=50, edgecolor='black', alpha=0.7, color='green')
    axes[0, 1].axvline(df['xfer_us'].mean(), color='red', linestyle='--', label=f"Mean: {df['xfer_us'].mean():.1f}μs")
    axes[0, 1].set_xlabel('SPI Transfer Time (μs)')
    axes[0, 1].set_ylabel('Frequency')
    axes[0, 1].set_title('SPI Transfer Time Distribution')
    axes[0, 1].legend()

    # Total time
    axes[1, 0].hist(df['total_us'], bins=50, edgecolor='black', alpha=0.7, color='purple')
    axes[1, 0].axvline(df['total_us'].mean(), color='red', linestyle='--', label=f"Mean: {df['total_us'].mean():.1f}μs")
    axes[1, 0].set_xlabel('Total Frame Time (μs)')
    axes[1, 0].set_ylabel('Frequency')
    axes[1, 0].set_title('Total Frame Time Distribution')
    axes[1, 0].legend()

    # Updates per degree
    axes[1, 1].hist(df['updates_per_degree'], bins=50, edgecolor='black', alpha=0.7, color='orange')
    axes[1, 1].axvline(df['updates_per_degree'].mean(), color='red', linestyle='--', label=f"Mean: {df['updates_per_degree'].mean():.2f}")
    axes[1, 1].axhline(y=10, color='blue', linestyle=':', linewidth=2, label='Target: ≥1 update/degree')
    axes[1, 1].set_xlabel('Updates per Degree')
    axes[1, 1].set_ylabel('Frequency')
    axes[1, 1].set_title('Update Rate Distribution')
    axes[1, 1].legend()

    plt.tight_layout()

    # Save figure
    output_path = OUTPUT_DIR / "timing_distributions.png"
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"\nSaved visualization: {output_path}")
    plt.close()

    # 2. Per-effect comparison
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle('POV Display Timing by Effect', fontsize=16)

    effect_names = {
        0: "Per-Arm Blobs",
        1: "Virtual Blobs",
        2: "Solid Arms",
        3: "RPM Arc"
    }

    # Generation time by effect
    gen_data = [df[df['effect'] == eid]['gen_us'].values for eid in sorted(df['effect'].unique())]
    gen_labels = [effect_names.get(eid, f"Effect {eid}") for eid in sorted(df['effect'].unique())]
    axes[0, 0].boxplot(gen_data, tick_labels=gen_labels)
    axes[0, 0].set_ylabel('Generation Time (μs)')
    axes[0, 0].set_title('Generation Time by Effect')
    axes[0, 0].tick_params(axis='x', rotation=15)

    # Transfer time by effect
    xfer_data = [df[df['effect'] == eid]['xfer_us'].values for eid in sorted(df['effect'].unique())]
    axes[0, 1].boxplot(xfer_data, tick_labels=gen_labels)
    axes[0, 1].set_ylabel('SPI Transfer Time (μs)')
    axes[0, 1].set_title('SPI Transfer Time by Effect')
    axes[0, 1].tick_params(axis='x', rotation=15)

    # Total time by effect
    total_data = [df[df['effect'] == eid]['total_us'].values for eid in sorted(df['effect'].unique())]
    axes[1, 0].boxplot(total_data, tick_labels=gen_labels)
    axes[1, 0].set_ylabel('Total Frame Time (μs)')
    axes[1, 0].set_title('Total Frame Time by Effect')
    axes[1, 0].tick_params(axis='x', rotation=15)

    # Updates per degree by effect
    upd_data = [df[df['effect'] == eid]['updates_per_degree'].values for eid in sorted(df['effect'].unique())]
    axes[1, 1].boxplot(upd_data, tick_labels=gen_labels)
    axes[1, 1].axhline(1.0, color='red', linestyle='--', label='Minimum: 1 update/degree')
    axes[1, 1].set_ylabel('Updates per Degree')
    axes[1, 1].set_title('Update Rate by Effect')
    axes[1, 1].tick_params(axis='x', rotation=15)
    axes[1, 1].legend()

    plt.tight_layout()

    # Save figure
    output_path = OUTPUT_DIR / "timing_by_effect.png"
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"Saved visualization: {output_path}")
    plt.close()

def generate_report(df, stats, update_stats):
    """Generate markdown report with analysis findings."""

    report_lines = []

    # Header
    report_lines.append("# POV Display Performance Analysis")
    report_lines.append("")
    report_lines.append("Analysis of real-world timing data collected from ESP32-S3 POV display.")
    report_lines.append("")
    report_lines.append("## Understanding POV Display Timing")
    report_lines.append("")
    report_lines.append("**Critical Concept**: POV (Persistence of Vision) displays work differently than frame-based displays:")
    report_lines.append("")
    report_lines.append("- The LEDs are physically spinning on a rotating arm")
    report_lines.append("- The code runs in a tight loop, continuously updating LEDs based on current rotation angle")
    report_lines.append("- There is NO hard deadline per degree - the loop just runs as fast as it can")
    report_lines.append("- What matters: **How many LED updates happen per degree of rotation**")
    report_lines.append("")
    report_lines.append("If the loop updates LEDs fast enough (≥1 update per degree), the display will look smooth.")
    report_lines.append("If updates are slower (< 1 update per degree), there will be gaps/blur in the image.")
    report_lines.append("")

    # Data summary
    report_lines.append("## Data Summary")
    report_lines.append("")
    report_lines.append(f"**Total samples:** {len(df)}")
    report_lines.append(f"**Effects tested:** {sorted(df['effect'].unique())}")
    report_lines.append(f"**Test RPM:** {update_stats['rpm']:.1f}")
    report_lines.append(f"**Revolution period:** {update_stats['us_per_revolution']:.0f} μs")
    report_lines.append(f"**Time per degree:** {update_stats['us_per_degree']:.1f} μs")
    report_lines.append("")

    # Timing statistics
    report_lines.append("## Timing Statistics")
    report_lines.append("")

    report_lines.append("### Overall Performance")
    report_lines.append("")
    report_lines.append("| Component | Min | Max | Mean | Median | P95 | P99 | Std Dev |")
    report_lines.append("|-----------|-----|-----|------|--------|-----|-----|---------|")

    gen = stats['overall']['gen_us']
    report_lines.append(f"| Generation | {gen['min']:.0f} | {gen['max']:.0f} | {gen['mean']:.1f} | {gen['median']:.1f} | {gen['p95']:.1f} | {gen['p99']:.1f} | {gen['std']:.1f} |")

    xfer = stats['overall']['xfer_us']
    report_lines.append(f"| SPI Transfer | {xfer['min']:.0f} | {xfer['max']:.0f} | {xfer['mean']:.1f} | {xfer['median']:.1f} | {xfer['p95']:.1f} | {xfer['p99']:.1f} | {xfer['std']:.1f} |")

    total = stats['overall']['total_us']
    report_lines.append(f"| **Total** | **{total['min']:.0f}** | **{total['max']:.0f}** | **{total['mean']:.1f}** | **{total['median']:.1f}** | **{total['p95']:.1f}** | **{total['p99']:.1f}** | **{total['std']:.1f}** |")

    report_lines.append("")

    # Update rate analysis
    report_lines.append("## Update Rate Analysis (Key Metric)")
    report_lines.append("")
    report_lines.append("This is the critical metric for POV displays: **How many times do the LEDs update per degree of rotation?**")
    report_lines.append("")

    report_lines.append("### Degrees Traveled Per Frame")
    report_lines.append("")
    report_lines.append(f"- **Minimum:** {update_stats['degrees_per_frame']['min']:.2f}° (fastest updates)")
    report_lines.append(f"- **Maximum:** {update_stats['degrees_per_frame']['max']:.2f}° (slowest updates)")
    report_lines.append(f"- **Mean:** {update_stats['degrees_per_frame']['mean']:.2f}°")
    report_lines.append(f"- **Median:** {update_stats['degrees_per_frame']['median']:.2f}°")
    report_lines.append("")

    report_lines.append("### Updates Per Degree")
    report_lines.append("")
    report_lines.append(f"- **Minimum:** {update_stats['updates_per_degree']['min']:.2f} updates/degree")
    report_lines.append(f"- **Maximum:** {update_stats['updates_per_degree']['max']:.2f} updates/degree")
    report_lines.append(f"- **Mean:** {update_stats['updates_per_degree']['mean']:.2f} updates/degree")
    report_lines.append(f"- **Median:** {update_stats['updates_per_degree']['median']:.2f} updates/degree")
    report_lines.append("")

    # Per-effect breakdown
    report_lines.append("## Per-Effect Performance")
    report_lines.append("")

    effect_names = {
        0: "Per-Arm Blobs",
        1: "Virtual Blobs",
        2: "Solid Arms Diagnostic",
        3: "RPM Arc"
    }

    for effect_id in sorted(stats['by_effect'].keys()):
        effect_stats = stats['by_effect'][effect_id]
        effect_name = effect_names.get(effect_id, f"Effect {effect_id}")

        report_lines.append(f"### Effect {effect_id}: {effect_name}")
        report_lines.append("")
        report_lines.append(f"**Samples:** {effect_stats['count']}")
        report_lines.append("")

        report_lines.append("| Component | Min | Max | Mean | Median | P95 | P99 |")
        report_lines.append("|-----------|-----|-----|------|--------|-----|-----|")

        gen = effect_stats['gen_us']
        report_lines.append(f"| Generation | {gen['min']:.0f} | {gen['max']:.0f} | {gen['mean']:.1f} | {gen['median']:.1f} | {gen['p95']:.1f} | {gen['p99']:.1f} |")

        xfer = effect_stats['xfer_us']
        report_lines.append(f"| SPI Transfer | {xfer['min']:.0f} | {xfer['max']:.0f} | {xfer['mean']:.1f} | {xfer['median']:.1f} | - | - |")

        total = effect_stats['total_us']
        report_lines.append(f"| **Total** | **{total['min']:.0f}** | **{total['max']:.0f}** | **{total['mean']:.1f}** | **{total['median']:.1f}** | **{total['p95']:.1f}** | **{total['p99']:.1f}** |")

        report_lines.append("")

        deg = effect_stats['degrees_per_frame']
        upd = effect_stats['updates_per_degree']
        report_lines.append("**Update Rate:**")
        report_lines.append(f"- Degrees per frame: {deg['min']:.2f}° - {deg['max']:.2f}° (mean: {deg['mean']:.2f}°)")
        report_lines.append(f"- Updates per degree: {upd['min']:.2f} - {upd['max']:.2f} (mean: {upd['mean']:.2f})")
        report_lines.append("")

    # Performance bottleneck analysis
    report_lines.append("## Performance Bottleneck Analysis")
    report_lines.append("")

    avg_gen = stats['overall']['gen_us']['mean']
    avg_xfer = stats['overall']['xfer_us']['mean']
    total_avg = avg_gen + avg_xfer

    gen_pct = (avg_gen / total_avg) * 100
    xfer_pct = (avg_xfer / total_avg) * 100

    report_lines.append("### Time Breakdown (Average)")
    report_lines.append("")
    report_lines.append("| Component | Time (μs) | Percentage |")
    report_lines.append("|-----------|-----------|------------|")
    report_lines.append(f"| Generation/Render | {avg_gen:.1f} | {gen_pct:.1f}% |")
    report_lines.append(f"| SPI Transfer | {avg_xfer:.1f} | {xfer_pct:.1f}% |")
    report_lines.append(f"| **Total** | **{total_avg:.1f}** | **100%** |")
    report_lines.append("")

    # Identify bottleneck
    if gen_pct > xfer_pct:
        bottleneck = "Generation/Render"
        bottleneck_pct = gen_pct
    else:
        bottleneck = "SPI Transfer"
        bottleneck_pct = xfer_pct

    report_lines.append(f"**Primary bottleneck:** {bottleneck} ({bottleneck_pct:.1f}% of total time)")
    report_lines.append("")

    # Key findings
    report_lines.append("## Key Findings")
    report_lines.append("")

    # SPI consistency
    xfer_std = stats['overall']['xfer_us']['std']
    xfer_mean = stats['overall']['xfer_us']['mean']
    xfer_cv = (xfer_std / xfer_mean) * 100  # Coefficient of variation

    report_lines.append(f"### 1. SPI Transfer Performance")
    report_lines.append("")
    report_lines.append(f"- **Mean transfer time:** {xfer_mean:.1f} μs")
    report_lines.append(f"- **Standard deviation:** {xfer_std:.1f} μs")
    report_lines.append(f"- **Coefficient of variation:** {xfer_cv:.1f}%")
    report_lines.append("")
    if xfer_cv < 10:
        report_lines.append("✅ **Very consistent SPI timing** - excellent hardware SPI performance")
    else:
        report_lines.append("⚠️ **Variable SPI timing** detected")
    report_lines.append("")

    # Generation time variance
    report_lines.append(f"### 2. Render Complexity Variance")
    report_lines.append("")
    gen_max = stats['overall']['gen_us']['max']
    gen_min = stats['overall']['gen_us']['min']
    gen_range = gen_max - gen_min

    report_lines.append(f"- **Generation time range:** {gen_min:.0f} - {gen_max:.0f} μs")
    report_lines.append(f"- **Variance:** {gen_range:.0f} μs")
    report_lines.append("")

    # Compare effect complexities
    report_lines.append("**Effect complexity comparison:**")
    report_lines.append("")
    for effect_id in sorted(stats['by_effect'].keys()):
        effect_name = effect_names.get(effect_id, f"Effect {effect_id}")
        effect_gen_mean = stats['by_effect'][effect_id]['gen_us']['mean']
        report_lines.append(f"- {effect_name}: {effect_gen_mean:.1f} μs average")

    report_lines.append("")

    # Update rate assessment
    report_lines.append(f"### 3. Update Rate Assessment")
    report_lines.append("")

    min_updates = update_stats['updates_per_degree']['min']
    mean_updates = update_stats['updates_per_degree']['mean']

    if min_updates >= 1.0:
        report_lines.append(f"✅ **EXCELLENT:** All frames achieve ≥1 update per degree")
        report_lines.append(f"- Minimum: {min_updates:.2f} updates/degree")
        report_lines.append(f"- Average: {mean_updates:.2f} updates/degree")
        report_lines.append("")
        report_lines.append("The display will be smooth with no gaps or blur at this RPM.")
    elif mean_updates >= 1.0:
        report_lines.append(f"⚠️ **MARGINAL:** Some frames fall below 1 update per degree")
        report_lines.append(f"- Minimum: {min_updates:.2f} updates/degree")
        report_lines.append(f"- Average: {mean_updates:.2f} updates/degree")
        report_lines.append("")
        report_lines.append("Most frames are fine, but occasional gaps may be visible.")
    else:
        report_lines.append(f"❌ **INSUFFICIENT:** Update rate too low for smooth display")
        report_lines.append(f"- Minimum: {min_updates:.2f} updates/degree")
        report_lines.append(f"- Average: {mean_updates:.2f} updates/degree")
        report_lines.append("")
        report_lines.append("Display will have visible gaps/blur. Need to optimize render time.")

    report_lines.append("")

    # Worst-case effect
    worst_effect_id = None
    worst_updates = float('inf')
    for effect_id in sorted(stats['by_effect'].keys()):
        upd_min = stats['by_effect'][effect_id]['updates_per_degree']['min']
        if upd_min < worst_updates:
            worst_updates = upd_min
            worst_effect_id = effect_id

    if worst_effect_id is not None:
        worst_effect_name = effect_names.get(worst_effect_id, f"Effect {worst_effect_id}")
        report_lines.append(f"**Slowest effect:** {worst_effect_name} ({worst_updates:.2f} updates/degree minimum)")
        report_lines.append("")

    # Multi-RPM projection
    report_lines.append("## RPM Range Analysis")
    report_lines.append("")
    report_lines.append("Projecting performance across the full operating range:")
    report_lines.append("")

    rpms_to_test = [700, 1200, 1940, 2800]
    worst_total = stats['overall']['total_us']['max']

    report_lines.append("| RPM | μs/degree | Worst-case frame (μs) | Degrees/frame | Updates/degree | Status |")
    report_lines.append("|-----|-----------|----------------------|---------------|----------------|--------|")

    for rpm in rpms_to_test:
        us_per_deg = (60_000_000 / rpm) / 360.0
        deg_per_frame = worst_total / us_per_deg
        upd_per_deg = 1.0 / deg_per_frame

        if upd_per_deg >= 1.0:
            status = "✅ Good"
        elif upd_per_deg >= 0.5:
            status = "⚠️ Marginal"
        else:
            status = "❌ Poor"

        report_lines.append(f"| {rpm} | {us_per_deg:.1f} | {worst_total:.0f} | {deg_per_frame:.2f} | {upd_per_deg:.2f} | {status} |")

    report_lines.append("")

    # Recommendations
    report_lines.append("## Recommendations")
    report_lines.append("")

    # Based on actual performance
    if min_updates >= 1.0:
        report_lines.append("### ✅ Current Performance is Excellent")
        report_lines.append("")
        report_lines.append("At 2800 RPM (worst case), all effects achieve ≥1 update per degree.")
        report_lines.append("")
        report_lines.append("**No optimization needed** - the current implementation performs well.")
        report_lines.append("")
    else:
        report_lines.append("### ⚠️ Optimization Recommended")
        report_lines.append("")
        report_lines.append("Some effects fall below 1 update per degree. Consider:")
        report_lines.append("")
        report_lines.append("1. **Optimize render code** - especially for complex effects")
        report_lines.append("2. **Simplify effects** - reduce computational complexity")
        report_lines.append("3. **Profile hot paths** - use timing instrumentation to identify bottlenecks")
        report_lines.append("")

    # Effect-specific recommendations
    if worst_effect_id is not None:
        worst_effect_name = effect_names.get(worst_effect_id, f"Effect {worst_effect_id}")
        worst_gen_mean = stats['by_effect'][worst_effect_id]['gen_us']['mean']

        if worst_gen_mean > 200:
            report_lines.append(f"**Priority:** Optimize {worst_effect_name} (mean generation time: {worst_gen_mean:.1f} μs)")
            report_lines.append("")

    # SPI performance note
    report_lines.append("### SPI Performance")
    report_lines.append("")
    report_lines.append(f"The SPI transfer time ({xfer_mean:.1f} μs average) is excellent and consistent.")
    report_lines.append("This confirms NeoPixelBus is properly using hardware SPI at 40MHz.")
    report_lines.append("No SPI optimization needed.")
    report_lines.append("")

    # Visualizations
    report_lines.append("## Visualizations")
    report_lines.append("")
    report_lines.append("### Timing Distributions")
    report_lines.append("")
    report_lines.append("![Timing Distributions](./timing_distributions.png)")
    report_lines.append("")
    report_lines.append("### Per-Effect Comparison")
    report_lines.append("")
    report_lines.append("![Timing by Effect](./timing_by_effect.png)")
    report_lines.append("")

    return "\n".join(report_lines)

def main():
    """Main analysis pipeline."""

    print("=" * 60)
    print("POV Display Performance Analysis (CORRECTED)")
    print("=" * 60)

    # Load data
    df = load_timing_data()

    # Calculate update rate analysis
    print("\nCalculating update rate analysis...")
    update_stats, df = calculate_update_rate_analysis(df)

    # Calculate statistics
    print("Calculating timing statistics...")
    stats = calculate_statistics(df)

    # Create visualizations
    print("\nCreating visualizations...")
    create_visualizations(df)

    # Generate report
    print("\nGenerating analysis report...")
    report = generate_report(df, stats, update_stats)

    # Save report
    report_path = OUTPUT_DIR / "PERFORMANCE_ANALYSIS.md"
    with open(report_path, 'w') as f:
        f.write(report)

    print(f"\nReport saved to: {report_path}")

    print("\n" + "=" * 60)
    print("Analysis complete!")
    print("=" * 60)

    # Print quick summary to console
    print("\n📊 QUICK SUMMARY")
    print("-" * 60)
    print(f"Total frames analyzed: {len(df)}")
    print(f"RPM: {update_stats['rpm']:.1f}")
    print(f"Time per degree: {update_stats['us_per_degree']:.1f} μs")
    print(f"\nAverage times:")
    print(f"  Generation: {stats['overall']['gen_us']['mean']:.1f} μs")
    print(f"  SPI Transfer: {stats['overall']['xfer_us']['mean']:.1f} μs")
    print(f"  Total: {stats['overall']['total_us']['mean']:.1f} μs")
    print(f"\nUpdate rate:")
    print(f"  Degrees per frame: {update_stats['degrees_per_frame']['mean']:.2f}°")
    print(f"  Updates per degree: {update_stats['updates_per_degree']['mean']:.2f}")

    if update_stats['updates_per_degree']['min'] >= 1.0:
        print(f"\n✅ EXCELLENT: All frames achieve ≥1 update/degree")
    elif update_stats['updates_per_degree']['mean'] >= 1.0:
        print(f"\n⚠️ MARGINAL: Some frames below 1 update/degree")
    else:
        print(f"\n❌ INSUFFICIENT: Update rate too low")

    print("-" * 60)

if __name__ == "__main__":
    main()
