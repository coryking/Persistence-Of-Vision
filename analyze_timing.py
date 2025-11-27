#!/usr/bin/env python3
"""
POV Display Timing Analysis
Analyzes timing data collected from ESP32-S3 POV display during operation.
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
import seaborn as sns

# Data directory
SAMPLES_DIR = Path("/Users/coryking/projects/POV_Project/samples")
OUTPUT_DIR = Path("/Users/coryking/projects/POV_Project/docs")

# Timing budgets (microseconds)
TIMING_BUDGETS = {
    700: 238.1,   # 700 RPM = 238.1 μs/degree
    1200: 138.9,  # 1200 RPM = 138.9 μs/degree
    1940: 85.9,   # 1940 RPM = 85.9 μs/degree
    2800: 59.5,   # 2800 RPM = 59.5 μs/degree
}

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

def calculate_statistics(df):
    """Calculate comprehensive statistics for timing data."""

    stats = {}

    # Overall statistics
    stats['overall'] = {
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
        }

    return stats

def analyze_timing_budget(df, rpm):
    """Analyze whether frames meet timing budget at given RPM."""

    if rpm not in TIMING_BUDGETS:
        raise ValueError(f"No timing budget defined for {rpm} RPM")

    budget = TIMING_BUDGETS[rpm]

    # Count violations
    violations = df[df['total_us'] > budget]
    total_frames = len(df)
    violation_count = len(violations)

    success_rate = ((total_frames - violation_count) / total_frames) * 100

    return {
        'rpm': rpm,
        'budget_us': budget,
        'total_frames': total_frames,
        'violations': violation_count,
        'success_rate_pct': success_rate,
        'worst_case_us': df['total_us'].max(),
        'margin_us': budget - df['total_us'].max(),
    }

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
    axes[1, 0].axvline(TIMING_BUDGETS[2800], color='orange', linestyle='--', label=f"2800 RPM budget: {TIMING_BUDGETS[2800]:.1f}μs")
    axes[1, 0].set_xlabel('Total Frame Time (μs)')
    axes[1, 0].set_ylabel('Frequency')
    axes[1, 0].set_title('Total Frame Time Distribution')
    axes[1, 0].legend()

    # Per-effect comparison
    effect_data = [df[df['effect'] == eid]['total_us'].values for eid in sorted(df['effect'].unique())]
    effect_labels = [f"Effect {eid}" for eid in sorted(df['effect'].unique())]
    axes[1, 1].boxplot(effect_data, labels=effect_labels)
    axes[1, 1].axhline(TIMING_BUDGETS[2800], color='red', linestyle='--', label=f"2800 RPM budget")
    axes[1, 1].set_xlabel('Effect')
    axes[1, 1].set_ylabel('Total Frame Time (μs)')
    axes[1, 1].set_title('Frame Time by Effect')
    axes[1, 1].legend()

    plt.tight_layout()

    # Save figure
    output_path = OUTPUT_DIR / "timing_distributions.png"
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"\nSaved visualization: {output_path}")
    plt.close()

    # 2. Time series plot showing timing over frames
    fig, axes = plt.subplots(3, 1, figsize=(14, 10))
    fig.suptitle('POV Display Timing Over Frames', fontsize=16)

    for effect_id in sorted(df['effect'].unique()):
        effect_df = df[df['effect'] == effect_id]

        # Plot generation time
        axes[0].plot(effect_df.index, effect_df['gen_us'], label=f"Effect {effect_id}", alpha=0.7)

        # Plot transfer time
        axes[1].plot(effect_df.index, effect_df['xfer_us'], label=f"Effect {effect_id}", alpha=0.7)

        # Plot total time
        axes[2].plot(effect_df.index, effect_df['total_us'], label=f"Effect {effect_id}", alpha=0.7)

    axes[0].set_ylabel('Generation Time (μs)')
    axes[0].set_title('Render/Generation Time Over Frames')
    axes[0].legend()
    axes[0].grid(True)

    axes[1].set_ylabel('SPI Transfer Time (μs)')
    axes[1].set_title('SPI Transfer Time Over Frames')
    axes[1].legend()
    axes[1].grid(True)

    axes[2].axhline(TIMING_BUDGETS[2800], color='red', linestyle='--', label='2800 RPM budget', linewidth=2)
    axes[2].set_xlabel('Frame Number')
    axes[2].set_ylabel('Total Frame Time (μs)')
    axes[2].set_title('Total Frame Time Over Frames')
    axes[2].legend()
    axes[2].grid(True)

    plt.tight_layout()

    # Save figure
    output_path = OUTPUT_DIR / "timing_timeseries.png"
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"Saved visualization: {output_path}")
    plt.close()

def generate_report(df, stats):
    """Generate markdown report with analysis findings."""

    report_lines = []

    # Header
    report_lines.append("# POV Display Performance Analysis")
    report_lines.append("")
    report_lines.append("Analysis of real-world timing data collected from ESP32-S3 POV display.")
    report_lines.append("")
    report_lines.append(f"**Data collected:** {df['source_file'].iloc[0]}")
    report_lines.append(f"**Total samples:** {len(df)}")
    report_lines.append(f"**Effects tested:** {sorted(df['effect'].unique())}")
    report_lines.append(f"**RPM:** {df['rpm'].iloc[0]:.1f}")
    report_lines.append("")

    # Timing budgets
    report_lines.append("## Timing Budgets")
    report_lines.append("")
    report_lines.append("| RPM  | μs/degree | μs/revolution |")
    report_lines.append("|------|-----------|---------------|")
    for rpm, budget in sorted(TIMING_BUDGETS.items()):
        us_per_rev = (60_000_000 / rpm)
        report_lines.append(f"| {rpm} | {budget:.1f} | {us_per_rev:.0f} |")
    report_lines.append("")

    # Overall statistics
    report_lines.append("## Overall Timing Statistics")
    report_lines.append("")
    report_lines.append("### Generation Time (Render)")
    report_lines.append("")
    report_lines.append("| Metric | Value (μs) |")
    report_lines.append("|--------|------------|")
    for metric, value in stats['overall']['gen_us'].items():
        report_lines.append(f"| {metric.upper()} | {value:.1f} |")
    report_lines.append("")

    report_lines.append("### SPI Transfer Time")
    report_lines.append("")
    report_lines.append("| Metric | Value (μs) |")
    report_lines.append("|--------|------------|")
    for metric, value in stats['overall']['xfer_us'].items():
        report_lines.append(f"| {metric.upper()} | {value:.1f} |")
    report_lines.append("")

    report_lines.append("### Total Frame Time")
    report_lines.append("")
    report_lines.append("| Metric | Value (μs) |")
    report_lines.append("|--------|------------|")
    for metric, value in stats['overall']['total_us'].items():
        report_lines.append(f"| {metric.upper()} | {value:.1f} |")
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

    # Timing budget analysis
    report_lines.append("## Timing Budget Analysis")
    report_lines.append("")

    current_rpm = df['rpm'].iloc[0]
    budget_analysis = analyze_timing_budget(df, 2800)  # Use 2800 RPM (worst case)

    report_lines.append(f"**Test RPM:** {current_rpm:.1f}")
    report_lines.append(f"**Worst-case RPM analyzed:** {budget_analysis['rpm']}")
    report_lines.append(f"**Timing budget:** {budget_analysis['budget_us']:.1f} μs/frame")
    report_lines.append("")

    report_lines.append("| Metric | Value |")
    report_lines.append("|--------|-------|")
    report_lines.append(f"| Total frames | {budget_analysis['total_frames']} |")
    report_lines.append(f"| Budget violations | {budget_analysis['violations']} |")
    report_lines.append(f"| Success rate | {budget_analysis['success_rate_pct']:.1f}% |")
    report_lines.append(f"| Worst-case frame time | {budget_analysis['worst_case_us']:.1f} μs |")
    report_lines.append(f"| Timing margin | {budget_analysis['margin_us']:.1f} μs |")
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

    # Recommendations
    report_lines.append("## Key Findings & Recommendations")
    report_lines.append("")

    if budget_analysis['margin_us'] > 0:
        report_lines.append(f"✅ **Current performance meets timing budget** with {budget_analysis['margin_us']:.1f} μs margin at 2800 RPM")
    else:
        report_lines.append(f"⚠️ **Timing budget exceeded** by {abs(budget_analysis['margin_us']):.1f} μs at 2800 RPM")

    report_lines.append("")

    # Check SPI consistency
    xfer_std = stats['overall']['xfer_us']['std']
    xfer_mean = stats['overall']['xfer_us']['mean']
    xfer_cv = (xfer_std / xfer_mean) * 100  # Coefficient of variation

    report_lines.append(f"- **SPI transfer time:** {xfer_mean:.1f} ± {xfer_std:.1f} μs (CV: {xfer_cv:.1f}%)")
    if xfer_cv < 10:
        report_lines.append("  - ✅ Very consistent SPI timing (good)")
    else:
        report_lines.append("  - ⚠️ Variable SPI timing detected")

    report_lines.append("")

    # Generation time analysis
    gen_max = stats['overall']['gen_us']['max']
    gen_min = stats['overall']['gen_us']['min']
    gen_range = gen_max - gen_min

    report_lines.append(f"- **Generation time range:** {gen_min:.0f}-{gen_max:.0f} μs (range: {gen_range:.0f} μs)")
    if gen_range > 100:
        report_lines.append("  - ⚠️ High variation in render times across effects")
        report_lines.append("  - Consider optimizing complex effects or reducing effect complexity")
    else:
        report_lines.append("  - ✅ Consistent render performance across effects")

    report_lines.append("")

    # Resolution feasibility
    report_lines.append("### Resolution Feasibility")
    report_lines.append("")

    max_total = stats['overall']['total_us']['max']

    report_lines.append("| RPM  | Budget (μs) | Max Frame (μs) | Feasible? |")
    report_lines.append("|------|-------------|----------------|-----------|")
    for rpm, budget in sorted(TIMING_BUDGETS.items()):
        feasible = "✅ Yes" if max_total < budget else "❌ No"
        report_lines.append(f"| {rpm} | {budget:.1f} | {max_total:.1f} | {feasible} |")

    report_lines.append("")

    # Final recommendation
    report_lines.append("### Final Recommendation")
    report_lines.append("")

    if max_total < TIMING_BUDGETS[2800]:
        report_lines.append("**✅ 1° fixed resolution is FEASIBLE across entire RPM range (700-2800 RPM)**")
        report_lines.append("")
        report_lines.append(f"The worst-case frame time ({max_total:.1f} μs) is well within the timing budget even at maximum RPM (2800 RPM = {TIMING_BUDGETS[2800]:.1f} μs/degree). No adaptive resolution needed.")
    else:
        report_lines.append("**⚠️ Adaptive resolution RECOMMENDED**")
        report_lines.append("")
        report_lines.append(f"The worst-case frame time ({max_total:.1f} μs) exceeds the timing budget at high RPM. Consider:")
        report_lines.append("- Implementing adaptive angular resolution (e.g., 2° at high RPM)")
        report_lines.append("- Optimizing render code for complex effects")
        report_lines.append("- Simplifying effects at high RPM")

    report_lines.append("")

    # Visualizations
    report_lines.append("## Visualizations")
    report_lines.append("")
    report_lines.append("![Timing Distributions](./timing_distributions.png)")
    report_lines.append("")
    report_lines.append("![Timing Time Series](./timing_timeseries.png)")
    report_lines.append("")

    return "\n".join(report_lines)

def main():
    """Main analysis pipeline."""

    print("=" * 60)
    print("POV Display Performance Analysis")
    print("=" * 60)

    # Load data
    df = load_timing_data()

    # Calculate statistics
    print("\nCalculating statistics...")
    stats = calculate_statistics(df)

    # Create visualizations
    print("\nCreating visualizations...")
    create_visualizations(df)

    # Generate report
    print("\nGenerating analysis report...")
    report = generate_report(df, stats)

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
    print(f"RPM: {df['rpm'].iloc[0]:.1f}")
    print(f"\nAverage times:")
    print(f"  Generation: {stats['overall']['gen_us']['mean']:.1f} μs")
    print(f"  SPI Transfer: {stats['overall']['xfer_us']['mean']:.1f} μs")
    print(f"  Total: {stats['overall']['total_us']['mean']:.1f} μs")
    print(f"\nWorst-case frame time: {stats['overall']['total_us']['max']:.1f} μs")
    print(f"2800 RPM budget: {TIMING_BUDGETS[2800]:.1f} μs")

    budget_analysis = analyze_timing_budget(df, 2800)
    if budget_analysis['margin_us'] > 0:
        print(f"\n✅ PASSES timing budget with {budget_analysis['margin_us']:.1f} μs margin")
    else:
        print(f"\n❌ EXCEEDS timing budget by {abs(budget_analysis['margin_us']):.1f} μs")

    print("-" * 60)

if __name__ == "__main__":
    main()
