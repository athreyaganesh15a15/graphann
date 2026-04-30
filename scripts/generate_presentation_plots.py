#!/usr/bin/env python3
"""
Generate presentation-quality plots from SIFT1M benchmark results.
Reads the raw terminal output from run_sift1m_full.sh and produces plots.

Usage:
    python3 scripts/generate_presentation_plots.py

No codebase changes required — this is a standalone visualization tool.
"""

import re
import os
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.ticker import PercentFormatter
from pathlib import Path

# ─── Configuration ───────────────────────────────────────────────────────────
PLOT_DIR = Path(__file__).resolve().parent.parent / "plots" / "presentation"
PLOT_DIR.mkdir(parents=True, exist_ok=True)

# Colors — curated palette
C_EXACT    = "#3B82F6"  # blue
C_QUANT    = "#10B981"  # emerald
C_DYNAMIC  = "#F59E0B"  # amber
C_R32      = "#6366F1"  # indigo
C_R64      = "#EC4899"  # pink
C_MEM_FP32 = "#EF4444"  # red
C_MEM_Q    = "#22C55E"  # green

plt.rcParams.update({
    'font.family': 'sans-serif',
    'font.size': 11,
    'axes.titlesize': 13,
    'axes.titleweight': 'bold',
    'axes.labelsize': 12,
    'figure.facecolor': 'white',
    'axes.facecolor': '#FAFAFA',
    'axes.grid': True,
    'grid.alpha': 0.3,
    'figure.dpi': 150,
})


# ─── Data Parsing ────────────────────────────────────────────────────────────
def parse_search_block(text):
    """Parse a search results table from terminal output."""
    rows = []
    pattern = re.compile(
        r'\s+(\d+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)'
    )
    for line in text.split('\n'):
        m = pattern.match(line)
        if m:
            rows.append({
                'L': int(m.group(1)),
                'recall': float(m.group(2)),
                'dist_cmps': float(m.group(3)),
                'avg_lat': float(m.group(4)),
                'p99_lat': float(m.group(5)),
            })
    return rows


def parse_results_file(filepath):
    """Parse a full results file and return dict of mode -> rows."""
    if not os.path.exists(filepath):
        print(f"  [SKIP] {filepath} not found")
        return {}
    with open(filepath, 'r') as f:
        text = f.read()

    results = {}
    # Split by search mode headers
    blocks = re.split(r'===\s*(.*?)\s*===', text)
    i = 1
    while i < len(blocks) - 1:
        header = blocks[i].strip()
        body = blocks[i + 1]
        rows = parse_search_block(body)
        if rows:
            # Normalize header
            key = header.lower()
            if 'quantized' in key and 'dynamic' in key:
                mode = 'quant_dynamic'
            elif 'quantized' in key:
                mode = 'quantized'
            elif 'dynamic' in key:
                mode = 'exact_dynamic'
            elif 'exact' in key or 'float32' in key:
                mode = 'exact'
            elif 'search results' in key:
                # Plain "Search Results (K=10)" means exact float32
                mode = 'exact'
            else:
                mode = header
            results[mode] = rows
        i += 2
    return results


def extract_build_time(filepath):
    """Extract build time from results file."""
    if not os.path.exists(filepath):
        return None
    with open(filepath, 'r') as f:
        text = f.read()
    m = re.search(r'Total build time:\s*([\d.]+)\s*seconds', text)
    return float(m.group(1)) if m else None


# ─── Plotting Functions ──────────────────────────────────────────────────────
def plot_recall_vs_latency(r32, r64, filename="01_recall_vs_latency.png"):
    """Plot 1: Recall vs Avg Latency Pareto curves (the hero plot)."""
    fig, axes = plt.subplots(1, 2, figsize=(14, 5.5), sharey=False)

    for ax, data, R_label in [(axes[0], r32, "R=32"), (axes[1], r64, "R=64")]:
        if not data:
            ax.set_title(f"{R_label} — No Data")
            continue

        for mode, color, label, marker in [
            ('exact',         C_EXACT,   'Exact Float32',       'o'),
            ('quantized',     C_QUANT,   'Quantized ADC',       's'),
            ('quant_dynamic', C_DYNAMIC, 'Quant + Dynamic Beam', 'D'),
        ]:
            if mode in data:
                rows = data[mode]
                recalls = [r['recall'] for r in rows]
                lats = [r['avg_lat'] for r in rows]
                ax.plot(recalls, lats, marker=marker, color=color, label=label,
                        linewidth=2, markersize=7, alpha=0.9)
                # Annotate L values
                for r_row in rows:
                    if r_row['L'] in [10, 50, 100, 200]:
                        ax.annotate(f"L={r_row['L']}", (r_row['recall'], r_row['avg_lat']),
                                    textcoords="offset points", xytext=(8, 5),
                                    fontsize=8, alpha=0.7)

        ax.set_xlabel("Recall@10")
        ax.set_ylabel("Avg Latency (µs)")
        ax.set_title(f"{R_label} — Recall vs Latency")
        ax.legend(loc='upper left', fontsize=9)
        ax.set_xlim(0.4, 1.005)

    plt.tight_layout()
    path = PLOT_DIR / filename
    plt.savefig(path, bbox_inches='tight')
    plt.close()
    print(f"  ✓ {path.name}")


def plot_recall_vs_dist_cmps(r32, r64, filename="02_recall_vs_dist_cmps.png"):
    """Plot 2: Recall vs Distance Computations — shows recall preserved."""
    fig, axes = plt.subplots(1, 2, figsize=(14, 5.5))

    for ax, data, R_label in [(axes[0], r32, "R=32"), (axes[1], r64, "R=64")]:
        if not data:
            ax.set_title(f"{R_label} — No Data")
            continue
        for mode, color, label, marker in [
            ('exact',         C_EXACT,   'Exact Float32',       'o'),
            ('quantized',     C_QUANT,   'Quantized ADC',       's'),
            ('quant_dynamic', C_DYNAMIC, 'Quant + Dynamic Beam', 'D'),
        ]:
            if mode in data:
                rows = data[mode]
                ax.plot([r['recall'] for r in rows],
                        [r['dist_cmps'] for r in rows],
                        marker=marker, color=color, label=label,
                        linewidth=2, markersize=7, alpha=0.9)
        ax.set_xlabel("Recall@10")
        ax.set_ylabel("Avg Distance Computations")
        ax.set_title(f"{R_label} — Recall vs Distance Computations")
        ax.legend(loc='upper left', fontsize=9)

    plt.tight_layout()
    path = PLOT_DIR / filename
    plt.savefig(path, bbox_inches='tight')
    plt.close()
    print(f"  ✓ {path.name}")


def plot_latency_speedup_bars(r32, filename="03_latency_speedup_bars.png"):
    """Plot 3: Latency at fixed recall targets as grouped bars."""
    if not r32:
        return

    fig, ax = plt.subplots(figsize=(10, 5.5))
    target_Ls = [20, 50, 100, 200]
    modes = [
        ('exact',         C_EXACT,   'Exact Float32'),
        ('quantized',     C_QUANT,   'Quantized ADC'),
        ('quant_dynamic', C_DYNAMIC, 'Quant + Dynamic Beam'),
    ]

    x = np.arange(len(target_Ls))
    width = 0.25

    for i, (mode, color, label) in enumerate(modes):
        if mode not in r32:
            continue
        lats = []
        for L in target_Ls:
            row = next((r for r in r32[mode] if r['L'] == L), None)
            lats.append(row['avg_lat'] if row else 0)
        bars = ax.bar(x + i * width, lats, width, label=label, color=color,
                      edgecolor='white', linewidth=0.5)
        for bar, val in zip(bars, lats):
            if val > 0:
                ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 15,
                        f'{val:.0f}', ha='center', va='bottom', fontsize=8)

    ax.set_xlabel("Search List Size (L)")
    ax.set_ylabel("Avg Latency (µs)")
    ax.set_title("R=32 — Latency by Search Mode at Each L")
    ax.set_xticks(x + width)
    ax.set_xticklabels([f"L={L}" for L in target_Ls])
    ax.legend(fontsize=9)

    plt.tight_layout()
    path = PLOT_DIR / filename
    plt.savefig(path, bbox_inches='tight')
    plt.close()
    print(f"  ✓ {path.name}")


def plot_p99_latency(r32, r64, filename="04_p99_tail_latency.png"):
    """Plot 4: P99 Tail Latency comparison."""
    fig, axes = plt.subplots(1, 2, figsize=(14, 5.5))

    for ax, data, R_label in [(axes[0], r32, "R=32"), (axes[1], r64, "R=64")]:
        if not data:
            ax.set_title(f"{R_label} — No Data")
            continue
        for mode, color, label, marker in [
            ('exact',         C_EXACT,   'Exact Float32',       'o'),
            ('quantized',     C_QUANT,   'Quantized ADC',       's'),
            ('quant_dynamic', C_DYNAMIC, 'Quant + Dynamic Beam', 'D'),
        ]:
            if mode in data:
                rows = data[mode]
                ax.plot([r['recall'] for r in rows],
                        [r['p99_lat'] for r in rows],
                        marker=marker, color=color, label=label,
                        linewidth=2, markersize=7, alpha=0.9)
        ax.set_xlabel("Recall@10")
        ax.set_ylabel("P99 Latency (µs)")
        ax.set_title(f"{R_label} — Tail Latency (P99)")
        ax.legend(loc='upper left', fontsize=9)

    plt.tight_layout()
    path = PLOT_DIR / filename
    plt.savefig(path, bbox_inches='tight')
    plt.close()
    print(f"  ✓ {path.name}")


def plot_r32_vs_r64_pareto(r32, r64, filename="05_r32_vs_r64_pareto.png"):
    """Plot 5: R=32 vs R=64 on same axes (quantized mode)."""
    fig, ax = plt.subplots(figsize=(9, 6))

    for data, R_label, color, marker in [
        (r32, "R=32", C_R32, 'o'),
        (r64, "R=64", C_R64, 's'),
    ]:
        if not data or 'quantized' not in data:
            continue
        rows = data['quantized']
        recalls = [r['recall'] for r in rows]
        lats = [r['avg_lat'] for r in rows]
        ax.plot(recalls, lats, marker=marker, color=color,
                label=f"{R_label} Quantized ADC", linewidth=2.5, markersize=8)
        for r_row in rows:
            if r_row['L'] in [10, 20, 50, 100, 200]:
                ax.annotate(f"L={r_row['L']}", (r_row['recall'], r_row['avg_lat']),
                            textcoords="offset points", xytext=(8, 5),
                            fontsize=8, alpha=0.7)

    ax.set_xlabel("Recall@10")
    ax.set_ylabel("Avg Latency (µs)")
    ax.set_title("R=32 vs R=64 — Recall-Latency Pareto (Quantized ADC)")
    ax.legend(fontsize=10)
    ax.set_xlim(0.75, 1.005)

    plt.tight_layout()
    path = PLOT_DIR / filename
    plt.savefig(path, bbox_inches='tight')
    plt.close()
    print(f"  ✓ {path.name}")


def plot_equivalent_recall_bars(r32, r64, filename="06_equivalent_recall.png"):
    """Plot 6: At equivalent recall, compare latency of baseline vs best."""
    fig, axes = plt.subplots(1, 2, figsize=(12, 5))

    # Target 1: ~0.98 recall
    # Target 2: ~0.995+ recall
    targets = [
        ("~98% Recall", 0.98),
        ("~99.5% Recall", 0.995),
    ]

    for ax, (label, target) in zip(axes, targets):
        configs = []
        latencies = []
        p99s = []
        colors = []

        for data, R_label, c in [(r32, "R=32", C_R32), (r64, "R=64", C_R64)]:
            if not data:
                continue
            for mode, mode_label in [('exact', 'Exact'), ('quantized', 'Quant')]:
                if mode not in data:
                    continue
                # Find closest row to target recall
                best = min(data[mode], key=lambda r: abs(r['recall'] - target))
                if abs(best['recall'] - target) < 0.02:
                    configs.append(f"{R_label}\n{mode_label}\nL={best['L']}")
                    latencies.append(best['avg_lat'])
                    p99s.append(best['p99_lat'])
                    colors.append(c if mode == 'exact' else
                                  (C_QUANT if R_label == "R=32" else C_DYNAMIC))

        if configs:
            bars = ax.bar(configs, latencies, color=colors, edgecolor='white', width=0.6)
            for bar, val in zip(bars, latencies):
                ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 10,
                        f'{val:.0f}µs', ha='center', fontsize=9, fontweight='bold')
            ax.set_ylabel("Avg Latency (µs)")
            ax.set_title(f"Latency at {label}")

    plt.tight_layout()
    path = PLOT_DIR / filename
    plt.savefig(path, bbox_inches='tight')
    plt.close()
    print(f"  ✓ {path.name}")


def plot_memory_footprint(filename="07_memory_footprint.png"):
    """Plot 7: Memory usage — Float32 vs Quantized."""
    fig, ax = plt.subplots(figsize=(7, 5))

    labels = ['Float32\n(Original)', 'Quantized\n(uint8 + metadata)']
    sizes = [488, 122]
    colors = [C_MEM_FP32, C_MEM_Q]

    bars = ax.bar(labels, sizes, color=colors, edgecolor='white',
                  width=0.5, linewidth=1.5)
    for bar, val in zip(bars, sizes):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 8,
                f'{val} MB', ha='center', fontsize=13, fontweight='bold')

    ax.set_ylabel("Memory (MB)")
    ax.set_title("Dataset Memory Footprint — SIFT1M (1M × 128d)")
    ax.set_ylim(0, 560)

    # Add reduction annotation
    ax.annotate("75% reduction", xy=(1, 122), xytext=(1.3, 300),
                fontsize=12, fontweight='bold', color=C_MEM_Q,
                arrowprops=dict(arrowstyle='->', color=C_MEM_Q, lw=2))

    plt.tight_layout()
    path = PLOT_DIR / filename
    plt.savefig(path, bbox_inches='tight')
    plt.close()
    print(f"  ✓ {path.name}")


def plot_build_time(r32_file, r64_file, filename="08_build_time.png"):
    """Plot 8: Build time comparison R=32 vs R=64."""
    fig, ax = plt.subplots(figsize=(7, 5))

    t32 = extract_build_time(r32_file)
    t64 = extract_build_time(r64_file)

    labels = []
    times = []
    colors = []

    if t32 is not None:
        labels.append("R=32\n(L=75, α=1.1)")
        times.append(t32)
        colors.append(C_R32)
    if t64 is not None:
        labels.append("R=64\n(L=100, α=1.2)")
        times.append(t64)
        colors.append(C_R64)

    if not times:
        print("  [SKIP] No build time data found")
        return

    bars = ax.bar(labels, times, color=colors, edgecolor='white', width=0.45)
    for bar, val in zip(bars, times):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 3,
                f'{val:.1f}s', ha='center', fontsize=13, fontweight='bold')

    ax.set_ylabel("Build Time (seconds)")
    ax.set_title("Index Build Time — SIFT1M (1M × 128d)")

    plt.tight_layout()
    path = PLOT_DIR / filename
    plt.savefig(path, bbox_inches='tight')
    plt.close()
    print(f"  ✓ {path.name}")


# ─── Main ────────────────────────────────────────────────────────────────────
def main():
    root = Path(__file__).resolve().parent.parent
    r32_file = root / "r32_results.txt"
    r64_file = root / "r64_results.txt"

    print("Parsing R=32 results...")
    r32 = parse_results_file(r32_file)
    print(f"  Modes found: {list(r32.keys())}")

    print("Parsing R=64 results...")
    r64 = parse_results_file(r64_file)
    print(f"  Modes found: {list(r64.keys())}")

    # Also try parsing dynamic-only results if they exist
    r32_dyn_file = root / "r32_dynamic_results.txt"
    r64_dyn_file = root / "r64_dynamic_results.txt"
    if os.path.exists(r32_dyn_file):
        dyn = parse_results_file(r32_dyn_file)
        for k, v in dyn.items():
            r32[f"exact_dynamic"] = v
    if os.path.exists(r64_dyn_file):
        dyn = parse_results_file(r64_dyn_file)
        for k, v in dyn.items():
            r64[f"exact_dynamic"] = v

    print(f"\nGenerating plots in {PLOT_DIR}/\n")

    plot_recall_vs_latency(r32, r64)
    plot_recall_vs_dist_cmps(r32, r64)
    plot_latency_speedup_bars(r32)
    plot_p99_latency(r32, r64)
    plot_r32_vs_r64_pareto(r32, r64)
    plot_equivalent_recall_bars(r32, r64)
    plot_memory_footprint()
    plot_build_time(r32_file, r64_file)

    print(f"\n✅ All plots saved to {PLOT_DIR}/")


if __name__ == '__main__':
    main()
