# Engineering Log Day 3 — Runtime Stabilization + Instrumentation

## Focus

The focus shifted from connectivity to **runtime stability** and **measurement credibility**.

The system was producing telemetry, but CPU and load behavior were noisy and hard to interpret. This day was about making the runtime signals stable and trustworthy.

---

## Key Issues Observed

- CPU utilization fluctuated heavily under constant load.
- Dashboard graphs looked “zig‑zag” even when nothing changed.
- Windowed stats (`exec_avg`, `exec_max`, `miss`) were often stale or ambiguous.

---

## Changes Made (Stability + Instrumentation)

### 1) CPU Baseline Fix

- CPU usage is derived from idle‑hook measurements.
- Baseline drift caused noisy estimates.
- We added a **startup baseline sampling period** and paused compute until baseline was captured.

Result: CPU readings stabilized significantly after boot.

### 2) Windowed Stats Interpretation

- Added `window_ready` to explicitly indicate when windowed stats are valid.
- This avoids misreading partial window values.

### 3) Effective Block Reporting

- Added `eff_blocks` in telemetry to show **actual compute blocks executed** after load scaling.
- This removed confusion between cap (`blocks`) and effective load.

### 4) Debug Logs (Compact)

- Periodic compact logs added to firmware:
  - `cpu`, `queue`, `load`, `cap`, `eff`, `win`
- MQTT control logs now show applied values.

---

## Result

By end of Day 3:

- CPU telemetry is far more stable.
- Compute load is traceable via `eff_blocks`.
- Windowed stats are clearly gated.

The system is now credible enough for evaluation harness work.
