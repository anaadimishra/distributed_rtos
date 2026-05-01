#!/usr/bin/env python3
"""
Generate failover analysis plots for deleg-failover-run7.

Outputs (saved alongside the run's existing delegation_timeline.png):
  1. failover_crash_window.png  — victim miss + cpu zoomed to crash/recovery window
  2. failover_full_timeline.png — full session: all nodes cpu/miss + events annotated
  3. failover_counter.png       — deleg_failover_count staircase (victim)
"""

import json
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np

RUN_DIR = os.path.join(
    os.path.dirname(__file__),
    "analysis_outputs",
    "deleg-failover-run7__session_20260501-123221",
)
JSONL   = os.path.join(RUN_DIR, "session_20260501-123221.jsonl")
EVENTS  = os.path.join(RUN_DIR, "delegation_events_session_20260501-123221.json")

# ------------------------------------------------------------------ load data
records = []
with open(JSONL) as f:
    for line in f:
        line = line.strip()
        if line:
            records.append(json.loads(line))

with open(EVENTS) as f:
    ev_data = json.load(f)

events = ev_data.get("events", [])

# Key timestamps (Unix seconds)
t_stress  = next((e["t"] for e in events if e["kind"] == "stress_start"),  None)
t_active  = next((e["t"] for e in events if e["kind"] == "deleg_active"),  None)
t_crash   = next((e["t"] for e in events if e["kind"] == "host_crashed"),  None)
t_recov   = next((e["t"] for e in events if e["kind"] == "host_recovered"),None)
t_hold_end= next((e["t"] for e in events if e["kind"] == "hold_end"),      None)

# Organise per-node time series
nodes = {}
for rec in records:
    nid = rec["node_id"]
    p   = rec["payload"]
    t   = rec["t_rx_ms"] / 1000.0
    nodes.setdefault(nid, {"t": [], "cpu": [], "miss": [], "deleg_state": [],
                           "deleg_failover_count": [], "deleg_blocks": []})
    nodes[nid]["t"].append(t)
    nodes[nid]["cpu"].append(int(p.get("cpu", 0)))
    nodes[nid]["miss"].append(int(p.get("miss", 0)))
    nodes[nid]["deleg_state"].append(p.get("deleg_state", "IDLE"))
    nodes[nid]["deleg_failover_count"].append(int(p.get("deleg_failover_count", 0)))
    nodes[nid]["deleg_blocks"].append(int(p.get("deleg_blocks", 0)))

victim   = "node-34A9F0"
bystanders = [n for n in nodes if n != victim]
NODE_COLORS = {
    "node-34A9F0": "#e74c3c",   # victim — red
    "node-2FCC00": "#3498db",
    "node-313978": "#2ecc71",
    "node-7115F8": "#9b59b6",
    "node-717AC4": "#f39c12",   # crashed host — orange
}

def t_rel(t_abs):
    """Seconds relative to stress start."""
    return t_abs - t_stress if t_stress else t_abs

def vline(ax, t_abs, label, color, ls="--", ymax=1.0):
    if t_abs:
        x = t_rel(t_abs)
        ax.axvline(x, color=color, linestyle=ls, linewidth=1.4, alpha=0.85)
        ax.text(x + 0.4, ax.get_ylim()[1] * ymax * 0.97, label,
                color=color, fontsize=7, rotation=90, va="top")


# ============================================================ PLOT 1: crash window
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6), sharex=True)
fig.suptitle("Failover Crash Window — deleg-failover-run7 (node-34A9F0 victim)",
             fontsize=11, fontweight="bold")

v = nodes[victim]
t_v = [t_rel(t) for t in v["t"]]

ax1.plot(t_v, v["miss"], color=NODE_COLORS[victim], linewidth=1.8, label="miss (victim)")
ax1.set_ylabel("Deadline Misses / window (max 20)")
ax1.set_ylim(-0.5, 22)
ax1.legend(loc="upper left", fontsize=8)
ax1.grid(True, alpha=0.3)

ax2.plot(t_v, v["cpu"], color=NODE_COLORS[victim], linewidth=1.8, label="cpu % (victim)")
ax2.set_ylabel("CPU Utilisation (%)")
ax2.set_ylim(0, 110)
ax2.set_xlabel("Time since stress start (s)")
ax2.legend(loc="upper left", fontsize=8)
ax2.grid(True, alpha=0.3)

# Shade crash → recovery
if t_crash and t_recov:
    for ax in (ax1, ax2):
        ax.axvspan(t_rel(t_crash), t_rel(t_recov),
                   color="#e74c3c", alpha=0.12, label="crash window")

# Event lines
for ax in (ax1, ax2):
    ax.set_xlim(t_rel(t_active) - 5 if t_active else 0,
                t_rel(t_hold_end) + 5 if t_hold_end else 200)
    if t_active: vline(ax, t_active, "ACTIVE",   "#27ae60", ymax=0.85)
    if t_crash:  vline(ax, t_crash,  "CRASH",    "#e74c3c", ymax=0.85)
    if t_recov:  vline(ax, t_recov,  "RECOVERED","#2980b9", ymax=0.85)

crash_patch = mpatches.Patch(color="#e74c3c", alpha=0.2, label=f"crash window (~17s)")
ax1.legend(handles=[ax1.lines[0], crash_patch], loc="upper left", fontsize=8)

plt.tight_layout()
out1 = os.path.join(RUN_DIR, "failover_crash_window.png")
plt.savefig(out1, dpi=150, bbox_inches="tight")
plt.close()
print(f"[saved] {out1}")


# ============================================================ PLOT 2: full session timeline
fig, axes = plt.subplots(3, 1, figsize=(13, 9), sharex=True)
fig.suptitle("Full Session Timeline — deleg-failover-run7", fontsize=11, fontweight="bold")

ax_cpu, ax_miss, ax_fc = axes

all_nodes_sorted = [victim] + sorted(bystanders)
for nid in all_nodes_sorted:
    nd  = nodes[nid]
    t_n = [t_rel(t) for t in nd["t"]]
    c   = NODE_COLORS.get(nid, "#7f8c8d")
    lw  = 2.2 if nid == victim else 1.2
    lbl = f"{nid} (victim)" if nid == victim else nid
    ax_cpu.plot(t_n,  nd["cpu"],  color=c, linewidth=lw, label=lbl)
    ax_miss.plot(t_n, nd["miss"], color=c, linewidth=lw, label=lbl)

# failover_count only for victim
t_v = [t_rel(t) for t in nodes[victim]["t"]]
ax_fc.step(t_v, nodes[victim]["deleg_failover_count"],
           where="post", color=NODE_COLORS[victim], linewidth=1.8, label="deleg_failover_count (victim)")

ax_cpu.set_ylabel("CPU Utilisation (%)")
ax_cpu.set_ylim(0, 110)
ax_cpu.legend(loc="upper right", fontsize=7, ncol=2)
ax_cpu.grid(True, alpha=0.3)

ax_miss.set_ylabel("Deadline Misses / window")
ax_miss.set_ylim(-0.5, 22)
ax_miss.legend(loc="upper right", fontsize=7, ncol=2)
ax_miss.grid(True, alpha=0.3)

ax_fc.set_ylabel("deleg_failover_count")
ax_fc.set_xlabel("Time since stress start (s)")
ax_fc.legend(loc="upper left", fontsize=8)
ax_fc.grid(True, alpha=0.3)

# Shade crash window on all panels
if t_crash and t_recov:
    for ax in axes:
        ax.axvspan(t_rel(t_crash), t_rel(t_recov), color="#e74c3c", alpha=0.10)

# Event lines on all panels
for ax in axes:
    if t_active:  vline(ax, t_active,  "ACTIVE",    "#27ae60")
    if t_crash:   vline(ax, t_crash,   "CRASH",     "#e74c3c")
    if t_recov:   vline(ax, t_recov,   "RECOVERED", "#2980b9")
    if t_hold_end:vline(ax, t_hold_end,"hold end",  "#7f8c8d", ls=":")

plt.tight_layout()
out2 = os.path.join(RUN_DIR, "failover_full_timeline.png")
plt.savefig(out2, dpi=150, bbox_inches="tight")
plt.close()
print(f"[saved] {out2}")


# ============================================================ PLOT 3: failover counter zoom
fig, ax = plt.subplots(figsize=(9, 4))
fig.suptitle("deleg_failover_count — victim node-34A9F0 (run7)", fontsize=11, fontweight="bold")

ax.step(t_v, nodes[victim]["deleg_failover_count"],
        where="post", color=NODE_COLORS[victim], linewidth=2.0)
ax.fill_between(t_v, nodes[victim]["deleg_failover_count"],
                step="post", alpha=0.15, color=NODE_COLORS[victim])

ax.set_xlabel("Time since stress start (s)")
ax.set_ylabel("Cumulative failover count")
ax.grid(True, alpha=0.3)

if t_crash and t_recov:
    ax.axvspan(t_rel(t_crash), t_rel(t_recov), color="#e74c3c", alpha=0.12)

if t_active:  vline(ax, t_active,  "ACTIVE",    "#27ae60")
if t_crash:   vline(ax, t_crash,   "CRASH",     "#e74c3c")
if t_recov:   vline(ax, t_recov,   "RECOVERED", "#2980b9")

# Annotate the crash-recovery increment
if t_crash and t_recov:
    ax.annotate(
        f"+{5} at recovery\n(17.2s gap)",
        xy=(t_rel(t_recov), 5),
        xytext=(t_rel(t_recov) + 4, 3),
        arrowprops=dict(arrowstyle="->", color="#2980b9"),
        fontsize=8, color="#2980b9",
    )

plt.tight_layout()
out3 = os.path.join(RUN_DIR, "failover_counter.png")
plt.savefig(out3, dpi=150, bbox_inches="tight")
plt.close()
print(f"[saved] {out3}")

print("\nAll plots saved to:")
print(f"  {RUN_DIR}/")
print("  - failover_crash_window.png")
print("  - failover_full_timeline.png")
print("  - failover_counter.png")
