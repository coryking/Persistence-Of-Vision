#!/usr/bin/env python3
"""
SPI/DMA Timing Analysis - Pivot Table Style

Aggregates raw timing data into summary statistics.
No filtering, no judgments, no equations - just the numbers.
"""

import pandas as pd
from pathlib import Path

CSV_PATH = Path(__file__).parent / "result.csv"
WARMUP_ITERATIONS = 3


def main():
    df = pd.read_csv(CSV_PATH)

    # Skip warmup iterations
    df = df[df['iteration'] >= WARMUP_ITERATIONS].copy()

    # Compute derived columns
    df['wire_time_us'] = df['show2_us'] - df['show3_us']
    df['total_transfer_us'] = df['show2_us']  # show2 = overhead + wire (for DMA)

    # Group by all config columns
    group_cols = ['spi_mhz', 'method', 'feature', 'buffer_mode', 'led_count']

    # Aggregate statistics
    agg = df.groupby(group_cols).agg({
        'show1_us': ['mean', 'std', 'min', 'max'],
        'show2_us': ['mean', 'std', 'min', 'max'],
        'show3_us': ['mean', 'std', 'min', 'max'],
        'wire_time_us': ['mean', 'std', 'min', 'max'],
    }).round(2)

    # Flatten column names
    agg.columns = ['_'.join(col) for col in agg.columns]
    agg = agg.reset_index()

    # Save to CSV
    output_path = CSV_PATH.parent / "summary.csv"
    agg.to_csv(output_path, index=False)
    print(f"Saved: {output_path}")
    print(f"Rows: {len(agg)}")
    print(f"\nColumns: {', '.join(agg.columns)}")

    # Also print as readable table
    print("\n" + "="*120)
    print(agg.to_string(index=False))


if __name__ == "__main__":
    main()
