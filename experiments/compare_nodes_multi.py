#!/usr/bin/env python3
"""
Aggregate multi-node comparison from a single summary CSV.

Outputs:
- CSV with per-load mean/min/max for selected metrics across all nodes.
- Optional plots (mean with min/max band) if matplotlib is available.

Usage:
  python experiments/compare_nodes_multi.py --summary experiments/analysis_outputs/<session>/summary_<session>.csv
"""

import argparse
import csv
from collections import defaultdict


def as_float(value):
    try:
        return float(value)
    except Exception:
        return 0.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--summary", required=True)
    parser.add_argument("--out", default=None)
    parser.add_argument("--plot-dir", default=None)
    args = parser.parse_args()

    metrics = [
        "cpu_mean",
        "cpu_p95",
        "stress_p95",
        "stress_high_ratio_pct",
        "exec_max_p95",
        "miss_p95",
        "telemetry_latency_p95_ms",
    ]

    by_load = defaultdict(list)
    with open(args.summary, "r", encoding="utf-8") as f:
        for row in csv.DictReader([line for line in f if not line.startswith("#")]):
            load = row["load"]
            by_load[load].append(row)

    lines = []
    header = ["load"]
    for m in metrics:
        header += [f"{m}_mean", f"{m}_min", f"{m}_max"]
    lines.append(",".join(header))

    for load in sorted(by_load.keys(), key=lambda x: int(x)):
        rows = by_load[load]
        parts = [str(load)]
        for m in metrics:
            vals = [as_float(r.get(m, 0)) for r in rows]
            if not vals:
                parts += ["0", "0", "0"]
            else:
                mean = sum(vals) / len(vals)
                parts += [f"{mean:.2f}", f"{min(vals):.2f}", f"{max(vals):.2f}"]
        lines.append(",".join(parts))

    out = args.out or (args.summary.replace("summary_", "compare_nodes_multi_"))
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    print(f"[done] wrote {out}")

    # Optional plots if matplotlib is available.
    try:
        import matplotlib.pyplot as plt
    except Exception:
        return

    plot_dir = args.plot_dir
    if plot_dir is None:
        plot_dir = str(out).rsplit("/", 1)[0]

    # Build data for plotting
    data = {}
    for line in lines[1:]:
        parts = line.split(",")
        load = float(parts[0])
        idx = 1
        for m in metrics:
            mean = float(parts[idx]); mn = float(parts[idx+1]); mx = float(parts[idx+2])
            data.setdefault(m, []).append((load, mean, mn, mx))
            idx += 3

    for m, points in data.items():
        points.sort(key=lambda x: x[0])
        loads = [p[0] for p in points]
        means = [p[1] for p in points]
        mins = [p[2] for p in points]
        maxs = [p[3] for p in points]

        plt.figure()
        plt.plot(loads, means, marker="o")
        plt.fill_between(loads, mins, maxs, alpha=0.2)
        plt.title(f"{m} (mean with min/max band)")
        plt.xlabel("Load")
        plt.ylabel(m)
        out_png = f"{plot_dir}/compare_nodes_multi_{m}.png"
        plt.savefig(out_png, dpi=150, bbox_inches="tight")
        plt.close()
        print(f"[plot] wrote {out_png}")


if __name__ == "__main__":
    main()
