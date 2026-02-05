#!/usr/bin/env python3
"""
SPI/DMA Timing Analysis for HD107S/SK9822 LEDs

Analyzes timing data from NeoPixelBus benchmark to derive:
- Wire time (actual SPI transfer time)
- Overhead (setup/queue time)
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
    # But since wire_time = show2 - show3, for DMA: total = show3 + (show2 - show3) = show2
    # So actually show2 represents total time for a "burst" scenario
    # For "total time to LEDs updated" from a clean start:
    # - Sync: show3_us (it blocks for full transfer)
    # - DMA: show3_us is just queue time, wire happens in background
    #        Total = show3_us + wire_time_us = show2_us (approximately)
    df['total_transfer_us'] = df['show3_us'] + df['wire_time_us'].clip(lower=0)

    return df


def aggregate_by_config(df: pd.DataFrame) -> pd.DataFrame:
    """Aggregate statistics by configuration."""
    agg = df.groupby(['spi_mhz', 'method', 'feature', 'buffer_mode', 'led_count']).agg({
        'show1_us': ['mean', 'std', 'min', 'max'],
        'show2_us': ['mean', 'std', 'min', 'max'],
        'show3_us': ['mean', 'std', 'min', 'max'],
        'wire_time_us': ['mean', 'std'],
        'overhead_us': ['mean', 'std'],
        'total_transfer_us': ['mean', 'std'],
    }).round(2)

    # Flatten column names
    agg.columns = ['_'.join(col).strip() for col in agg.columns.values]
    return agg.reset_index()


def linear_regression(x: np.ndarray, y: np.ndarray) -> dict:
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
        }).reset_index()

        led_counts = means['led_count'].values

        # Regression for show2 (the "burst" call that includes wait time)
        reg_show2 = linear_regression(led_counts, means['show2_us'].values)

        # Regression for show3 (overhead only, no wait)
        reg_show3 = linear_regression(led_counts, means['show3_us'].values)

        # Regression for wire_time (show2 - show3)
        reg_wire = linear_regression(led_counts, means['wire_time_us'].values)

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
        })

    return pd.DataFrame(results)


def theoretical_wire_time(led_count: int, spi_mhz: int, bytes_per_led: int = 4) -> float:
    """
    Calculate theoretical SPI wire time.

    DotStar/SK9822 protocol:
    - Start frame: 4 bytes (0x00000000)
    - LED data: 4 bytes per LED (1 brightness + 3 RGB)
    - End frame: ceil(n/2) bytes of 0xFF (at least n/2 clock edges)

    Time = bits / clock_rate
    """
    start_frame_bytes = 4
    led_data_bytes = led_count * bytes_per_led
    end_frame_bytes = max(4, (led_count + 15) // 16 * 4)  # Round up to 4-byte boundary

    total_bytes = start_frame_bytes + led_data_bytes + end_frame_bytes
    total_bits = total_bytes * 8

    clock_hz = spi_mhz * 1_000_000
    time_us = (total_bits / clock_hz) * 1_000_000

    return time_us


def analyze_sync_vs_dma(df: pd.DataFrame) -> None:
    """Compare sync vs DMA behavior."""
    print("\n" + "="*80)
    print("SYNC vs DMA BEHAVIOR ANALYSIS")
    print("="*80)

    for spi_mhz in sorted(df['spi_mhz'].unique()):
        print(f"\n--- {spi_mhz} MHz ---")

        for feature in ['BGR', 'LBGR']:
            subset = df[(df['spi_mhz'] == spi_mhz) & (df['feature'] == feature)]

            sync_data = subset[subset['method'] == 'sync']
            dma_data = subset[subset['method'] == 'dma']

            if sync_data.empty or dma_data.empty:
                continue

            # Compare at a specific LED count (40 LEDs, typical use case)
            led_count = 40
            sync_40 = sync_data[sync_data['led_count'] == led_count]
            dma_40 = dma_data[dma_data['led_count'] == led_count]

            if sync_40.empty or dma_40.empty:
                continue

            print(f"\n  {feature} @ {led_count} LEDs:")
            print(f"    SYNC: show1={sync_40['show1_us'].mean():.1f}µs, "
                  f"show2={sync_40['show2_us'].mean():.1f}µs, "
                  f"show3={sync_40['show3_us'].mean():.1f}µs")
            print(f"    DMA:  show1={dma_40['show1_us'].mean():.1f}µs, "
                  f"show2={dma_40['show2_us'].mean():.1f}µs, "
                  f"show3={dma_40['show3_us'].mean():.1f}µs")

            # Check if sync shows expected behavior (all ~equal)
            sync_ratio = sync_40['show2_us'].mean() / sync_40['show3_us'].mean()
            print(f"    SYNC show2/show3 ratio: {sync_ratio:.2f} (expected ~1.0 for blocking)")

            # Check if DMA shows expected behavior (show2 >> show3)
            dma_ratio = dma_40['show2_us'].mean() / dma_40['show3_us'].mean()
            print(f"    DMA show2/show3 ratio: {dma_ratio:.2f} (expected >1.0 for async)")

            # Wire time estimate
            wire_time = dma_40['show2_us'].mean() - dma_40['show3_us'].mean()
            theoretical = theoretical_wire_time(led_count, spi_mhz)
            print(f"    DMA wire time: {wire_time:.1f}µs (theoretical: {theoretical:.1f}µs)")


def print_equations(equations_df: pd.DataFrame) -> None:
    """Print derived timing equations in readable format."""
    print("\n" + "="*80)
    print("DERIVED TIMING EQUATIONS")
    print("="*80)
    print("\nFormat: time_us = intercept + slope * led_count")
    print("        (intercept = constant overhead, slope = per-LED cost)")

    for method in ['sync', 'dma']:
        print(f"\n{'='*40}")
        print(f"  {method.upper()} METHODS")
        print(f"{'='*40}")

        method_data = equations_df[equations_df['method'] == method]

        for _, row in method_data.iterrows():
            print(f"\n  {row['spi_mhz']} MHz {row['feature']}:")

            # Show2 (back-to-back timing)
            print(f"    Back-to-back (show2): {row['show2_intercept_us']:.1f} + {row['show2_slope_us_per_led']:.3f} * n  (R²={row['show2_r_squared']:.4f})")

            # Show3 (overhead only)
            print(f"    After delay (show3):  {row['show3_intercept_us']:.1f} + {row['show3_slope_us_per_led']:.3f} * n  (R²={row['show3_r_squared']:.4f})")

            if method == 'dma':
                # Wire time (show2 - show3)
                print(f"    Wire time:            {row['wire_intercept_us']:.1f} + {row['wire_slope_us_per_led']:.3f} * n  (R²={row['wire_r_squared']:.4f})")


def print_practical_summary(equations_df: pd.DataFrame) -> None:
    """Print practical timing estimates for common LED counts."""
    print("\n" + "="*80)
    print("PRACTICAL TIMING ESTIMATES")
    print("="*80)

    led_counts = [40, 80, 100, 200]

    for method in ['sync', 'dma']:
        print(f"\n--- {method.upper()} ---")

        method_data = equations_df[equations_df['method'] == method]

        # Focus on 40MHz BGR (most common config)
        row = method_data[(method_data['spi_mhz'] == 40) & (method_data['feature'] == 'BGR')]
        if row.empty:
            continue
        row = row.iloc[0]

        print(f"\n  40 MHz BGR (production config):")
        print(f"  {'LEDs':<6} {'show2 (burst)':<15} {'show3 (spaced)':<15} {'wire time':<12}")
        print(f"  {'-'*6} {'-'*15} {'-'*15} {'-'*12}")

        for n in led_counts:
            show2 = row['show2_intercept_us'] + row['show2_slope_us_per_led'] * n
            show3 = row['show3_intercept_us'] + row['show3_slope_us_per_led'] * n
            wire = show2 - show3 if method == 'dma' else show2

            print(f"  {n:<6} {show2:<15.1f} {show3:<15.1f} {wire:<12.1f}")


def check_for_anomalies(df: pd.DataFrame) -> None:
    """Check for unexpected patterns in the data."""
    print("\n" + "="*80)
    print("ANOMALY CHECK")
    print("="*80)

    # Check: For sync, show2 should ≈ show3 (both blocking)
    sync_data = df[df['method'] == 'sync']
    sync_ratio = (sync_data['show2_us'] / sync_data['show3_us']).mean()
    print(f"\nSync show2/show3 ratio (expected ~1.0): {sync_ratio:.3f}")

    # Check: For DMA, show2 should > show3 (show2 waits for previous)
    dma_data = df[df['method'] == 'dma']
    dma_ratio = (dma_data['show2_us'] / dma_data['show3_us']).mean()
    print(f"DMA show2/show3 ratio (expected >1.0): {dma_ratio:.3f}")

    # Check: show1 should ≈ show3 (both have no pending DMA)
    show1_show3_ratio = (df['show1_us'] / df['show3_us']).mean()
    print(f"show1/show3 ratio (expected ~1.0): {show1_show3_ratio:.3f}")

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

    print("Computing derived metrics...")
    df = compute_derived_metrics(df)

    print("Deriving equations...")
    equations_df = derive_equations(df)

    # Output analysis
    analyze_sync_vs_dma(df)
    print_equations(equations_df)
    print_practical_summary(equations_df)
    check_for_anomalies(df)

    # Save equations to CSV
    equations_path = CSV_PATH.parent / "equations.csv"
    equations_df.to_csv(equations_path, index=False)
    print(f"\n\nEquations saved to: {equations_path}")

    # Save aggregated data
    agg_df = aggregate_by_config(df)
    agg_path = CSV_PATH.parent / "aggregated.csv"
    agg_df.to_csv(agg_path, index=False)
    print(f"Aggregated data saved to: {agg_path}")


if __name__ == "__main__":
    main()
