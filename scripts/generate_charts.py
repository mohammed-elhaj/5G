#!/usr/bin/env python3
"""
generate_charts.py — Profiling Chart Generator for 5G Layer 2 Simulator

Reads profiling_results.csv and generates three charts:
  1. Bar chart: Average time per layer (TX and RX)
  2. Line chart: Time vs TB size per layer
  3. Line chart: Time vs packet size per layer

Output: PNG files saved to docs/charts/
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os
import sys

def ensure_output_dir():
    """Create docs/charts/ directory if it doesn't exist"""
    os.makedirs('docs/charts', exist_ok=True)

def load_profiling_data(csv_path='profiling_results.csv'):
    """Load and validate the profiling CSV"""
    if not os.path.exists(csv_path):
        print(f"ERROR: {csv_path} not found. Run the profiling first.")
        sys.exit(1)
    
    df = pd.read_csv(csv_path)
    print(f"Loaded {len(df)} profiling records from {csv_path}")
    return df

def chart1_avg_time_per_layer(df):
    """
    Chart 1: Bar chart of average time per layer (TX and RX)
    Shows PDCP, RLC, MAC processing times
    """
    # Calculate averages
    avg_pdcp_tx = df['pdcp_tx_us'].mean()
    avg_rlc_tx = df['rlc_tx_us'].mean()
    avg_mac_tx = df['mac_tx_us'].mean()
    avg_mac_rx = df['mac_rx_us'].mean()
    avg_rlc_rx = df['rlc_rx_us'].mean()
    avg_pdcp_rx = df['pdcp_rx_us'].mean()
    
    layers = ['PDCP', 'RLC', 'MAC']
    tx_times = [avg_pdcp_tx, avg_rlc_tx, avg_mac_tx]
    rx_times = [avg_pdcp_rx, avg_rlc_rx, avg_mac_rx]
    
    x = np.arange(len(layers))
    width = 0.35
    
    fig, ax = plt.subplots(figsize=(10, 6))
    bars1 = ax.bar(x - width/2, tx_times, width, label='TX (Uplink)', color='#2E86AB')
    bars2 = ax.bar(x + width/2, rx_times, width, label='RX (Downlink)', color='#A23B72')
    
    ax.set_xlabel('Protocol Layer', fontsize=12)
    ax.set_ylabel('Average Time (microseconds)', fontsize=12)
    ax.set_title('Average Processing Time per Layer', fontsize=14, fontweight='bold')
    ax.set_xticks(x)
    ax.set_xticklabels(layers)
    ax.legend()
    ax.grid(axis='y', alpha=0.3)
    
    # Add value labels on bars
    for bars in [bars1, bars2]:
        for bar in bars:
            height = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2., height,
                   f'{height:.1f}',
                   ha='center', va='bottom', fontsize=9)
    
    plt.tight_layout()
    output_path = 'docs/charts/chart1_avg_time_per_layer.png'
    plt.savefig(output_path, dpi=300)
    print(f"✓ Chart 1 saved: {output_path}")
    plt.close()

def chart2_time_vs_tb_size(df):
    """
    Chart 2: Line chart of time vs TB size per layer
    Shows how each layer's processing time scales with transport block size
    """
    # Group by TB size and calculate means
    grouped = df.groupby('tb_size').agg({
        'pdcp_tx_us': 'mean',
        'rlc_tx_us': 'mean',
        'mac_tx_us': 'mean',
        'mac_rx_us': 'mean',
        'rlc_rx_us': 'mean',
        'pdcp_rx_us': 'mean'
    }).reset_index()
    
    # Sort by TB size
    grouped = grouped.sort_values('tb_size')
    
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))
    
    # TX plot
    ax1.plot(grouped['tb_size'], grouped['pdcp_tx_us'], marker='o', label='PDCP TX', linewidth=2)
    ax1.plot(grouped['tb_size'], grouped['rlc_tx_us'], marker='s', label='RLC TX', linewidth=2)
    ax1.plot(grouped['tb_size'], grouped['mac_tx_us'], marker='^', label='MAC TX', linewidth=2)
    ax1.set_xlabel('Transport Block Size (bytes)', fontsize=11)
    ax1.set_ylabel('Average Time (microseconds)', fontsize=11)
    ax1.set_title('TX Processing Time vs TB Size', fontsize=12, fontweight='bold')
    ax1.legend()
    ax1.grid(alpha=0.3)
    
    # RX plot
    ax2.plot(grouped['tb_size'], grouped['mac_rx_us'], marker='^', label='MAC RX', linewidth=2)
    ax2.plot(grouped['tb_size'], grouped['rlc_rx_us'], marker='s', label='RLC RX', linewidth=2)
    ax2.plot(grouped['tb_size'], grouped['pdcp_rx_us'], marker='o', label='PDCP RX', linewidth=2)
    ax2.set_xlabel('Transport Block Size (bytes)', fontsize=11)
    ax2.set_ylabel('Average Time (microseconds)', fontsize=11)
    ax2.set_title('RX Processing Time vs TB Size', fontsize=12, fontweight='bold')
    ax2.legend()
    ax2.grid(alpha=0.3)
    
    plt.tight_layout()
    output_path = 'docs/charts/chart2_time_vs_tb_size.png'
    plt.savefig(output_path, dpi=300)
    print(f"✓ Chart 2 saved: {output_path}")
    plt.close()

def chart3_time_vs_packet_size(df):
    """
    Chart 3: Line chart of time vs packet size per layer
    Shows how each layer's processing time scales with IP packet size
    """
    # Group by packet size and calculate means
    grouped = df.groupby('packet_size').agg({
        'pdcp_tx_us': 'mean',
        'rlc_tx_us': 'mean',
        'mac_tx_us': 'mean',
        'mac_rx_us': 'mean',
        'rlc_rx_us': 'mean',
        'pdcp_rx_us': 'mean'
    }).reset_index()
    
    # Sort by packet size
    grouped = grouped.sort_values('packet_size')
    
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))
    
    # TX plot
    ax1.plot(grouped['packet_size'], grouped['pdcp_tx_us'], marker='o', label='PDCP TX', linewidth=2)
    ax1.plot(grouped['packet_size'], grouped['rlc_tx_us'], marker='s', label='RLC TX', linewidth=2)
    ax1.plot(grouped['packet_size'], grouped['mac_tx_us'], marker='^', label='MAC TX', linewidth=2)
    ax1.set_xlabel('IP Packet Size (bytes)', fontsize=11)
    ax1.set_ylabel('Average Time (microseconds)', fontsize=11)
    ax1.set_title('TX Processing Time vs Packet Size', fontsize=12, fontweight='bold')
    ax1.legend()
    ax1.grid(alpha=0.3)
    
    # RX plot
    ax2.plot(grouped['packet_size'], grouped['mac_rx_us'], marker='^', label='MAC RX', linewidth=2)
    ax2.plot(grouped['packet_size'], grouped['rlc_rx_us'], marker='s', label='RLC RX', linewidth=2)
    ax2.plot(grouped['packet_size'], grouped['pdcp_rx_us'], marker='o', label='PDCP RX', linewidth=2)
    ax2.set_xlabel('IP Packet Size (bytes)', fontsize=11)
    ax2.set_ylabel('Average Time (microseconds)', fontsize=11)
    ax2.set_title('RX Processing Time vs Packet Size', fontsize=12, fontweight='bold')
    ax2.legend()
    ax2.grid(alpha=0.3)
    
    plt.tight_layout()
    output_path = 'docs/charts/chart3_time_vs_packet_size.png'
    plt.savefig(output_path, dpi=300)
    print(f"✓ Chart 3 saved: {output_path}")
    plt.close()

def print_summary_stats(df):
    """Print summary statistics to console"""
    print("\n" + "="*60)
    print("PROFILING SUMMARY")
    print("="*60)
    print(f"Total packets processed: {len(df)}")
    print(f"Packet sizes tested: {sorted(df['packet_size'].unique())}")
    print(f"TB sizes tested: {sorted(df['tb_size'].unique())}")
    print(f"Pass rate: {(df['pass'] == 'PASS').sum()}/{len(df)} ({100*(df['pass'] == 'PASS').mean():.1f}%)")
    print("\nAverage processing times (microseconds):")
    print(f"  PDCP TX: {df['pdcp_tx_us'].mean():.2f}")
    print(f"  RLC TX:  {df['rlc_tx_us'].mean():.2f}")
    print(f"  MAC TX:  {df['mac_tx_us'].mean():.2f}")
    print(f"  MAC RX:  {df['mac_rx_us'].mean():.2f}")
    print(f"  RLC RX:  {df['rlc_rx_us'].mean():.2f}")
    print(f"  PDCP RX: {df['pdcp_rx_us'].mean():.2f}")
    print(f"  Total:   {df['total_us'].mean():.2f}")
    print("="*60 + "\n")

def main():
    print("5G Layer 2 Profiling Chart Generator")
    print("=" * 60)
    
    # Load data
    df = load_profiling_data()
    
    # Ensure output directory exists
    ensure_output_dir()
    
    # Generate all three charts
    print("\nGenerating charts...")
    chart1_avg_time_per_layer(df)
    chart2_time_vs_tb_size(df)
    chart3_time_vs_packet_size(df)
    
    # Print summary
    print_summary_stats(df)
    
    print("All charts generated successfully!")
    print("Charts saved to: docs/charts/")

if __name__ == '__main__':
    main()
