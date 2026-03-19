# Day 05 Engineering Log (2026-03-18)

## Focus
- Stabilize single-node evaluation runs.
- Improve tooling for repeatable experiments and analysis outputs.
- Clarify windowed vs raw telemetry behavior.

## Changes Implemented
- `run-lab.sh` test phase now restarts MQTT + dashboard in quiet mode, waits 10s, runs load sweep, analyzes outputs, and prints the analysis directory.
- `experiments/load_sweep.py` now supports `--repeat` and waits up to 20s for nodes to appear before starting the sweep.
- `experiments/analyze_logs.py` supports `--skip-seconds` and `--window-ready-only` to reduce transition artifacts without smoothing raw data.
- Dashboard:
  - Added graphs for `miss` and `saturation`.
  - Added windowed stats panel (exec avg/max, miss, window_ready).
  - Enforced window_ready-only chart updates using last window-ready samples.
  - Quiet logging support via `DASHBOARD_QUIET=1`.
- Profiling toggles added in firmware:
  - `ENABLE_METRICS` and `ENABLE_MANAGER_TASK` to isolate profiling overhead.

## Observations
- Single-node repeated runs are consistent up to load 800. Beyond that, CPU volatility increases and miss counts saturate at window size.
- Telemetry latency p95 spikes were traced to a few outliers (not time-sync failures).
- `miss=20` corresponds to all cycles in the 20-cycle window missing their deadlines.
- Two-node warmup runs show a stable overload threshold at load 850 across repeated sessions.

## Interpretation
- Stability below 800 indicates schedulable operation; instability and misses at 900+ indicate overload regime.
- Window-ready gating yields reliable windowed stats while preserving raw telemetry for forensic analysis.
- Warmup/reset before sweeps eliminates “overload-from-start” runs and yields consistent two-node baselines.

## Telemetry Latency Variability
- Telemetry latency p95 shows weak/inconsistent correlation with load across repeated runs.
- This indicates network jitter dominates telemetry latency rather than compute load.
- Recommendation: treat telemetry latency as a network delivery metric and report median/p95 separately.

## Decisions Captured
- Added warmup phase (load reset + delay) before each sweep to stabilize initial conditions.
- Defined saturation as `cpu >= 90 OR miss > 0` for early-warning of overload.
- Added labeled run outputs and an index for easier navigation of experiments.
- Added window-ready-only analysis and skip-seconds to avoid transition artifacts without smoothing raw data.

## Artifacts
- Analysis outputs under `experiments/analysis_outputs/session_20260318-030318`, `...-030638`, `...-030959`.
- Session-specific interpretation notes written for `session_20260318-024639`.

## Next Steps
- Aggregate repeated run summaries into a single table for dissertation.
- Start two-node evaluation runs (step 2 of the evaluation plan).
- TODO: Run a fixed-load, long-duration 2-node latency test and compare median/p95 distributions for network overhead.

## Continuation
- See `docs/engineering-logs/day06.md` for follow-up challenges and observations:
  - dashboard control-path reliability fixes,
  - reboot/MQTT fault injection validation,
  - dual-core vs single-core comparison runs,
  - watchdog-backed overload evidence.
