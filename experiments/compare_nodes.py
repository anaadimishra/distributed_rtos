#!/usr/bin/env python3
"""
Compare two nodes in a single summary CSV.

Usage:
  python experiments/compare_nodes.py --summary experiments/analysis_outputs/<session>/summary_<session>.csv \
    --node-a node-AAAAAA --node-b node-BBBBBB
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
    parser.add_argument("--summary", required=True)
    parser.add_argument("--node-a", required=True)
    parser.add_argument("--node-b", required=True)
    args = parser.parse_args()

    rows = load_summary(args.summary)
    keys = sorted({k[1] for k in rows.keys() if k[0] in (args.node_a, args.node_b)})
    if not keys:
        raise SystemExit("No matching node/load rows found.")

    fields = [
        "cpu_mean",
        "cpu_p95",
        "exec_max_p95",
        "miss_p95",
        "telemetry_latency_p95_ms",
    ]

    header = ["load"]
    for f in fields:
        header += [f"{f}_{args.node_a}", f"{f}_{args.node_b}", f"{f}_delta"]
    print(",".join(header))

    for load in keys:
        row_a = rows.get((args.node_a, load))
        row_b = rows.get((args.node_b, load))
        if not row_a or not row_b:
            continue
        parts = [str(load)]
        for f in fields:
            a = as_float(row_a.get(f, 0))
            b = as_float(row_b.get(f, 0))
            parts += [f"{a:.2f}", f"{b:.2f}", f"{(b - a):.2f}"]
        print(",".join(parts))


if __name__ == "__main__":
    main()
