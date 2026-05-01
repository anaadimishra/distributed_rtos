#!/usr/bin/env python3
"""
Analyze a delegation validation session.

Reads the telemetry JSONL and delegation_events JSON produced by
delegation_test.py and outputs:

  delegation_timeline.csv   — per-record per-node state snapshots
  delegation_summary.csv    — key metrics (handshake latency, duration, blocks)
  delegation_timeline.png   — state-transition + CPU + miss plot (matplotlib)

Usage:
  python experiments/analyze_delegation.py \\
    --log-file  experiments/analysis_outputs/delegation-bench__session_XYZ/session_XYZ.jsonl \\
    --events-file experiments/analysis_outputs/delegation-bench__session_XYZ/delegation_events_session_XYZ.json \\
    --out-dir   experiments/analysis_outputs/delegation-bench__session_XYZ
"""

import argparse
import csv
import json
import os
from collections import defaultdict

DELEG_STATE_INT = {
    "IDLE": 0,
    "REQUESTING": 1,
    "ACTIVE": 2,
    "HOSTING": 3,
}

# Events worth drawing as vertical markers on the timeline plot.
MARKER_EVENTS = {
    "stress_start",
    "deleg_requesting",
    "deleg_active",
    "deleg_hosting",
    "recovery_start",
    "deleg_idle_restored",
}


def metric_int(payload, key, default=0):
    try:
        return int(payload.get(key, default))
    except (TypeError, ValueError):
        return default


def effective_deleg_state(payload):
    state = payload.get("deleg_state", "IDLE")
    if state == "HOSTING" and metric_int(payload, "deleg_blocks") <= 0:
        return "IDLE"
    return state


def load_jsonl(path):
    rows = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--log-file", required=True,
                        help="Telemetry JSONL from the delegation session")
    parser.add_argument("--events-file", default=None,
                        help="delegation_events_*.json produced by delegation_test.py")
    parser.add_argument("--out-dir", default="experiments/analysis_outputs")
    parser.add_argument("--label", default="delegation")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    # ------------------------------------------------------------------ load
    rows = load_jsonl(args.log_file)
    print(f"[load] {len(rows)} telemetry records from {args.log_file}")

    events_data = {}
    if args.events_file and os.path.exists(args.events_file):
        with open(args.events_file, "r", encoding="utf-8") as f:
            events_data = json.load(f)
        print(f"[load] {len(events_data.get('events', []))} events from {args.events_file}")

    # --------------------------------------------------------- build timeline
    by_node = defaultdict(list)
    for r in rows:
        node = r.get("node_id")
        if not node:
            continue
        p = r.get("payload", {})
        deleg_state = effective_deleg_state(p)
        by_node[node].append({
            "node_id": node,
            "t_rx_ms": r.get("t_rx_ms", 0),
            "cpu": metric_int(p, "cpu"),
            "load": metric_int(p, "load"),
            "miss": metric_int(p, "miss"),
            "stress_level": metric_int(p, "stress_level"),
            "deleg_state": deleg_state,
            "deleg_state_int": DELEG_STATE_INT.get(deleg_state, 0),
            "deleg_peer": p.get("deleg_peer", ""),
            "deleg_blocks": metric_int(p, "deleg_blocks"),
            "deleg_dispatched": metric_int(p, "deleg_dispatched"),
            "deleg_returned": metric_int(p, "deleg_returned"),
            "active_blocks": metric_int(p, "blocks"),
            "eff_blocks": metric_int(p, "eff_blocks"),
            "state": p.get("state", "SCHEDULABLE"),
        })

    for node in by_node:
        by_node[node].sort(key=lambda r: r["t_rx_ms"])

    # Normalise timestamps: t=0 at first record across all nodes.
    all_t_ms = [r["t_rx_ms"] for records in by_node.values() for r in records]
    t0_ms = min(all_t_ms) if all_t_ms else 0
    t0_s = t0_ms / 1000.0   # used to align wall-clock event times

    for node in by_node:
        for r in by_node[node]:
            r["t_s"] = round((r["t_rx_ms"] - t0_ms) / 1000.0, 2)

    # ------------------------------------------------------ timeline CSV
    timeline_csv = os.path.join(args.out_dir, "delegation_timeline.csv")
    fieldnames = [
        "t_s", "node_id",
        "deleg_state", "deleg_state_int", "deleg_peer", "deleg_blocks",
        "deleg_dispatched", "deleg_returned",
        "cpu", "load", "miss", "stress_level",
        "active_blocks", "eff_blocks", "state",
    ]
    with open(timeline_csv, "w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for node in sorted(by_node.keys()):
            for r in by_node[node]:
                writer.writerow({k: r[k] for k in fieldnames})
    print(f"[done] wrote {timeline_csv}")

    # ------------------------------------------------------ per-node stats
    victim = events_data.get("actual_delegator") or events_data.get("victim", "")
    node_stats = {}
    for node, records in by_node.items():
        total = len(records)
        counts = defaultdict(int)
        for r in records:
            counts[r["deleg_state"]] += 1
        non_idle = total - counts["IDLE"]
        node_stats[node] = {
            "total_samples": total,
            "idle_samples": counts["IDLE"],
            "requesting_samples": counts["REQUESTING"],
            "active_samples": counts["ACTIVE"],
            "hosting_samples": counts["HOSTING"],
            "deleg_active_pct": round(100.0 * non_idle / total, 2) if total else 0,
            "max_deleg_blocks": max((r["deleg_blocks"] for r in records), default=0),
            "max_deleg_dispatched": max((r["deleg_dispatched"] for r in records), default=0),
            "max_deleg_returned": max((r["deleg_returned"] for r in records), default=0),
        }

    # ------------------------------------------------------ summary CSV
    summary = events_data.get("summary", {})
    summary_csv = os.path.join(args.out_dir, "delegation_summary.csv")
    fields = [
        "node_id", "role",
        "total_samples", "idle_samples",
        "requesting_samples", "active_samples", "hosting_samples",
        "deleg_active_pct", "max_deleg_blocks",
        "max_deleg_dispatched", "max_deleg_returned",
        "handshake_latency_ms", "time_to_delegate_ms",
        "delegation_duration_ms", "delegation_fired",
    ]
    with open(summary_csv, "w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for node in sorted(node_stats.keys()):
            role = "victim" if node == victim else "bystander"
            row = {**node_stats[node], "node_id": node, "role": role}
            if node == victim:
                row["handshake_latency_ms"] = summary.get("handshake_latency_ms", "")
                row["time_to_delegate_ms"] = summary.get("time_to_delegate_ms", "")
                row["delegation_duration_ms"] = summary.get("delegation_duration_ms", "")
                row["delegation_fired"] = summary.get("delegation_fired", "")
            else:
                row["handshake_latency_ms"] = ""
                row["time_to_delegate_ms"] = ""
                row["delegation_duration_ms"] = ""
                row["delegation_fired"] = ""
            row.setdefault("max_deleg_dispatched", node_stats[node]["max_deleg_dispatched"])
            row.setdefault("max_deleg_returned",   node_stats[node]["max_deleg_returned"])
            writer.writerow(row)
    print(f"[done] wrote {summary_csv}")

    # ------------------------------------------------------ print findings
    print("\n=== Delegation Analysis ===")
    for node in sorted(node_stats.keys()):
        s = node_stats[node]
        role = "VICTIM    " if node == victim else "BYSTANDER "
        print(
            f"  {role} {node}: "
            f"requesting={s['requesting_samples']}s  "
            f"active={s['active_samples']}s  "
            f"hosting={s['hosting_samples']}s  "
            f"max_blocks={s['max_deleg_blocks']}"
        )
    if summary.get("handshake_latency_ms") is not None:
        print(f"  Handshake latency:    {summary['handshake_latency_ms']}ms")
    if summary.get("time_to_delegate_ms") is not None:
        print(f"  Time to delegate:     {summary['time_to_delegate_ms']}ms")
    if summary.get("delegation_duration_ms") is not None:
        print(f"  Delegation duration:  {summary['delegation_duration_ms']}ms")

    # ------------------------------------------------------ plot
    try:
        import matplotlib.pyplot as plt

        NODE_COLORS = ["#4cc9f0", "#6be675", "#ff6b6b", "#f4d35e", "#c77dff"]
        sorted_nodes = sorted(by_node.keys())
        color_map = {n: NODE_COLORS[i % len(NODE_COLORS)] for i, n in enumerate(sorted_nodes)}

        fig, axes = plt.subplots(3, 1, figsize=(13, 9), sharex=True)
        fig.suptitle("Delegation Validation — State Timeline", fontsize=13)

        # Panel 1: delegation state (stacked offset per node for readability)
        ax = axes[0]
        for i, node in enumerate(sorted_nodes):
            records = by_node[node]
            ts = [r["t_s"] for r in records]
            ds = [r["deleg_state_int"] + i * 4.5 for r in records]
            ax.step(ts, ds, where="post", label=node, color=color_map[node], linewidth=1.5)
        ax.set_ylabel("Deleg State\n(0=IDLE 1=REQ 2=ACT 3=HOST)")
        ax.legend(fontsize=8, loc="upper right")

        # Panel 2: CPU per node
        ax = axes[1]
        for node in sorted_nodes:
            records = by_node[node]
            ts = [r["t_s"] for r in records]
            cpu = [r["cpu"] for r in records]
            ax.plot(ts, cpu, label=node, color=color_map[node], linewidth=1.5)
        ax.axhline(90, linestyle="--", color="orange", linewidth=1, label="Saturation (90%)")
        ax.axhline(70, linestyle=":", color="#888", linewidth=1, label="Headroom limit (70%)")
        ax.set_ylabel("CPU (%)")
        ax.legend(fontsize=8, loc="upper right")

        # Panel 3: deadline misses per node
        ax = axes[2]
        for node in sorted_nodes:
            records = by_node[node]
            ts = [r["t_s"] for r in records]
            miss = [r["miss"] for r in records]
            ax.step(ts, miss, where="post", label=node, color=color_map[node], linewidth=1.5)
        ax.set_ylabel("Deadline Misses")
        ax.set_xlabel("Time (s)")
        ax.legend(fontsize=8, loc="upper right")

        # Mark key events as vertical lines
        for ev in events_data.get("events", []):
            t_ev = ev.get("t")
            if t_ev is None:
                continue
            kind = ev.get("kind", "")
            if kind not in MARKER_EVENTS:
                continue
            t_rel = t_ev - t0_s
            for ax in axes:
                ax.axvline(t_rel, linestyle=":", linewidth=0.9, color="#aaaaaa", alpha=0.8)
            # Label on the top panel only
            ylim = axes[0].get_ylim()
            label_y = ylim[1] * 0.92
            axes[0].text(
                t_rel + 0.4, label_y,
                kind.replace("_", "\n"),
                fontsize=5.5, color="#cccccc", va="top",
            )

        plt.tight_layout()
        out_png = os.path.join(args.out_dir, "delegation_timeline.png")
        plt.savefig(out_png, dpi=150, bbox_inches="tight")
        plt.close()
        print(f"[plot] wrote {out_png}")

    except Exception as exc:
        print(f"[plot] skipped ({exc})")


if __name__ == "__main__":
    main()
