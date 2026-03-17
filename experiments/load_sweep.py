#!/usr/bin/env python3
"""
Load sweep controller.

- Restarts logging session on the dashboard.
- Applies load steps to all currently visible nodes.
- Records a schedule file for later analysis.

Usage:
  python experiments/load_sweep.py --base-url http://localhost:5000 \
    --min-load 100 --max-load 1000 --step 100 --hold-seconds 10
"""

import argparse
import json
import time
import shutil
import urllib.request
import urllib.error
import os

def http_get_json(url):
    with urllib.request.urlopen(url, timeout=5) as resp:
        return json.loads(resp.read().decode("utf-8"))


def http_post_json(url, payload):
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=5) as resp:
        return json.loads(resp.read().decode("utf-8"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="http://localhost:5000")
    parser.add_argument("--min-load", type=int, default=100)
    parser.add_argument("--max-load", type=int, default=1000)
    parser.add_argument("--step", type=int, default=100)
    parser.add_argument("--hold-seconds", type=int, default=20)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--label", default="run")
    parser.add_argument("--out", default="experiments/last_run.json")
    parser.add_argument(
        "--log-dir",
        default="/Volumes/GAIA_PRIME/devn/distributed-rtos/dashboard/telemetry_logs",
    )
    args = parser.parse_args()

    base = args.base_url.rstrip("/")

    runs = []
    for idx in range(1, args.repeat + 1):
        # Start a new logging session.
        session = http_post_json(f"{base}/api/logging/restart", {})
        session_id = session.get("session_id", "unknown")
        print(f"[session] {session_id} ({idx}/{args.repeat})")

        # Pull initial state to discover nodes (wait for telemetry to arrive).
        nodes = []
        deadline = time.time() + 20
        while time.time() < deadline:
            state = http_get_json(f"{base}/api/state")
            nodes = sorted(state.keys())
            if nodes:
                break
            time.sleep(1)
        if not nodes:
            raise SystemExit("No nodes detected in /api/state after waiting 20s")
        print(f"[nodes] {nodes}")

        steps = []
        load = args.min_load
        while load <= args.max_load:
            print(f"[load] setting load={load} for {args.hold_seconds}s")
            t_start = time.time()
            for node in nodes:
                http_post_json(f"{base}/api/control", {"node": node, "load": load})
            time.sleep(args.hold_seconds)
            t_end = time.time()
            steps.append({"load": load, "t_start": t_start, "t_end": t_end})
            load += args.step

        # Save run metadata for the analysis script.
        run = {
            "session_id": session_id,
            "label": args.label,
            "base_url": base,
            "nodes": nodes,
            "hold_seconds": args.hold_seconds,
            "steps": steps,
        }
        run_out = args.out
        if args.repeat > 1:
            root, ext = os.path.splitext(args.out)
            run_out = f"{root}_{idx}{ext or '.json'}"
        with open(run_out, "w", encoding="utf-8") as f:
            json.dump(run, f, indent=2)

        # Move the dashboard log into an experiment folder for this session.
        src_log = os.path.join(args.log_dir, f"{session_id}.jsonl")
        dst_dir = os.path.join("experiments", f"{args.label}__{session_id}")
        dst_log = os.path.join(dst_dir, f"{session_id}.jsonl")
        if os.path.exists(src_log):
            os.makedirs(dst_dir, exist_ok=True)
            shutil.move(src_log, dst_log)
            print(f"[logs] moved {src_log} -> {dst_log}")
        else:
            print(f"[logs] log not found at {src_log}")

        print(f"[done] wrote {run_out}")
        runs.append(run)


if __name__ == "__main__":
    try:
        main()
    except urllib.error.URLError as exc:
        raise SystemExit(f"HTTP error: {exc}")
