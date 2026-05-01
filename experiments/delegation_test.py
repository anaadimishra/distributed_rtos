#!/usr/bin/env python3
"""
Delegation validation test.

Orchestrates a controlled stress-asymmetry experiment:
  1. Warmup   — all nodes at low load to establish a stable baseline.
  2. Stress   — one "victim" node pushed to high load, others held low.
  3. Wait     — poll until delegation fires (deleg_state != IDLE on victim).
  4. Hold     — sustain the asymmetry to capture stable delegation data.
  5. Recovery — all nodes return to low load; wait for all to reach IDLE.
  6. Save     — write event log + telemetry JSONL then run analyze_delegation.

Usage:
  python experiments/delegation_test.py \\
    --base-url http://localhost:5000 \\
    --high-load 950 \\
    --low-load 400 \\
    --hold-seconds 40 \\
    --label delegation-bench

  # Pin the victim node explicitly:
  python experiments/delegation_test.py --victim node-AABBCC ...
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.request
import urllib.error

VICTIM_STRESS_CPU_THRESHOLD = 85


def http_get_json(url):
    with urllib.request.urlopen(url, timeout=5) as resp:
        return json.loads(resp.read().decode("utf-8"))


def http_post_json(url, payload):
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        url, data=data, headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req, timeout=5) as resp:
        return json.loads(resp.read().decode("utf-8"))


def set_load(base, node, load):
    http_post_json(f"{base}/api/control", {"node": node, "load": load})


def reboot_node(base, node):
    http_post_json(f"{base}/api/control", {"node": node, "action": "REBOOT"})


def metric_int(state, key, default=0):
    try:
        return int(state.get(key, default) or default)
    except (TypeError, ValueError):
        return default


def wait_for_nodes(base, min_count, timeout=60, settle_seconds=8):
    deadline = time.time() + timeout
    best_nodes = []
    first_min_count_t = None
    last_growth_t = None

    while time.time() < deadline:
        state = http_get_json(f"{base}/api/state")
        nodes = sorted(state.keys())

        if len(nodes) > len(best_nodes):
            best_nodes = nodes
            last_growth_t = time.time()
            print(f"[nodes] discovered {len(best_nodes)} node(s): {best_nodes}")

        if len(best_nodes) >= min_count and first_min_count_t is None:
            first_min_count_t = time.time()
            if last_growth_t is None:
                last_growth_t = first_min_count_t
            print(
                f"[nodes] reached min_count={min_count}; waiting {settle_seconds}s for late joiners..."
            )

        if first_min_count_t is not None and last_growth_t is not None:
            if (time.time() - last_growth_t) >= settle_seconds:
                return best_nodes

        time.sleep(1)

    state = http_get_json(f"{base}/api/state")
    nodes = sorted(state.keys())
    if len(nodes) > len(best_nodes):
        best_nodes = nodes
    if not best_nodes:
        raise SystemExit(f"No nodes detected after {timeout}s")
    if len(best_nodes) < min_count:
        raise SystemExit(
            f"Only {len(best_nodes)} node(s) detected after {timeout}s; "
            f"need at least {min_count}. Seen: {best_nodes}"
        )
    return best_nodes


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="http://localhost:5000")
    parser.add_argument("--high-load", type=int, default=950,
                        help="Load to apply to the victim node (default 950)")
    parser.add_argument("--low-load", type=int, default=400,
                        help="Load to hold bystander nodes at (default 400)")
    parser.add_argument("--warmup-seconds", type=int, default=30,
                        help="Seconds at low load before introducing stress")
    parser.add_argument("--hold-seconds", type=int, default=40,
                        help="Seconds to sustain the asymmetry after delegation fires")
    parser.add_argument("--drain-seconds", type=int, default=30,
                        help="After victim load drops, seconds to watch TCP channels drain to IDLE")
    parser.add_argument("--recovery-seconds", type=int, default=30,
                        help="Seconds to wait for all nodes to return to IDLE after full reset")
    parser.add_argument("--delegation-timeout", type=int, default=60,
                        help="Seconds to wait for delegation before giving up")
    parser.add_argument("--stress-precheck-seconds", type=int, default=15,
                        help="Seconds after stress start before warning if victim is not stressed")
    parser.add_argument("--crash-host-after", type=int, default=0,
                        help="Seconds after delegation ACTIVE before auto-crashing the host node. "
                             "0 = no crash (normal test). Requires hold_seconds > crash_host_after + ~60s.")
    parser.add_argument("--victim", default=None,
                        help="Node ID to stress. Auto-selects first node if omitted.")
    parser.add_argument("--min-nodes", type=int, default=2,
                        help="Minimum number of nodes required before test starts (default 2)")
    parser.add_argument("--nodes-timeout", type=int, default=90,
                        help="Seconds to wait for node discovery (default 90)")
    parser.add_argument("--nodes-settle-seconds", type=int, default=8,
                        help="After reaching min nodes, wait this long for late joiners (default 8)")
    parser.add_argument("--label", default="delegation-bench")
    parser.add_argument("--out-dir", default="experiments/analysis_outputs")
    parser.add_argument(
        "--log-dir",
        default="/Volumes/GAIA_PRIME/devn/distributed-rtos/dashboard/telemetry_logs",
    )
    args = parser.parse_args()
    base = args.base_url.rstrip("/")

    # ------------------------------------------------------------------ session
    session = http_post_json(f"{base}/api/logging/restart", {})
    session_id = session.get("session_id", "unknown")
    print(f"[session] {session_id}")

    # ------------------------------------------------------------------ nodes
    print("[nodes] waiting for nodes...")
    nodes = wait_for_nodes(
        base,
        min_count=args.min_nodes,
        timeout=args.nodes_timeout,
        settle_seconds=args.nodes_settle_seconds,
    )
    print(f"[nodes] {nodes}")
    if len(nodes) < args.min_nodes:
        raise SystemExit(
            f"Need \u2265{args.min_nodes} nodes for delegation test, found {len(nodes)}"
        )

    victim = (
        args.victim
        if args.victim and args.victim in nodes
        else nodes[0]
    )
    bystanders = [n for n in nodes if n != victim]
    print(f"[roles] victim={victim}  bystanders={bystanders}")

    events = []
    snapshots = []

    def snap(phase):
        state = http_get_json(f"{base}/api/state")
        snapshots.append({"t": time.time(), "phase": phase, "state": state})
        return state

    def log_event(kind, detail=None):
        entry = {"t": time.time(), "kind": kind}
        if detail:
            entry.update(detail)
        events.append(entry)
        suffix = f" {detail}" if detail else ""
        print(f"[event] {kind}{suffix}")

    # ---------------------------------------------------------------- warmup
    print(f"[warmup] all nodes -> load={args.low_load} for {args.warmup_seconds}s")
    for n in nodes:
        set_load(base, n, args.low_load)
    log_event("warmup_start", {"load": args.low_load})
    time.sleep(args.warmup_seconds)
    log_event("warmup_end")

    # --------------------------------------------------------------- stress
    print(f"[stress] victim={victim} -> {args.high_load}  bystanders -> {args.low_load}")
    set_load(base, victim, args.high_load)
    for n in bystanders:
        set_load(base, n, args.low_load)
    log_event("stress_start", {
        "victim": victim,
        "high_load": args.high_load,
        "low_load": args.low_load,
    })
    t_stress_start = time.time()

    # ------------------------------------------------- wait for delegation
    # Watch ALL nodes — whichever reaches high stress first becomes the
    # actual delegator regardless of which one was assigned high load.
    print(f"[wait] polling for delegation (timeout={args.delegation_timeout}s)...")
    t_requesting = None
    t_active = None
    t_hosting = None
    actual_delegator = None
    actual_host = None
    active_peer_at_crash = None
    delegation_fired = False
    stale_hosts_logged = set()
    victim_precheck_warned = False
    victim_ever_stressed = False
    victim_max_cpu = 0
    victim_max_stress = 0
    victim_max_eff_blocks = 0
    victim_max_miss = 0

    deadline = time.time() + args.delegation_timeout
    while time.time() < deadline:
        state = snap("stress")
        victim_state = state.get(victim, {})
        victim_cpu = metric_int(victim_state, "cpu")
        victim_stress = metric_int(victim_state, "stress_level")
        victim_eff_blocks = metric_int(victim_state, "eff_blocks")
        victim_miss = metric_int(victim_state, "miss")
        victim_max_cpu = max(victim_max_cpu, victim_cpu)
        victim_max_stress = max(victim_max_stress, victim_stress)
        victim_max_eff_blocks = max(victim_max_eff_blocks, victim_eff_blocks)
        victim_max_miss = max(victim_max_miss, victim_miss)

        victim_ever_stressed = victim_ever_stressed or (
            victim_max_stress > 0
            or victim_max_cpu >= VICTIM_STRESS_CPU_THRESHOLD
            or victim_max_miss > 0
        )
        stress_elapsed = time.time() - t_stress_start
        if (
            not victim_precheck_warned
            and stress_elapsed >= args.stress_precheck_seconds
            and not victim_ever_stressed
        ):
            victim_precheck_warned = True
            log_event("victim_not_stressed", {
                "victim": victim,
                "elapsed_s": round(stress_elapsed, 1),
                "max_cpu": victim_max_cpu,
                "max_stress_level": victim_max_stress,
                "max_eff_blocks": victim_max_eff_blocks,
                "max_miss": victim_max_miss,
            })
            print(
                f"[warn] victim={victim} not stressed after "
                f"{round(stress_elapsed, 1)}s: max_cpu={victim_max_cpu} "
                f"max_stress={victim_max_stress} max_eff_blocks={victim_max_eff_blocks}"
            )

        for n, ns in state.items():
            deleg = ns.get("deleg_state", "IDLE")

            if deleg == "REQUESTING" and t_requesting is None:
                t_requesting = time.time()
                actual_delegator = n
                log_event("deleg_requesting", {"node": n, "t": t_requesting})
                print(f"  -> {n} REQUESTING")

            if (
                deleg == "ACTIVE"
                and t_active is None
                and (actual_delegator is None or n == actual_delegator)
            ):
                t_active = time.time()
                if actual_delegator is None:
                    actual_delegator = n
                peer = ns.get("deleg_peer", "")
                blocks = metric_int(ns, "deleg_blocks")
                hs = round((t_active - t_requesting) * 1000) if t_requesting else None
                log_event("deleg_active", {
                    "delegator": n, "peer": peer,
                    "blocks": blocks, "t": t_active,
                    "handshake_ms": hs,
                })
                print(f"  -> {n} ACTIVE  peer={peer}  blocks={blocks}  handshake={hs}ms")
                delegation_fired = True
                # Derive actual_host from the delegator's peer field.
                # This is more reliable than waiting for the host to self-report
                # deleg_blocks > 0, which can be 0 if the snapshot lands between cycles.
                if peer and (actual_host is None or args.crash_host_after > 0):
                    actual_host = peer
                    log_event("failover_target_selected", {
                        "host": actual_host,
                        "source": "active_peer",
                        "t": time.time(),
                    })

            if deleg == "HOSTING" and t_hosting is None:
                blocks = metric_int(ns, "deleg_blocks")
                if blocks <= 0:
                    if n not in stale_hosts_logged:
                        stale_hosts_logged.add(n)
                        log_event("stale_hosting_ignored", {
                            "host": n,
                            "peer": ns.get("deleg_peer", ""),
                            "blocks": blocks,
                            "t": time.time(),
                        })
                        print(f"  -> {n} HOSTING ignored (blocks={blocks})")
                    continue

                t_hosting = time.time()
                actual_host = n
                log_event("deleg_hosting", {
                    "host": n,
                    "peer": ns.get("deleg_peer", ""),
                    "blocks": blocks,
                    "t": t_hosting,
                })
                print(f"  -> {n} HOSTING  blocks={blocks}")

        if delegation_fired and (
            t_hosting is not None
            or (args.crash_host_after > 0 and actual_host is not None)
        ):
            break

        time.sleep(1)

    if not delegation_fired:
        log_event("deleg_timeout", {"elapsed_s": round(time.time() - t_stress_start, 1)})
        print("[warn] delegation did not fire within timeout — recording anyway")

    # ----------------------------------------------------------------- hold
    print(f"[hold] sustaining asymmetry for {args.hold_seconds}s")
    log_event("hold_start")

    crash_issued = False
    t_crash = None
    crashed_host = None
    t_redelegate = None
    new_host = None
    miss_spike_max = 0
    failover_count_before_crash = 0
    failover_count_max = 0

    hold_end = time.time() + args.hold_seconds
    while time.time() < hold_end:
        state = snap("hold")
        delegator_state = state.get(actual_delegator or victim, {})
        failover_count_max = max(
            failover_count_max,
            metric_int(delegator_state, "deleg_failover_count"),
        )

        # ---- auto-crash host after crash_host_after seconds from ACTIVE ----
        if args.crash_host_after > 0 and not crash_issued:
            elapsed = round(time.time() - t_active, 1) if t_active else None
            if elapsed is None or int(elapsed) % 10 == 0 or elapsed >= args.crash_host_after:
                print(f"[hold-dbg] actual_host={actual_host!r}  t_active={'set' if t_active else 'None'}"
                      f"  elapsed={elapsed}s  threshold={args.crash_host_after}s")
        if (
            args.crash_host_after > 0
            and not crash_issued
            and actual_host is not None
            and t_active is not None
            and (time.time() - t_active) >= args.crash_host_after
        ):
            crashed_host = actual_host
            active_peer_at_crash = delegator_state.get("deleg_peer", "")
            failover_count_before_crash = metric_int(delegator_state, "deleg_failover_count")
            print(f"[failover] crashing host={crashed_host} "
                  f"(t={round(time.time() - t_active, 1)}s after ACTIVE)")
            reboot_node(base, crashed_host)
            crash_issued = True
            t_crash = time.time()
            log_event("host_crashed", {"host": crashed_host, "t": t_crash})

        # ---- after crash: track miss spike and watch for new host -----------
        if crash_issued and t_redelegate is None:
            miss_spike_max = max(miss_spike_max, metric_int(delegator_state, "miss"))
            failover_count_now = metric_int(delegator_state, "deleg_failover_count")
            candidate_peer = delegator_state.get("deleg_peer", "")
            if (
                failover_count_now > failover_count_before_crash
                and delegator_state.get("deleg_state") == "ACTIVE"
                and candidate_peer
            ):
                t_redelegate = time.time()
                new_host = candidate_peer
                time_to_redelegate_ms = round((t_redelegate - t_crash) * 1000)
                log_event("host_recovered", {
                    "new_host": new_host,
                    "t": t_redelegate,
                    "time_to_redelegate_ms": time_to_redelegate_ms,
                    "deleg_failover_count": failover_count_now,
                })
                print(f"  -> re-delegated to {new_host} in {time_to_redelegate_ms}ms  "
                      f"miss_spike_max={miss_spike_max}")
                actual_host = new_host

        time.sleep(1)

    log_event("hold_end")

    # --------------------------------------------------------------- drain
    # Drop the victim load back to low so stress passes, then watch the
    # already-open TCP delegation channels complete in-flight work and drain
    # to IDLE. This demonstrates transient-overload recovery: the mechanism
    # handles the spike and winds down cleanly once pressure is gone.
    print(f"[drain] victim ({victim}) -> load={args.low_load}; watching channels drain for {args.drain_seconds}s")
    set_load(base, victim, args.low_load)
    log_event("drain_start", {"victim_load_released": args.low_load})
    t_drain_idle = None
    drain_end = time.time() + args.drain_seconds
    while time.time() < drain_end:
        state = snap("drain")
        victim_state = state.get(victim, {})
        if victim_state.get("deleg_state", "IDLE") == "IDLE":
            t_drain_idle = time.time()
            log_event("drain_idle", {"t": t_drain_idle})
            print(f"  -> victim channels IDLE after load release")
            break
        time.sleep(1)
    if t_drain_idle is None:
        print(f"  [warn] channels did not reach IDLE within drain window")
    log_event("drain_end")

    # ------------------------------------------------------------- recovery
    print(f"[recovery] all nodes -> load={args.low_load}")
    for n in nodes:
        set_load(base, n, args.low_load)
    log_event("recovery_start")

    t_idle = None
    rec_end = time.time() + args.recovery_seconds
    while time.time() < rec_end:
        state = snap("recovery")
        if all(state.get(n, {}).get("deleg_state", "IDLE") == "IDLE" for n in nodes):
            t_idle = time.time()
            dur = round((t_idle - t_active) * 1000) if t_active else None
            log_event("deleg_idle_restored", {"t": t_idle, "delegation_duration_ms": dur})
            print(f"  -> all nodes IDLE  duration={dur}ms")
            break
        time.sleep(1)

    log_event("recovery_end")

    # ----------------------------------------------------------------- save
    out_dir = os.path.join(args.out_dir, f"{args.label}__{session_id}")
    os.makedirs(out_dir, exist_ok=True)

    src_log = os.path.join(args.log_dir, f"{session_id}.jsonl")
    dst_log = os.path.join(out_dir, f"{session_id}.jsonl")
    if os.path.exists(src_log):
        shutil.move(src_log, dst_log)
        print(f"[logs] telemetry -> {dst_log}")
    else:
        print(f"[warn] telemetry log not found at {src_log}")
        dst_log = None

    handshake_ms = (
        round((t_active - t_requesting) * 1000)
        if (t_active and t_requesting)
        else None
    )
    time_to_delegate_ms = (
        round((t_active - t_stress_start) * 1000) if t_active else None
    )
    delegation_duration_ms = (
        round((t_idle - t_active) * 1000) if (t_idle and t_active) else None
    )
    time_to_redelegate_ms = (
        round((t_redelegate - t_crash) * 1000) if (t_redelegate and t_crash) else None
    )
    # Read deleg_failover_count from final victim/delegator telemetry; also keep
    # the max observed during hold because a victim reboot resets volatile counters.
    final_state = http_get_json(f"{base}/api/state")
    delegator_id = actual_delegator or victim
    failover_count = metric_int(final_state.get(delegator_id, {}), "deleg_failover_count")
    failover_count = max(failover_count, failover_count_max)

    events_path = os.path.join(out_dir, f"delegation_events_{session_id}.json")
    with open(events_path, "w", encoding="utf-8") as f:
        json.dump(
            {
                "session_id": session_id,
                "label": args.label,
                "assigned_victim": victim,
                "actual_delegator": actual_delegator,
                "actual_host": actual_host,
                "bystanders": bystanders,
                "nodes": nodes,
                "high_load": args.high_load,
                "low_load": args.low_load,
                "events": events,
                "summary": {
                    "delegation_fired": delegation_fired,
                    "t_requesting": t_requesting,
                    "t_active": t_active,
                    "t_hosting": t_hosting,
                    "t_idle": t_idle,
                    "handshake_latency_ms": handshake_ms,
                    "time_to_delegate_ms": time_to_delegate_ms,
                    "delegation_duration_ms": delegation_duration_ms,
                    "victim_not_stressed": not victim_ever_stressed,
                    "victim_precheck_warned": victim_precheck_warned,
                    "victim_max_cpu": victim_max_cpu,
                    "victim_max_stress_level": victim_max_stress,
                    "victim_max_eff_blocks": victim_max_eff_blocks,
                    "victim_max_miss": victim_max_miss,
                    "failover_crash_host": crashed_host,
                    "active_peer_at_crash": active_peer_at_crash,
                    "t_crash": t_crash,
                    "t_redelegate": t_redelegate,
                    "time_to_redelegate_ms": time_to_redelegate_ms,
                    "miss_spike_max": miss_spike_max if crash_issued else None,
                    "new_host_after_failover": new_host,
                    "deleg_failover_count": failover_count,
                },
            },
            f,
            indent=2,
        )
    print(f"[done] events -> {events_path}")

    print("\n=== Delegation Validation Summary ===")
    print(f"  Session:              {session_id}")
    print(f"  Delegation fired:     {delegation_fired}")
    print(
        f"  Victim max:           cpu={victim_max_cpu} "
        f"stress={victim_max_stress} eff_blocks={victim_max_eff_blocks} "
        f"miss={victim_max_miss}"
    )
    if not victim_ever_stressed:
        print("  Victim stress check:  NOT STRESSED")
    if handshake_ms is not None:
        print(f"  Handshake latency:    {handshake_ms}ms")
    if time_to_delegate_ms is not None:
        print(f"  Time to delegate:     {time_to_delegate_ms}ms")
    if delegation_duration_ms is not None:
        print(f"  Delegation duration:  {delegation_duration_ms}ms")
    if crash_issued:
        print(f"  Crashed host:         {crashed_host}")
        if time_to_redelegate_ms is not None:
            print(f"  Time to re-delegate:  {time_to_redelegate_ms}ms  new_host={new_host}")
        else:
            print(f"  Re-delegation:        did not fire within hold window")
        print(f"  Miss spike (max):     {miss_spike_max}/20")
        print(f"  deleg_failover_count: {failover_count}")
    print(f"  Output dir:           {out_dir}")

    if dst_log and os.path.exists(dst_log):
        print("\n[analyze] running delegation analysis...")
        subprocess.run(
            [
                sys.executable,
                "experiments/analyze_delegation.py",
                "--log-file", dst_log,
                "--events-file", events_path,
                "--out-dir", out_dir,
                "--label", args.label,
            ],
            check=False,
        )


if __name__ == "__main__":
    try:
        main()
    except urllib.error.URLError as exc:
        raise SystemExit(f"HTTP error: {exc}")
