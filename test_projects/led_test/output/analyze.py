#!/usr/bin/env python3
"""
SPI/DMA Timing Analysis for HD107S/SK9822 LEDs

Analyzes timing data from NeoPixelBus benchmark to derive:
- Wire time (actual SPI transfer time)
- Overhead (setup/queue time)
- Buffer mode comparison (copy vs swap)
- Total transfer time (Show call to LEDs updated)
- Linear scaling equations: time = a + b * n (where n = LED count)
"""

import pandas as pd
import numpy as np
from scipy import stats
from pathlib import Path

# Configuration
WARMUP_ITERATIONS = 3  # Skip first N iterations (warmup)
CSV_PATH = Path(__file__).parent / "result.csv"


def load_and_clean_data(csv_path: Path) -> pd.DataFrame:
    """Load CSV and filter out warmup iterations."""
    df = pd.read_csv(csv_path)
    # Skip warmup iterations
    df = df[df['iteration'] >= WARMUP_ITERATIONS].copy()

    # Handle old format (no buffer_mode column) by adding default
    if 'buffer_mode' not in df.columns:
        df['buffer_mode'] = 'copy'

    return df


def compute_derived_metrics(df: pd.DataFrame) -> pd.DataFrame:
    """Add derived timing metrics."""
    # For DMA: wire_time = show2 - show3 (show2 waits, show3 doesn't)
    # For sync: wire_time ≈ 0 (show2 ≈ show3, both block fully)
    df['wire_time_us'] = df['show2_us'] - df['show3_us']

    # Overhead is the "clean start" show time (show3 after delay)
    df['overhead_us'] = df['show3_us']

    # Total transfer time from Show() call to LEDs updated:
    # - For sync: show3_us (blocking, LEDs update when Show returns)
    # - For DMA: show3_us + wire_time_us (queue time + background transfer)
    # Clipping wire_time to 0 for sync (where it may be slightly negative due to noise)
    df['total_transfer_us'] = df['show3_us'] + df['wire_time_us'].clip(lower=0)

    return df


def linear_regression(x, y) -> dict:
    """Perform linear regression: y = a + b*x"""
    slope, intercept, r_value, p_value, std_err = stats.linregress(x, y)
    return {
        'intercept_a': intercept,  # Constant overhead
        'slope_b': slope,          # Per-LED cost
        'r_squared': r_value**2,
        'std_err': std_err,
    }


def derive_equations(df: pd.DataFrame) -> pd.DataFrame:
    """Derive timing equations for each method/speed/feature/buffer_mode combo."""
    results = []

    for (spi_mhz, method, feature, buffer_mode), group in df.groupby(['spi_mhz', 'method', 'feature', 'buffer_mode']):
        # Get mean values per LED count
        means = group.groupby('led_count').agg({
            'show1_us': 'mean',
            'show2_us': 'mean',
            'show3_us': 'mean',
            'wire_time_us': 'mean',
            'total_transfer_us': 'mean',
        }).reset_index()

        led_counts = means['led_count'].values

        # Regression for show2 (the "burst" call that includes wait time)
        reg_show2 = linear_regression(led_counts, means['show2_us'].values)

        # Regression for show3 (overhead only, no wait)
        reg_show3 = linear_regression(led_counts, means['show3_us'].values)

        # Regression for wire_time (show2 - show3)
        reg_wire = linear_regression(led_counts, means['wire_time_us'].values)

        # Regression for total transfer time
        reg_total = linear_regression(led_counts, means['total_transfer_us'].values)

        results.append({
            'spi_mhz': spi_mhz,
            'method': method,
            'feature': feature,
            'buffer_mode': buffer_mode,
            # Show2 equation (total time when back-to-back)
            'show2_intercept_us': round(reg_show2['intercept_a'], 2),
            'show2_slope_us_per_led': round(reg_show2['slope_b'], 3),
            'show2_r_squared': round(reg_show2['r_squared'], 4),
            # Show3 equation (overhead only)
            'show3_intercept_us': round(reg_show3['intercept_a'], 2),
            'show3_slope_us_per_led': round(reg_show3['slope_b'], 3),
            'show3_r_squared': round(reg_show3['r_squared'], 4),
            # Wire time equation
            'wire_intercept_us': round(reg_wire['intercept_a'], 2),
            'wire_slope_us_per_led': round(reg_wire['slope_b'], 3),
            'wire_r_squared': round(reg_wire['r_squared'], 4),
            # Total transfer time equation
            'total_intercept_us': round(reg_total['intercept_a'], 2),
            'total_slope_us_per_led': round(reg_total['slope_b'], 3),
            'total_r_squared': round(reg_total['r_squared'], 4),
        })

    return pd.DataFrame(results)


def analyze_buffer_modes(df: pd.DataFrame) -> None:
    """Compare copy vs swap buffer modes."""
    print("\n" + "="*80)
    print("BUFFER MODE COMPARISON (copy vs swap)")
    print("="*80)
    print("\nmaintainBuffer=true (copy): Copies edit buffer to send buffer")
    print("maintainBuffer=false (swap): Swaps buffers (faster, but you must overwrite all pixels)")

    for method in ['sync', 'dma']:
        print(f"\n--- {method.upper()} ---")

        method_data = df[df['method'] == method]
        if method_data.empty:
            print("  No data")
            continue

        # Compare at 40MHz BGR, various LED counts
        for led_count in [40, 100, 200]:
            subset = method_data[(method_data['spi_mhz'] == 40) &
                                (method_data['feature'] == 'BGR') &
                                (method_data['led_count'] == led_count)]

            copy_data = subset[subset['buffer_mode'] == 'copy']
            swap_data = subset[subset['buffer_mode'] == 'swap']

            if copy_data.empty or swap_data.empty:
                continue

            copy_show3 = copy_data['show3_us'].mean()
            swap_show3 = swap_data['show3_us'].mean()
            diff = copy_show3 - swap_show3
            pct = (diff / copy_show3) * 100 if copy_show3 > 0 else 0

            print(f"  40MHz BGR @ {led_count} LEDs:")
            print(f"    copy: {copy_show3:.1f}µs, swap: {swap_show3:.1f}µs, diff: {diff:.1f}µs ({pct:.1f}% faster)")


def analyze_total_transfer_time(df: pd.DataFrame) -> None:
    """Compare total transfer time between sync and DMA."""
    print("\n" + "="*80)
    print("TOTAL TRANSFER TIME ANALYSIS")
    print("="*80)
    print("\nQuestion: Does DMA increase total time from Show() to LEDs updated?")
    print("Total time = overhead (Show return) + wire time (background transfer)")

    for spi_mhz in [40, 20, 10]:
        print(f"\n--- {spi_mhz} MHz ---")

        for led_count in [40, 100, 200]:
            print(f"\n  {led_count} LEDs (BGR, copy mode):")

            sync_data = df[(df['method'] == 'sync') &
                          (df['spi_mhz'] == spi_mhz) &
                          (df['feature'] == 'BGR') &
                          (df['buffer_mode'] == 'copy') &
                          (df['led_count'] == led_count)]

            dma_data = df[(df['method'] == 'dma') &
                         (df['spi_mhz'] == spi_mhz) &
                         (df['feature'] == 'BGR') &
                         (df['buffer_mode'] == 'copy') &
                         (df['led_count'] == led_count)]

            if sync_data.empty or dma_data.empty:
                print("    (missing data)")
                continue

            # For sync: total = show3 (blocking)
            sync_total = sync_data['show3_us'].mean()

            # For DMA: total = show3 (overhead) + wire_time (background)
            dma_overhead = dma_data['show3_us'].mean()
            dma_wire = dma_data['wire_time_us'].mean()
            dma_total = dma_overhead + max(0, dma_wire)

            diff = dma_total - sync_total
            diff_pct = (diff / sync_total) * 100 if sync_total > 0 else 0

            print(f"    SYNC total: {sync_total:.1f}µs (all blocking)")
            print(f"    DMA  total: {dma_total:.1f}µs (overhead: {dma_overhead:.1f}µs + wire: {dma_wire:.1f}µs)")
            print(f"    Difference: {diff:+.1f}µs ({diff_pct:+.1f}%)")
            print(f"    CPU freed:  {dma_wire:.1f}µs ({(dma_wire/dma_total)*100:.0f}% of transfer time)")


def print_equations(equations_df: pd.DataFrame) -> None:
    """Print derived timing equations in readable format."""
    print("\n" + "="*80)
    print("DERIVED TIMING EQUATIONS")
    print("="*80)
    print("\nFormat: time_us = intercept + slope * led_count")
    print("        (intercept = constant overhead, slope = per-LED cost)")

    # Focus on copy mode (most common usage)
    copy_equations = equations_df[equations_df['buffer_mode'] == 'copy']

    for method in ['sync', 'dma']:
        print(f"\n{'='*40}")
        print(f"  {method.upper()} METHODS (buffer_mode=copy)")
        print(f"{'='*40}")

        method_data = copy_equations[copy_equations['method'] == method]

        for _, row in method_data.iterrows():
            print(f"\n  {row['spi_mhz']} MHz {row['feature']}:")

            # Show2 (back-to-back timing)
            print(f"    Back-to-back (show2): {row['show2_intercept_us']:.1f} + {row['show2_slope_us_per_led']:.3f} * n  (R²={row['show2_r_squared']:.4f})")

            # Show3 (overhead only)
            print(f"    After delay (show3):  {row['show3_intercept_us']:.1f} + {row['show3_slope_us_per_led']:.3f} * n  (R²={row['show3_r_squared']:.4f})")

            if method == 'dma':
                # Wire time (show2 - show3)
                print(f"    Wire time:            {row['wire_intercept_us']:.1f} + {row['wire_slope_us_per_led']:.3f} * n  (R²={row['wire_r_squared']:.4f})")

            # Total transfer time
            print(f"    Total transfer:       {row['total_intercept_us']:.1f} + {row['total_slope_us_per_led']:.3f} * n  (R²={row['total_r_squared']:.4f})")


def print_practical_summary(equations_df: pd.DataFrame) -> None:
    """Print practical timing estimates for common LED counts."""
    print("\n" + "="*80)
    print("PRACTICAL TIMING ESTIMATES (40MHz BGR, copy mode)")
    print("="*80)

    led_counts = [40, 80, 100, 200]

    copy_equations = equations_df[equations_df['buffer_mode'] == 'copy']

    for method in ['sync', 'dma']:
        print(f"\n--- {method.upper()} ---")

        row = copy_equations[(copy_equations['method'] == method) &
                            (copy_equations['spi_mhz'] == 40) &
                            (copy_equations['feature'] == 'BGR')]
        if row.empty:
            print("  No data")
            continue
        row = row.iloc[0]

        print(f"\n  {'LEDs':<6} {'Show() returns':<16} {'Total transfer':<16} {'CPU free time':<14}")
        print(f"  {'-'*6} {'-'*16} {'-'*16} {'-'*14}")

        for n in led_counts:
            show3 = row['show3_intercept_us'] + row['show3_slope_us_per_led'] * n
            total = row['total_intercept_us'] + row['total_slope_us_per_led'] * n
            cpu_free = total - show3 if method == 'dma' else 0

            print(f"  {n:<6} {show3:<16.1f} {total:<16.1f} {cpu_free:<14.1f}")


def check_for_anomalies(df: pd.DataFrame) -> None:
    """Check for unexpected patterns in the data."""
    print("\n" + "="*80)
    print("ANOMALY CHECK")
    print("="*80)

    # Use copy mode for checks
    copy_data = df[df['buffer_mode'] == 'copy']

    # Check: For sync, show2 should ≈ show3 (both blocking)
    sync_data = copy_data[copy_data['method'] == 'sync']
    if not sync_data.empty:
        sync_ratio = (sync_data['show2_us'] / sync_data['show3_us']).mean()
        print(f"\nSync show2/show3 ratio (expected ~1.0): {sync_ratio:.3f}")

    # Check: For DMA, show2 should > show3 (show2 waits for previous)
    dma_data = copy_data[copy_data['method'] == 'dma']
    if not dma_data.empty:
        dma_ratio = (dma_data['show2_us'] / dma_data['show3_us']).mean()
        print(f"DMA show2/show3 ratio (expected >1.0): {dma_ratio:.3f}")

    # Check: Wire time should scale with 1/clock
    print("\nWire time scaling with clock speed (should halve when clock doubles):")
    for feature in ['BGR', 'LBGR']:
        dma_feat = dma_data[dma_data['feature'] == feature]
        for led_count in [40, 100, 200]:
            times = {}
            for mhz in [10, 20, 40]:
                subset = dma_feat[(dma_feat['spi_mhz'] == mhz) & (dma_feat['led_count'] == led_count)]
                if not subset.empty:
                    times[mhz] = subset['wire_time_us'].mean()

            if len(times) == 3:
                ratio_10_20 = times[10] / times[20] if times[20] > 0 else 0
                ratio_20_40 = times[20] / times[40] if times[40] > 0 else 0
                print(f"  {feature} @ {led_count} LEDs: 10/20MHz={ratio_10_20:.2f}x, 20/40MHz={ratio_20_40:.2f}x (expected ~2.0)")


def main():
    print("Loading data...")
    df = load_and_clean_data(CSV_PATH)
    print(f"Loaded {len(df)} data points (after removing {WARMUP_ITERATIONS} warmup iterations)")

    # Check if we have buffer_mode data
    buffer_modes = df['buffer_mode'].unique()
    print(f"Buffer modes in data: {list(buffer_modes)}")

    print("Computing derived metrics...")
    df = compute_derived_metrics(df)

    print("Deriving equations...")
    equations_df = derive_equations(df)

    # Output analysis
    analyze_buffer_modes(df)
    analyze_total_transfer_time(df)
    print_equations(equations_df)
    print_practical_summary(equations_df)
    check_for_anomalies(df)

    # Save equations to CSV
    equations_path = CSV_PATH.parent / "equations.csv"
    equations_df.to_csv(equations_path, index=False)
    print(f"\n\nEquations saved to: {equations_path}")


if __name__ == "__main__":
    main()
