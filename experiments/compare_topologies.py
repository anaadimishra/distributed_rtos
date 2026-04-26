#!/usr/bin/env python3
"""
Compare overload/saturation thresholds and CPU scaling across topologies.

Accepts any number of topology sessions via repeatable --session arguments:

  python experiments/compare_topologies.py \
    --session "1-node:experiments/analysis_outputs/one-node-bench__session_XYZ" \
    --session "2-node:experiments/analysis_outputs/two-node-bench__session_XYZ" \
    --session "5-node:experiments/analysis_outputs/five-node-bench__session_XYZ"

Definitions:
  saturation threshold = first load where cpu_p95 >= 90 (across any node)
  overload threshold   = first load where miss_p95 > 0  (across any node, averaged)

Outputs per run:
  - overload_thresholds.csv             — saturation + overload threshold per topology
  - cpu_by_load.csv                     — mean CPU across nodes at each load step per topology
  - overload_threshold_compare.png      — grouped bar chart of saturation/overload thresholds
  - profile_cpu_mean.png
  - profile_cpu_p95.png
  - profile_miss_p95.png
  - profile_exec_max_p95.png
  - profile_drift_p95.png
  - profile_queue_p95.png
  - profile_telemetry_latency_p95_ms.png
"""

import argparse
import csv
import glob
import os
import statistics
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


def session_id_from_path(path):
    return os.path.splitext(os.path.basename(path))[0].replace("summary_", "")


PROFILE_FIELDS = [
    "cpu_mean",
    "cpu_p95",
    "stress_p95",
    "stress_high_ratio_pct",
    "miss_p95",
    "exec_max_p95",
    "drift_p95",
    "queue_p95",
    "telemetry_latency_p95_ms",
]


def compute_metrics(rows):
    """
    For each load step, aggregate across all nodes (mean across nodes per field).
    cpu_p95_max is kept separately for saturation threshold detection.
    Returns dict keyed by load (int).
    """
    by_load = defaultdict(lambda: {f: [] for f in PROFILE_FIELDS + ["cpu_p95_raw"]})
    for row in rows:
        try:
            load = int(row["load"])
        except Exception:
            continue
        for f in PROFILE_FIELDS:
            try:
                by_load[load][f].append(float(row.get(f, 0) or 0))
            except Exception:
                pass
        try:
            by_load[load]["cpu_p95_raw"].append(float(row.get("cpu_p95", 0) or 0))
        except Exception:
            pass

    result = {}
    for load, vals in by_load.items():
        entry = {}
        for f in PROFILE_FIELDS:
            entry[f] = round(statistics.mean(vals[f]), 2) if vals[f] else 0
        entry["cpu_p95_max"] = max(vals["cpu_p95_raw"]) if vals["cpu_p95_raw"] else 0
        # keep miss_mean alias for threshold detection
        entry["miss_mean"] = entry["miss_p95"]
        result[load] = entry
    return result


def saturation_threshold(metrics):
    """First load where any node's cpu_p95 >= 90."""
    for load in sorted(metrics.keys()):
        if metrics[load]["cpu_p95_max"] >= 90:
            return load
    return None


def overload_threshold(metrics):
    """First load where mean miss_p95 across nodes > 0."""
    for load in sorted(metrics.keys()):
        if metrics[load]["miss_mean"] > 0:
            return load
    return None


def parse_session_arg(s):
    """Parse 'label:path' or 'label=path' into (label, path)."""
    for sep in ("=", ":"):
        parts = s.split(sep, 1)
        if len(parts) == 2:
            return parts[0].strip(), parts[1].strip()
    raise SystemExit(f"--session must be in 'label:path' or 'label=path' format, got: {s!r}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--session",
        action="append",
        default=[],
        metavar="LABEL:PATH",
        help="Topology label and session directory, e.g. '1-node:experiments/analysis_outputs/one-node-bench__session_XYZ'. Repeatable.",
    )
    parser.add_argument("--out-dir", default="experiments/analysis_outputs")
    parser.add_argument("--label", default="topology_compare")
    args = parser.parse_args()

    if not args.session:
        raise SystemExit("Provide at least one --session LABEL:PATH")

    topologies = []
    session_ids = []
    for s in args.session:
        label, path = parse_session_arg(s)
        rows, summary_path = load_summary(path)
        metrics = compute_metrics(rows)
        sat_thr = saturation_threshold(metrics)
        ovl_thr = overload_threshold(metrics)
        sid = session_id_from_path(summary_path)
        session_ids.append(sid)
        topologies.append({
            "label": label,
            "summary_path": summary_path,
            "session_id": sid,
            "metrics": metrics,
            "saturation_threshold": sat_thr,
            "overload_threshold": ovl_thr,
        })
        print(f"[{label}] session={sid} saturation={sat_thr} overload={ovl_thr}")

    out_label = args.label + "_" + "_".join(session_ids)
    out_dir = os.path.join(args.out_dir, out_label)
    os.makedirs(out_dir, exist_ok=True)

    # --- Threshold CSV ---
    thr_csv = os.path.join(out_dir, "overload_thresholds.csv")
    with open(thr_csv, "w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=["topology", "session_id", "saturation_threshold", "overload_threshold", "summary_csv"],
        )
        writer.writeheader()
        for t in topologies:
            writer.writerow({
                "topology": t["label"],
                "session_id": t["session_id"],
                "saturation_threshold": t["saturation_threshold"] if t["saturation_threshold"] is not None else "none",
                "overload_threshold": t["overload_threshold"] if t["overload_threshold"] is not None else "none",
                "summary_csv": t["summary_path"],
            })
    print(f"[done] wrote {thr_csv}")

    # --- CPU by load CSV ---
    all_loads = sorted({load for t in topologies for load in t["metrics"].keys()})
    cpu_csv = os.path.join(out_dir, "cpu_by_load.csv")
    with open(cpu_csv, "w", encoding="utf-8", newline="") as f:
        fieldnames = ["load"] + [t["label"] + "_cpu_mean" for t in topologies]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for load in all_loads:
            row = {"load": load}
            for t in topologies:
                val = t["metrics"].get(load, {}).get("cpu_mean", "")
                row[t["label"] + "_cpu_mean"] = val
            writer.writerow(row)
    print(f"[done] wrote {cpu_csv}")

    # --- Plots ---
    try:
        import matplotlib.pyplot as plt
    except Exception:
        print("[plot] matplotlib not available; skipping plots")
        return

    # Bar chart: saturation + overload thresholds per topology
    labels = [t["label"] for t in topologies]
    sat_vals = [t["saturation_threshold"] or 0 for t in topologies]
    ovl_vals = [t["overload_threshold"] or 0 for t in topologies]

    x = range(len(labels))
    width = 0.35
    fig, ax = plt.subplots()
    bars_sat = ax.bar([i - width / 2 for i in x], sat_vals, width, label="Saturation (cpu≥90)")
    bars_ovl = ax.bar([i + width / 2 for i in x], ovl_vals, width, label="Overload (miss>0)")
    for bar, val in zip(bars_sat, sat_vals):
        if val:
            ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 5, str(val),
                    ha="center", va="bottom", fontsize=9)
    for bar, val in zip(bars_ovl, ovl_vals):
        if val:
            ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 5, str(val),
                    ha="center", va="bottom", fontsize=9)
    ax.set_xticks(list(x))
    ax.set_xticklabels(labels)
    ax.set_ylabel("Load")
    ax.set_title("Saturation & Overload Thresholds by Topology")
    ax.legend()
    out_png = os.path.join(out_dir, "overload_threshold_compare.png")
    plt.savefig(out_png, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"[plot] wrote {out_png}")

    # Profile plots: one line per topology, one plot per metric
    profile_specs = [
        ("cpu_mean",                  "profile_cpu_mean.png",                 "CPU Mean (%)",              90,      "Saturation (90%)"),
        ("cpu_p95",                   "profile_cpu_p95.png",                  "CPU p95 (%)",               90,      "Saturation (90%)"),
        ("stress_p95",                "profile_stress_p95.png",               "Stress p95 (0/1/2)",       None,    None),
        ("stress_high_ratio_pct",     "profile_stress_high_ratio_pct.png",    "HIGH Stress Ratio (%)",    None,    None),
        ("miss_p95",                  "profile_miss_p95.png",                 "Deadline Miss p95 (count)", None,    None),
        ("exec_max_p95",              "profile_exec_max_p95.png",             "Exec Max p95 (ticks)",      None,    None),
        ("drift_p95",                 "profile_drift_p95.png",                "Manager Drift p95 (ms)",    None,    None),
        ("queue_p95",                 "profile_queue_p95.png",                "Queue Depth p95",           None,    None),
        ("telemetry_latency_p95_ms",  "profile_telemetry_latency_p95_ms.png", "Telemetry Latency p95 (ms)", None,  None),
    ]

    for field, filename, ylabel, hline_val, hline_label in profile_specs:
        plt.figure()
        for t in topologies:
            loads = sorted(t["metrics"].keys())
            vals = [t["metrics"][l].get(field, 0) for l in loads]
            plt.plot(loads, vals, marker="o", label=t["label"])
        if hline_val is not None:
            plt.axhline(hline_val, linestyle="--", linewidth=1, color="orange", label=hline_label)
        plt.xlabel("Load")
        plt.ylabel(ylabel)
        plt.title(f"{ylabel} vs Load by Topology")
        plt.legend()
        out_png = os.path.join(out_dir, filename)
        plt.savefig(out_png, dpi=150, bbox_inches="tight")
        plt.close()
        print(f"[plot] wrote {out_png}")


if __name__ == "__main__":
    main()
