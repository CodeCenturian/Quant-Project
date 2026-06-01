#!/usr/bin/env python3
"""
report.py — generate latency charts from lobe_bench output CSV

Usage:
    python3 report.py latency_results.csv

Output:
    latency_report.png  — bar chart of p50/p95/p99/p99.9 per scenario
    latency_cdf.png     — CDF (cumulative distribution) for all scenarios
"""

import sys
import csv
import numpy as np
import matplotlib
matplotlib.use('Agg')  # non-interactive backend (works without a display)
import matplotlib.pyplot as plt
import os

# ── Load CSV ──────────────────────────────────────────────────────────────────
def load(path):
    data = {"insert_ns": [], "match_ns": [], "mixed_ns": []}
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            for key in data:
                val = row.get(key, "").strip()
                if val:
                    data[key].append(int(val))
    return {k: np.array(v) for k, v in data.items() if v}

# ── Percentile bar chart ──────────────────────────────────────────────────────
def plot_percentiles(data, out_path):
    scenarios = {
        "Insert\n(no match)": data["insert_ns"],
        "Match\nheavy":       data["match_ns"],
        "Realistic\nmix":     data["mixed_ns"],
    }
    pcts   = [50, 95, 99, 99.9]
    colors = ["#4CAF50", "#FFC107", "#FF5722", "#F44336"]
    labels = ["P50", "P95", "P99", "P99.9"]

    fig, ax = plt.subplots(figsize=(10, 6))
    fig.patch.set_facecolor("#1e1e2e")
    ax.set_facecolor("#1e1e2e")

    n_scenarios = len(scenarios)
    n_pcts      = len(pcts)
    bar_width   = 0.18
    x           = np.arange(n_scenarios)

    for i, (p, color, label) in enumerate(zip(pcts, colors, labels)):
        values = [np.percentile(arr, p) for arr in scenarios.values()]
        offset = (i - n_pcts / 2 + 0.5) * bar_width
        bars = ax.bar(x + offset, values, bar_width,
                      label=label, color=color, alpha=0.88, zorder=3)
        for bar, val in zip(bars, values):
            ax.text(bar.get_x() + bar.get_width() / 2,
                    bar.get_height() + 5,
                    f"{val:.0f}", ha="center", va="bottom",
                    fontsize=8, color="white")

    ax.set_xticks(x)
    ax.set_xticklabels(list(scenarios.keys()), color="white", fontsize=11)
    ax.set_ylabel("Latency (nanoseconds)", color="white")
    ax.set_title("LOBE — Order Processing Latency by Scenario", color="white", fontsize=13)
    ax.yaxis.label.set_color("white")
    ax.tick_params(colors="white")
    ax.spines[:].set_color("#444")
    ax.grid(axis="y", color="#333", zorder=0)
    ax.legend(facecolor="#2e2e3e", labelcolor="white")

    plt.tight_layout()
    plt.savefig(out_path, dpi=150, facecolor=fig.get_facecolor())
    print(f"Saved: {out_path}")
    plt.close()

# ── CDF plot ──────────────────────────────────────────────────────────────────
def plot_cdf(data, out_path):
    fig, ax = plt.subplots(figsize=(10, 6))
    fig.patch.set_facecolor("#1e1e2e")
    ax.set_facecolor("#1e1e2e")

    configs = [
        ("insert_ns", "#4CAF50", "Insert only"),
        ("match_ns",  "#FFC107", "Match heavy"),
        ("mixed_ns",  "#2196F3", "Realistic mix"),
    ]

    for key, color, label in configs:
        arr  = np.sort(data[key])
        # Clip to p99.9 so extreme outliers don't distort the chart
        clip = np.percentile(arr, 99.9)
        arr  = arr[arr <= clip]
        cdf  = np.arange(1, len(arr) + 1) / len(arr) * 100
        ax.plot(arr, cdf, color=color, label=label, linewidth=2)

    ax.set_xlabel("Latency (ns)", color="white")
    ax.set_ylabel("Percentile (%)", color="white")
    ax.set_title("LOBE — Latency CDF (clipped at P99.9)", color="white", fontsize=13)
    ax.tick_params(colors="white")
    ax.spines[:].set_color("#444")
    ax.grid(color="#333")
    ax.legend(facecolor="#2e2e3e", labelcolor="white")

    plt.tight_layout()
    plt.savefig(out_path, dpi=150, facecolor=fig.get_facecolor())
    print(f"Saved: {out_path}")
    plt.close()

# ── Summary table ─────────────────────────────────────────────────────────────
def print_summary(data):
    print(f"\n{'Scenario':<22} {'P50':>8} {'P95':>8} {'P99':>8} {'P99.9':>10}  (ns)")
    print("─" * 62)
    names = {"insert_ns": "Insert only", "match_ns": "Match heavy", "mixed_ns": "Realistic mix"}
    for key, name in names.items():
        arr = data[key]
        print(f"{name:<22} {np.percentile(arr,50):>8.0f} {np.percentile(arr,95):>8.0f}"
              f" {np.percentile(arr,99):>8.0f} {np.percentile(arr,99.9):>10.0f}")

# ── Entry point ───────────────────────────────────────────────────────────────
if __name__ == "__main__":
    csv_path = sys.argv[1] if len(sys.argv) > 1 else "latency_results.csv"
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found. Run ./lobe_bench first.")
        sys.exit(1)

    data = load(csv_path)
    print_summary(data)

    out_dir = os.path.dirname(os.path.abspath(csv_path))
    plot_percentiles(data, os.path.join(out_dir, "latency_report.png"))
    plot_cdf(data,         os.path.join(out_dir, "latency_cdf.png"))
    print("\nDone. Add latency_report.png to your README.")
