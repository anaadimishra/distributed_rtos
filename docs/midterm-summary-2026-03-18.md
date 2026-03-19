# Midterm Summary (2026-03-18)

## Scope of Today’s Work
We focused on stabilizing evaluation runs, improving repeatability of experiments, and clarifying how telemetry is interpreted for the dissertation.

## Design Decisions (Today)
- **Windowed stats as the canonical analysis signal:** Use `window_ready=1` samples for analysis and dashboards to avoid partial-window noise without smoothing raw data.
- **Saturation definition:** `cpu >= 90% OR miss > 0` as an early-warning indicator of overload.
- **Profiling isolation:** Added compile-time switches to disable metrics and/or manager task to quantify profiling overhead.
- **Experiment repeatability:** Load sweeps can now run repeated trials and produce per-session outputs for comparison.
- **Quiet test runs:** Redirect MQTT/dashboard logs to files to avoid console flooding.

## Tooling Improvements
- `run-lab.sh test`:
  - Restarts MQTT + dashboard in quiet mode.
  - Waits 10s, runs a load sweep, analyzes logs, and prints output paths.
- `experiments/load_sweep.py`:
  - Added `--repeat`.
  - Waits up to 20s for telemetry before running.
- `experiments/analyze_logs.py`:
  - Added `--skip-seconds` (default 2) and `--window-ready-only`.
  - Outputs analysis under `experiments/analysis_outputs/<session_id>/`.
- Dashboard:
  - Added miss and saturation graphs.
  - Added windowed stats panel (exec avg/max, miss, window_ready).
  - Enforced window-ready-only chart updates.

## Key Observations
- **Stable regime:** Loads 100–800 are consistent across repeated runs.
  - `exec_max_p95` matches exactly across repeats.
  - `miss_p95` remains 0.
- **Overload regime:** Loads 900–1000 show `miss_p95 = 20` (all cycles missed in a 20-cycle window) with high CPU volatility.
- **Telemetry latency spikes:** p95 spikes were driven by a few outliers (not time-sync failures).
- **Two-node warmup baseline:** Overload threshold stabilized at **load 850** across repeated sessions.

## Coherent Data Collected (for report use)
- **Repeated single-node sweeps:** Three back-to-back sessions with matching load steps (100→1000, 20s each).
- **Windowed metrics per load:** `cpu_mean`, `cpu_p95`, `exec_max_p95`, `miss_p95`, `telemetry_latency_p95_ms`.
- **Raw window-ready CPU samples:** `analysis_outputs/session_20260318-024639/cpu_raw_window_ready_ge700.txt`.
- **Session-level interpretations:** `analysis_outputs/session_20260318-024639/analysis_summary.md`.

## Interpretation
- The system is schedulable and stable up to ~800 load with 24 blocks.
- Overload begins around 900 load, where deadline misses dominate and CPU becomes highly volatile.
- Windowed stats are the correct basis for schedulability claims; raw telemetry remains available for traceability.
- Warmup resets before sweeps provide a consistent baseline for multi-node comparisons.

## Telemetry Latency Variability Note
- Across three consecutive runs, telemetry latency p95 shows **weak or inconsistent correlation** with load.
- Per-run correlations: `0.145`, `0.048`, `-0.565` (load vs telemetry p95), indicating network jitter dominates.
- Recommendation: treat telemetry latency as a **network delivery metric**, report median/p95 separately, and avoid using it as a proxy for CPU load.

## References (Artifacts)
- `docs/engineering-logs/day05.md`
- `experiments/analysis_outputs/session_20260318-030318`
- `experiments/analysis_outputs/session_20260318-030638`
- `experiments/analysis_outputs/session_20260318-030959`
- `experiments/analysis_outputs/session_20260318-024639/analysis_summary.md`

## Next Steps (Evaluation Plan Order)
1. Single-node stabilization complete.
2. Two-node schedulability + latency runs.
3. Failover time measurement.
4. Consensus overhead experiment.
5. Scale to five nodes once stability is confirmed.
