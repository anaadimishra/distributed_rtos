# Engineering Log Day 4 — Calibration + Evaluation Pipeline

## Focus

We moved from stabilization into **calibration** and **evaluation tooling**. The goal was to make the system measurable in a way that supports dissertation‑grade evidence.

---

## Calibration Iterations

### CPU Baseline Sampling

- Increased baseline sampling window to improve stability (e.g., 3 → 20 samples).
- This reduced jitter at the cost of a short “warm‑up” period.

### Load vs Blocks Clarification

- Reinforced the relationship:
  - `load` scales work
  - `blocks` is the ceiling
  - `eff_blocks` is the truth

This reduced confusion when interpreting graphs and telemetry.

### Window Size Reduction

- Reduced `PROCESSING_WINDOW_CYCLES` so windowed stats update faster (target ~2s windows).
- This makes evaluation loops responsive while still averaging noise.

---

## Evaluation Pipeline

### 1) Time Sync for Real Latency

- Added `SYNC_TIME` control to align node clock with dashboard epoch.
- Telemetry now includes `t_pub_epoch_ms`, enabling real end‑to‑end latency measurement.

### 2) Session‑Based Logging

- Each dashboard run creates a new JSONL log file:

```
dashboard/telemetry_logs/session_YYYYMMDD-HHMMSS.jsonl
```

- A dashboard button allows manual log session reset to separate experiments.

### 3) Load Sweep Harness

`experiments/load_sweep.py`

- Detects all nodes
- Sweeps load 100 → 1000
- Holds each step for 10 seconds
- Records session + schedule metadata

### 4) Analysis + Plots

`experiments/analyze_logs.py`

Produces:

- CSV summaries (per node + load)
- CPU vs load
- queue p95 vs load
- eff_blocks mean vs load
- exec_max p95 vs load
- miss p95 vs load
- control latency p95 vs load
- telemetry latency p95 vs load

---

## Result

The system now produces **repeatable experimental runs** with measurable metrics and plots. This directly addresses evaluation depth requirements for the dissertation.
