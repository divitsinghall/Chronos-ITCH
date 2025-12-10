#!/usr/bin/env python3
"""
demo.py - ITCH Parser Python Demo

Demonstrates using the itch_handler module to parse PCAP files
and analyze market data with Pandas.

Usage:
    python demo.py [pcap_file]

Example:
    python demo.py ../data/Multiple.Packets.pcap
"""

import sys
from pathlib import Path

# Try to import itch_handler from build directory
try:
    import itch_handler
except ImportError:
    # Add build directory to path if running from source
    build_dir = Path(__file__).parent.parent / "build"
    sys.path.insert(0, str(build_dir))
    import itch_handler

import pandas as pd
import numpy as np


def parse_and_analyze(pcap_file: str) -> None:
    """Parse PCAP file and display analysis."""
    
    print(f"ITCH Parser Python Demo (v{itch_handler.version()})")
    print("=" * 60)
    
    # Parse the file
    print(f"\nParsing: {pcap_file}")
    data = itch_handler.parse_file(pcap_file)
    
    print(f"Packets processed: {data['packet_count']}")
    print(f"File size: {data['file_size']:,} bytes")
    
    # Convert AddOrders to DataFrame
    add_orders = data['add_orders']
    if len(add_orders['order_ref']) > 0:
        df_orders = pd.DataFrame({
            'order_ref': add_orders['order_ref'],
            'timestamp_ns': add_orders['timestamp'],
            'stock_locate': add_orders['stock_locate'],
            'shares': add_orders['shares'],
            'price_raw': add_orders['price'],
            'side': [chr(s) for s in add_orders['side']],  # Convert char to string
        })
        
        # Convert price from fixed point (price * 10000) to float
        df_orders['price'] = df_orders['price_raw'] / 10000.0
        
        # Convert timestamp to readable format (nanoseconds since midnight)
        df_orders['time'] = pd.to_timedelta(df_orders['timestamp_ns'], unit='ns')
        
        print(f"\n=== Add Orders ({len(df_orders)} total) ===")
        print("\nFirst 5 rows:")
        print(df_orders[['order_ref', 'time', 'side', 'shares', 'price']].head())
        
        print(f"\nStatistics:")
        print(f"  Average price:  ${df_orders['price'].mean():.2f}")
        print(f"  Total volume:   {df_orders['shares'].sum():,} shares")
        print(f"  Buy orders:     {(df_orders['side'] == 'B').sum()}")
        print(f"  Sell orders:    {(df_orders['side'] == 'S').sum()}")
    else:
        print("\nNo Add Orders found in file.")
    
    # Convert OrderExecuted to DataFrame
    order_executed = data['order_executed']
    if len(order_executed['order_ref']) > 0:
        df_exec = pd.DataFrame({
            'order_ref': order_executed['order_ref'],
            'timestamp_ns': order_executed['timestamp'],
            'stock_locate': order_executed['stock_locate'],
            'executed_shares': order_executed['executed_shares'],
            'match_number': order_executed['match_number'],
        })
        
        df_exec['time'] = pd.to_timedelta(df_exec['timestamp_ns'], unit='ns')
        
        print(f"\n=== Order Executed ({len(df_exec)} total) ===")
        print("\nFirst 5 rows:")
        print(df_exec[['order_ref', 'time', 'executed_shares', 'match_number']].head())
        
        print(f"\nStatistics:")
        print(f"  Total executions:  {len(df_exec)}")
        print(f"  Total volume:      {df_exec['executed_shares'].sum():,} shares")
    else:
        print("\nNo Order Executed messages found in file.")
    
    print("\n" + "=" * 60)
    print("Demo complete!")


def main():
    # Default to sample file if no argument provided
    if len(sys.argv) > 1:
        pcap_file = sys.argv[1]
    else:
        # Try finding the sample file relative to script location
        script_dir = Path(__file__).parent
        candidates = [
            script_dir.parent / "data" / "Multiple.Packets.pcap",
            script_dir / "data" / "Multiple.Packets.pcap",
            Path("data/Multiple.Packets.pcap"),
            Path("../data/Multiple.Packets.pcap"),
        ]
        
        pcap_file = None
        for candidate in candidates:
            if candidate.exists():
                pcap_file = str(candidate)
                break
        
        if pcap_file is None:
            print("Usage: python demo.py <pcap_file>")
            print("\nNo PCAP file provided and could not find sample file.")
            print("Tried:")
            for c in candidates:
                print(f"  - {c}")
            sys.exit(1)
    
    # Verify file exists
    if not Path(pcap_file).exists():
        print(f"Error: File not found: {pcap_file}")
        sys.exit(1)
    
    parse_and_analyze(pcap_file)


if __name__ == "__main__":
    main()
