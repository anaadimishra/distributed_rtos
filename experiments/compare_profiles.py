#!/usr/bin/env python3
"""
Compare two analysis summaries (e.g., profiling on vs off).

Usage:
  python experiments/compare_profiles.py --base experiments/analysis_outputs/session_A/summary_session_A.csv \
    --test experiments/analysis_outputs/session_B/summary_session_B.csv
"""

import argparse
import csv


def load_summary(path):
    rows = {}
    with open(path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            key = (row["node_id"], row["load"])
            rows[key] = row
    return rows


def as_float(value):
    try:
        return float(value)
    except Exception:
        return 0.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", required=True, help="Baseline summary CSV")
    parser.add_argument("--test", required=True, help="Test summary CSV")
    args = parser.parse_args()

    base = load_summary(args.base)
    test = load_summary(args.test)

    keys = sorted(set(base.keys()) & set(test.keys()))
    if not keys:
        raise SystemExit("No overlapping node/load entries to compare.")

    fields = [
        "cpu_mean",
        "cpu_p95",
        "cpu_std",
        "queue_p95",
        "eff_blocks_mean",
        "exec_max_p95",
        "miss_p95",
        "telemetry_latency_p95_ms",
    ]

    print("node_id,load," + ",".join(f"{f}_base,{f}_test,{f}_delta" for f in fields))
    for key in keys:
        b = base[key]
        t = test[key]
        node_id, load = key
        parts = [node_id, load]
        for f in fields:
            bval = as_float(b.get(f, 0))
            tval = as_float(t.get(f, 0))
            delta = tval - bval
            parts.extend([f"{bval:.2f}", f"{tval:.2f}", f"{delta:.2f}"])
        print(",".join(parts))


if __name__ == "__main__":
    main()
