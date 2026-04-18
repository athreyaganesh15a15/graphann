#!/usr/bin/env python3
"""
Visualization script for Vamana experiments.
Reads CSV results and generates plots.
"""

import sys
import csv
import matplotlib.pyplot as plt
import numpy as np
from collections import defaultdict

def read_results(csv_file):
    """Read experiment results from CSV."""
    results = defaultdict(list)
    with open(csv_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            exp_name = row['experiment_name']
            results[exp_name].append(row)
    return results

def plot_beam_width(results, output_prefix):
    """Plot beam width experiment results."""
    if not results:
        return

    L_values = []
    recalls = []
    build_times = []
    avg_degrees = []

    for r in sorted(results, key=lambda x: int(x['variant'].split('=')[1])):
        L_values.append(int(r['variant'].split('=')[1]))
        recalls.append(float(r['avg_recall']))
        build_times.append(float(r['build_time_sec']))
        avg_degrees.append(float(r['avg_degree']))

    fig, axes = plt.subplots(1, 3, figsize=(15, 4))

    # Recall vs L
    axes[0].plot(L_values, recalls, 'o-', linewidth=2, markersize=8)
    axes[0].set_xlabel('L (Search List Size)', fontsize=12)
    axes[0].set_ylabel('Recall@10', fontsize=12)
    axes[0].set_title('Recall vs Beam Width (L)', fontsize=13, fontweight='bold')
    axes[0].grid(True, alpha=0.3)

    # Build time vs L
    axes[1].plot(L_values, build_times, 's-', color='orange', linewidth=2, markersize=8)
    axes[1].set_xlabel('L (Search List Size)', fontsize=12)
    axes[1].set_ylabel('Build Time (seconds)', fontsize=12)
    axes[1].set_title('Build Time vs Beam Width (L)', fontsize=13, fontweight='bold')
    axes[1].grid(True, alpha=0.3)

    # Average degree vs L
    axes[2].plot(L_values, avg_degrees, '^-', color='green', linewidth=2, markersize=8)
    axes[2].set_xlabel('L (Search List Size)', fontsize=12)
    axes[2].set_ylabel('Average Out-Degree', fontsize=12)
    axes[2].set_title('Average Degree vs Beam Width (L)', fontsize=13, fontweight='bold')
    axes[2].grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(f'{output_prefix}_beam_width.png', dpi=150, bbox_inches='tight')
    print(f"Saved: {output_prefix}_beam_width.png")
    plt.close()

def plot_medoid_start(results, output_prefix):
    """Plot medoid vs random start comparison."""
    if not results:
        return

    variants = [r['variant'] for r in results]
    recalls = [float(r['avg_recall']) for r in results]
    latencies = [float(r['avg_latency_us']) for r in results]

    fig, axes = plt.subplots(1, 2, figsize=(10, 4))

    colors = ['lightblue', 'lightcoral']
    axes[0].bar(variants, recalls, color=colors, edgecolor='black', linewidth=1.5)
    axes[0].set_ylabel('Recall@10', fontsize=12)
    axes[0].set_title('Recall: Random vs Medoid Start', fontsize=13, fontweight='bold')
    axes[0].grid(True, alpha=0.3, axis='y')

    axes[1].bar(variants, latencies, color=colors, edgecolor='black', linewidth=1.5)
    axes[1].set_ylabel('Latency (μs)', fontsize=12)
    axes[1].set_title('Latency: Random vs Medoid Start', fontsize=13, fontweight='bold')
    axes[1].grid(True, alpha=0.3, axis='y')

    plt.tight_layout()
    plt.savefig(f'{output_prefix}_medoid_start.png', dpi=150, bbox_inches='tight')
    print(f"Saved: {output_prefix}_medoid_start.png")
    plt.close()

def plot_multipass(results, output_prefix):
    """Plot multi-pass build results."""
    if not results:
        return

    passes = [r['variant'] for r in results]
    recalls = [float(r['avg_recall']) for r in results]
    avg_degrees = [float(r['avg_degree']) for r in results]

    fig, axes = plt.subplots(1, 2, figsize=(10, 4))

    colors = ['lightblue', 'lightgreen']
    axes[0].bar(passes, recalls, color=colors, edgecolor='black', linewidth=1.5)
    axes[0].set_ylabel('Recall@10', fontsize=12)
    axes[0].set_title('Recall: Single Pass vs Multi-Pass Build', fontsize=13, fontweight='bold')
    axes[0].grid(True, alpha=0.3, axis='y')

    axes[1].bar(passes, avg_degrees, color=colors, edgecolor='black', linewidth=1.5)
    axes[1].set_ylabel('Average Out-Degree', fontsize=12)
    axes[1].set_title('Graph Density: Single vs Multi-Pass', fontsize=13, fontweight='bold')
    axes[1].grid(True, alpha=0.3, axis='y')

    plt.tight_layout()
    plt.savefig(f'{output_prefix}_multipass.png', dpi=150, bbox_inches='tight')
    print(f"Saved: {output_prefix}_multipass.png")
    plt.close()

def plot_degree_analysis(results, output_prefix):
    """Plot degree distribution for different alpha values."""
    if not results:
        return

    alpha_values = []
    recalls = []
    avg_degrees = []
    build_times = []

    for r in sorted(results, key=lambda x: float(x['variant'].split('=')[1])):
        alpha_values.append(float(r['variant'].split('=')[1]))
        recalls.append(float(r['avg_recall']))
        avg_degrees.append(float(r['avg_degree']))
        build_times.append(float(r['build_time_sec']))

    fig, axes = plt.subplots(1, 3, figsize=(15, 4))

    # Recall vs Alpha
    axes[0].plot(alpha_values, recalls, 'o-', linewidth=2, markersize=8, color='purple')
    axes[0].set_xlabel('Alpha (α)', fontsize=12)
    axes[0].set_ylabel('Recall@10', fontsize=12)
    axes[0].set_title('Recall vs Alpha (RNG Pruning)', fontsize=13, fontweight='bold')
    axes[0].grid(True, alpha=0.3)

    # Average degree vs Alpha
    axes[1].plot(alpha_values, avg_degrees, 's-', linewidth=2, markersize=8, color='red')
    axes[1].set_xlabel('Alpha (α)', fontsize=12)
    axes[1].set_ylabel('Average Out-Degree', fontsize=12)
    axes[1].set_title('Graph Density vs Alpha', fontsize=13, fontweight='bold')
    axes[1].grid(True, alpha=0.3)

    # Build time vs Alpha
    axes[2].plot(alpha_values, build_times, '^-', linewidth=2, markersize=8, color='green')
    axes[2].set_xlabel('Alpha (α)', fontsize=12)
    axes[2].set_ylabel('Build Time (seconds)', fontsize=12)
    axes[2].set_title('Build Time vs Alpha', fontsize=13, fontweight='bold')
    axes[2].grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(f'{output_prefix}_degree_analysis.png', dpi=150, bbox_inches='tight')
    print(f"Saved: {output_prefix}_degree_analysis.png")
    plt.close()

def plot_search_optimization(results, output_prefix):
    """Plot search optimization results."""
    if not results:
        return

    methods = [r['variant'] for r in results]
    latencies = [float(r['avg_latency_us']) for r in results]

    fig, ax = plt.subplots(figsize=(8, 5))

    colors = ['lightblue', 'lightgreen']
    bars = ax.bar(methods, latencies, color=colors, edgecolor='black', linewidth=1.5)

    ax.set_ylabel('Average Latency (μs)', fontsize=12)
    ax.set_title('Search Optimization: Scratch Buffer vs Normal', fontsize=13, fontweight='bold')
    ax.grid(True, alpha=0.3, axis='y')

    # Add value labels on bars
    for bar, val in zip(bars, latencies):
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2., height,
                f'{val:.2f}',
                ha='center', va='bottom', fontsize=11, fontweight='bold')

    plt.tight_layout()
    plt.savefig(f'{output_prefix}_search_optimization.png', dpi=150, bbox_inches='tight')
    print(f"Saved: {output_prefix}_search_optimization.png")
    plt.close()

def main():
    if len(sys.argv) < 2:
        print("Usage: python visualize_experiments.py <results.csv> [output_prefix]")
        sys.exit(1)

    csv_file = sys.argv[1]
    output_prefix = sys.argv[2] if len(sys.argv) > 2 else 'plots'

    print(f"Reading results from {csv_file}...")
    results = read_results(csv_file)

    if 'beam_width' in results:
        print("Plotting beam width experiment...")
        plot_beam_width(results['beam_width'], output_prefix)

    if 'medoid_start' in results:
        print("Plotting medoid start experiment...")
        plot_medoid_start(results['medoid_start'], output_prefix)

    if 'multipass_build' in results:
        print("Plotting multi-pass build experiment...")
        plot_multipass(results['multipass_build'], output_prefix)

    if 'degree_analysis' in results:
        print("Plotting degree analysis experiment...")
        plot_degree_analysis(results['degree_analysis'], output_prefix)

    if 'search_optimization' in results:
        print("Plotting search optimization experiment...")
        plot_search_optimization(results['search_optimization'], output_prefix)

    print("Done!")

if __name__ == '__main__':
    main()
