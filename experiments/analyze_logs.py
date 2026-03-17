#!/usr/bin/env python3
"""
Analyze telemetry JSONL logs and produce summary + plots.

Usage:
  python experiments/analyze_logs.py --run experiments/last_run.json \
    --log-dir /Volumes/GAIA_PRIME/devn/distributed-rtos/dashboard/telemetry_logs

  python experiments/analyze_logs.py --log-file experiments/last_run.jsonl

Outputs:
- CSV summary per node + load step
- Optional PNG plots if matplotlib is available
"""

import argparse
import json
import os
import statistics


def load_jsonl(path):
    rows = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rows.append(json.loads(line))
    return rows


def p95(values):
    if not values:
        return 0
    values = sorted(values)
    idx = int(0.95 * (len(values) - 1))
    return values[idx]

def stddev(values):
    if len(values) <= 1:
        return 0
    return statistics.pstdev(values)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run", default="experiments/last_run.json")
    parser.add_argument("--log-dir", default="/Volumes/GAIA_PRIME/devn/distributed-rtos/dashboard/telemetry_logs")
    parser.add_argument("--log-file", default=None)
    parser.add_argument("--out-dir", default="experiments/analysis_outputs")
    parser.add_argument("--out-csv", default=None)
    parser.add_argument("--skip-seconds", type=int, default=2)
    parser.add_argument("--window-ready-only", action="store_true")
    parser.add_argument("--label", default="run")
    parser.add_argument("--index-file", default="experiments/analysis_outputs/index.csv")
    args = parser.parse_args()

    if args.log_file:
        log_path = args.log_file
        session_id = os.path.splitext(os.path.basename(log_path))[0]
    else:
        with open(args.run, "r", encoding="utf-8") as f:
            run = json.load(f)
        session_id = run["session_id"]
        log_path = os.path.join(args.log_dir, f"{session_id}.jsonl")

    if not os.path.exists(log_path):
        raise SystemExit(f"Log file not found: {log_path}")

    rows = load_jsonl(log_path)

    # Index by node
    by_node = {}
    for r in rows:
        node = r.get("node_id")
        by_node.setdefault(node, []).append(r)

    # If no run schedule is available, bucket by payload.load.
    steps = []
    if args.log_file:
        loads = sorted({r.get("payload", {}).get("load") for r in rows})
        loads = [l for l in loads if l is not None]
        for load in loads:
            steps.append({
                "load": load,
                "t_start_ms": None,
                "t_end_ms": None,
            })
    else:
        steps = []
        for step in run["steps"]:
            steps.append({
                "load": step["load"],
                "t_start_ms": int(step["t_start"] * 1000),
                "t_end_ms": int(step["t_end"] * 1000),
            })

    summaries = []
    for step in steps:
        load = step["load"]
        t_start_ms = step["t_start_ms"]
        t_end_ms = step["t_end_ms"]

        for node, records in by_node.items():
            if t_start_ms is None:
                window = [r for r in records if r.get("payload", {}).get("load") == load]
                if window and args.skip_seconds > 0:
                    first_ts = min(r.get("t_rx_ms", 0) for r in window)
                    cutoff = first_ts + (args.skip_seconds * 1000)
                    window = [r for r in window if r.get("t_rx_ms", 0) >= cutoff]
            else:
                cutoff = t_start_ms + (args.skip_seconds * 1000)
                window = [r for r in records if cutoff <= r.get("t_rx_ms", 0) <= t_end_ms]
            if not window:
                continue
            if args.window_ready_only:
                window = [r for r in window if r.get("payload", {}).get("window_ready", 0)]
                if not window:
                    continue

            cpu = [r["payload"].get("cpu", 0) for r in window]
            queue = [r["payload"].get("queue", 0) for r in window]
            miss = [r["payload"].get("miss", 0) for r in window]
            exec_max = [r["payload"].get("exec_max", 0) for r in window]
            eff_blocks = [r["payload"].get("eff_blocks", 0) for r in window]
            ctrl_lat = [r.get("ctrl_latency_ms", 0) for r in window]
            telem_lat = [r.get("telemetry_latency_ms", 0) for r in window]

            cpu_mean = statistics.mean(cpu)
            cpu_std = stddev(cpu)
            volatility_score = round(cpu_std, 2)
            warning_flags = []
            if p95(miss) > 0:
                warning_flags.append("miss")
            if p95(queue) > 0:
                warning_flags.append("queue")
            if p95(cpu) >= 90:
                warning_flags.append("cpu_saturation")
            warning = "|".join(warning_flags) if warning_flags else "ok"

            summaries.append({
                "node_id": node,
                "load": load,
                "cpu_mean": round(cpu_mean, 2),
                "cpu_p95": p95(cpu),
                "cpu_std": round(cpu_std, 2),
                "volatility_score": volatility_score,
                "queue_mean": round(statistics.mean(queue), 2),
                "queue_p95": p95(queue),
                "eff_blocks_mean": round(statistics.mean(eff_blocks), 2),
                "exec_max_p95": p95(exec_max),
                "miss_p95": p95(miss),
                "ctrl_latency_p95_ms": p95(ctrl_lat),
                "telemetry_latency_p95_ms": p95(telem_lat),
                "warning": warning,
            })

    out_dir = os.path.join(args.out_dir, f"{args.label}__{session_id}")
    os.makedirs(out_dir, exist_ok=True)
    out_csv = args.out_csv or os.path.join(out_dir, f"summary_{session_id}.csv")
    with open(out_csv, "w", encoding="utf-8") as f:
        headers = [
            "node_id",
            "load",
            "cpu_mean",
            "cpu_p95",
            "cpu_std",
            "volatility_score",
            "queue_mean",
            "queue_p95",
            "eff_blocks_mean",
            "exec_max_p95",
            "miss_p95",
            "ctrl_latency_p95_ms",
            "telemetry_latency_p95_ms",
            "warning",
        ]
        f.write(",".join(headers) + "\n")
        for row in summaries:
            f.write(",".join(str(row[h]) for h in headers) + "\n")

    print(f"[done] wrote {out_csv}")

    # Append to index for easier navigation.
    try:
        os.makedirs(os.path.dirname(args.index_file), exist_ok=True)
        new_row = {
            "session_id": session_id,
            "label": args.label,
            "log_path": log_path,
            "summary_csv": out_csv,
        }
        write_header = not os.path.exists(args.index_file)
        with open(args.index_file, "a", encoding="utf-8") as f:
            if write_header:
                f.write(",".join(new_row.keys()) + "\n")
            f.write(",".join(new_row.values()) + "\n")
    except Exception:
        pass

    # Optional plotting if matplotlib is available.
    try:
        import matplotlib.pyplot as plt
    except Exception:
        print("[plot] matplotlib not available; skipping plots")
        return

    # Plot CPU mean vs load per node
    nodes = sorted({r["node_id"] for r in summaries})
    for node in nodes:
        node_rows = [r for r in summaries if r["node_id"] == node]
        node_rows.sort(key=lambda r: (r["load"], r["node_id"]))
        loads = [r["load"] for r in node_rows]
        cpu_means = [r["cpu_mean"] for r in node_rows]
        plt.figure()
        plt.plot(loads, cpu_means, marker="o")
        plt.title(f"CPU Mean vs Load ({node})")
        plt.xlabel("Load")
        plt.ylabel("CPU Mean (%)")
        out_png = os.path.join(out_dir, f"cpu_vs_load_{session_id}_{node}.png")
        plt.savefig(out_png, dpi=150, bbox_inches="tight")
        plt.close()
        print(f"[plot] wrote {out_png}")

    # Plot queue p95 vs load per node
    for node in nodes:
        node_rows = [r for r in summaries if r["node_id"] == node]
        node_rows.sort(key=lambda r: (r["load"], r["node_id"]))
        loads = [r["load"] for r in node_rows]
        queue_p95 = [r["queue_p95"] for r in node_rows]
        plt.figure()
        plt.plot(loads, queue_p95, marker="o")
        plt.title(f"Queue p95 vs Load ({node})")
        plt.xlabel("Load")
        plt.ylabel("Queue p95 (depth)")
        out_png = os.path.join(out_dir, f"queue_p95_{session_id}_{node}.png")
        plt.savefig(out_png, dpi=150, bbox_inches="tight")
        plt.close()
        print(f"[plot] wrote {out_png}")

    # Plot effective blocks mean vs load per node
    for node in nodes:
        node_rows = [r for r in summaries if r["node_id"] == node]
        node_rows.sort(key=lambda r: (r["load"], r["node_id"]))
        loads = [r["load"] for r in node_rows]
        eff_mean = [r["eff_blocks_mean"] for r in node_rows]
        plt.figure()
        plt.plot(loads, eff_mean, marker="o")
        plt.title(f"Eff Blocks Mean vs Load ({node})")
        plt.xlabel("Load")
        plt.ylabel("Effective Blocks Mean")
        out_png = os.path.join(out_dir, f"eff_blocks_mean_{session_id}_{node}.png")
        plt.savefig(out_png, dpi=150, bbox_inches="tight")
        plt.close()
        print(f"[plot] wrote {out_png}")

    # Plot exec_max p95 vs load per node
    for node in nodes:
        node_rows = [r for r in summaries if r["node_id"] == node]
        node_rows.sort(key=lambda r: (r["load"], r["node_id"]))
        loads = [r["load"] for r in node_rows]
        exec_p95 = [r["exec_max_p95"] for r in node_rows]
        plt.figure()
        plt.plot(loads, exec_p95, marker="o")
        plt.title(f"Exec Max p95 vs Load ({node})")
        plt.xlabel("Load")
        plt.ylabel("Exec Max p95 (ticks)")
        out_png = os.path.join(out_dir, f"exec_max_p95_{session_id}_{node}.png")
        plt.savefig(out_png, dpi=150, bbox_inches="tight")
        plt.close()
        print(f"[plot] wrote {out_png}")

    # Plot miss p95 vs load per node
    for node in nodes:
        node_rows = [r for r in summaries if r["node_id"] == node]
        node_rows.sort(key=lambda r: (r["load"], r["node_id"]))
        loads = [r["load"] for r in node_rows]
        miss_p95 = [r["miss_p95"] for r in node_rows]
        plt.figure()
        plt.plot(loads, miss_p95, marker="o")
        plt.title(f"Deadline Miss p95 vs Load ({node})")
        plt.xlabel("Load")
        plt.ylabel("Miss p95 (count)")
        out_png = os.path.join(out_dir, f"miss_p95_{session_id}_{node}.png")
        plt.savefig(out_png, dpi=150, bbox_inches="tight")
        plt.close()
        print(f"[plot] wrote {out_png}")

    # Plot control latency p95 vs load (first node)
    if nodes:
        node = nodes[0]
        node_rows = [r for r in summaries if r["node_id"] == node]
        node_rows.sort(key=lambda r: (r["load"], r["node_id"]))
        loads = [r["load"] for r in node_rows]
        ctrl = [r["ctrl_latency_p95_ms"] for r in node_rows]
        plt.figure()
        plt.plot(loads, ctrl, marker="o")
        plt.title(f"Control Latency p95 vs Load ({node})")
        plt.xlabel("Load")
        plt.ylabel("Control Latency p95 (ms)")
        out_png = os.path.join(out_dir, f"ctrl_p95_{session_id}_{node}.png")
        plt.savefig(out_png, dpi=150, bbox_inches="tight")
        plt.close()
        print(f"[plot] wrote {out_png}")

    # Plot telemetry latency p95 vs load (first node)
    if nodes:
        node = nodes[0]
        node_rows = [r for r in summaries if r["node_id"] == node]
        node_rows.sort(key=lambda r: (r["load"], r["node_id"]))
        loads = [r["load"] for r in node_rows]
        telem = [r["telemetry_latency_p95_ms"] for r in node_rows]
        plt.figure()
        plt.plot(loads, telem, marker="o")
        plt.title(f"Telemetry Latency p95 vs Load ({node})")
        plt.xlabel("Load")
        plt.ylabel("Telemetry Latency p95 (ms)")
        out_png = os.path.join(out_dir, f"telem_p95_{session_id}_{node}.png")
        plt.savefig(out_png, dpi=150, bbox_inches="tight")
        plt.close()
        print(f"[plot] wrote {out_png}")


if __name__ == "__main__":
    main()
