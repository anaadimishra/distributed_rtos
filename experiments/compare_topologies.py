#!/usr/bin/env python3
"""
Compare overload threshold across topologies (single-node vs two-node).

Definition:
  overload threshold = first load where miss_p95 > 0

Hypothesis:
  Shared Wi-Fi contention increases MQTT publish latency under load, which delays
  the manager_task publish cycle, which in turn causes the compute_task deadline
  window to be evaluated against a slightly stale epoch - compounding real
  execution jitter with measurement jitter.

TODO:
  Run the same topology-threshold test with a wired broker (Ethernet) to isolate
  whether the threshold shift is network-induced or compute-interference-induced.
"""

import argparse
import csv
import glob
import os
from collections import defaultdict


def resolve_summary(path_or_dir):
    if os.path.isfile(path_or_dir):
        return path_or_dir
    matches = glob.glob(os.path.join(path_or_dir, "summary_*.csv"))
    if not matches:
        raise SystemExit(f"No summary_*.csv found in {path_or_dir}")
    return matches[0]


def load_summary(path_or_dir):
    path = resolve_summary(path_or_dir)
    rows = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            if line.startswith("#"):
                continue
            rows.append(line)
    return list(csv.DictReader(rows)), path


def threshold_from_rows(rows):
    by_load = defaultdict(list)
    for row in rows:
        try:
            load = int(row["load"])
            miss = float(row.get("miss_p95", 0))
        except Exception:
            continue
        by_load[load].append(miss)

    for load in sorted(by_load.keys()):
        miss_mean = sum(by_load[load]) / len(by_load[load])
        if miss_mean > 0:
            return load
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--single-node-session", required=True)
    parser.add_argument("--two-node-session", required=True)
    parser.add_argument("--out-dir", default="experiments/analysis_outputs")
    args = parser.parse_args()

    single_rows, single_path = load_summary(args.single_node_session)
    two_rows, two_path = load_summary(args.two_node_session)

    single_thr = threshold_from_rows(single_rows)
    two_thr = threshold_from_rows(two_rows)

    single_sid = os.path.splitext(os.path.basename(single_path))[0].replace("summary_", "")
    two_sid = os.path.splitext(os.path.basename(two_path))[0].replace("summary_", "")

    out_dir = os.path.join(args.out_dir, f"topology_compare_{single_sid}_vs_{two_sid}")
    os.makedirs(out_dir, exist_ok=True)

    out_csv = os.path.join(out_dir, "overload_thresholds.csv")
    with open(out_csv, "w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=["topology", "summary_csv", "threshold_load_first_miss_p95_gt_0"],
        )
        writer.writeheader()
        writer.writerow({
            "topology": "1-node",
            "summary_csv": single_path,
            "threshold_load_first_miss_p95_gt_0": single_thr if single_thr is not None else "none",
        })
        writer.writerow({
            "topology": "2-node",
            "summary_csv": two_path,
            "threshold_load_first_miss_p95_gt_0": two_thr if two_thr is not None else "none",
        })
    print(f"[done] wrote {out_csv}")

    try:
        import matplotlib.pyplot as plt
    except Exception:
        print("[plot] matplotlib not available; skipping plot")
        return

    labels = ["1-node", "2-node"]
    values = [single_thr or 0, two_thr or 0]
    plt.figure()
    bars = plt.bar(labels, values)
    plt.ylabel("Overload Threshold Load")
    plt.title("Overload threshold: 1-node vs 2-node")
    for bar, val in zip(bars, values):
        plt.text(bar.get_x() + bar.get_width() / 2, bar.get_height(), str(val),
                 ha="center", va="bottom")
    out_png = os.path.join(out_dir, "overload_threshold_1node_vs_2node.png")
    plt.savefig(out_png, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"[plot] wrote {out_png}")


if __name__ == "__main__":
    main()
