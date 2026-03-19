# Day 06 Engineering Log (2026-03-19)

## Focus
- Execute controlled 2-node and 4-node sweeps for dissertation evidence.
- Validate fault-injection controls (`MQTT OFF/ON`, reboot) from dashboard to firmware.
- Isolate core-mode effects (dual-core vs single-core) on CPU volatility and overload behavior.

## Major Changes Implemented
- Firmware/dashboard control path:
  - Added control actions in firmware: `FAIL_SILENT_ON`, `FAIL_SILENT_OFF`, `REBOOT`, `POWEROFF`.
  - Added dashboard per-node controls with explicit UI feedback states.
  - Added `fault_mode` tracking in dashboard state.
- Telemetry and observability:
  - Added telemetry fields: `drift_ms`, `t_actual_publish_ms`, `t_expected_publish_ms`, `state`.
  - Added extra debug logs in dashboard and firmware for control dispatch/receipt.
- Tooling:
  - Updated `run-lab.sh` with `monitor` mode (minicom).
  - Fixed `run-lab.sh` shell boolean condition parsing issues.
  - Fixed CSV parsing bug (`KeyError: node_id`) by skipping `#` comment lines in run/compare scripts.
- Baseline/performance pipeline:
  - Added baseline mode support (build with metrics disabled, targeted sweep).
  - Added analysis support for baseline comparison and repeat-summary outputs.
- Compute retuning:
  - Reduced compute kernel size from `MATRIX_SIZE=28` to `MATRIX_SIZE=20` to lower low-load CPU floor.

## Challenges Encountered

### 1) Dashboard controls appeared non-responsive
- Symptom: clicking reboot/MQTT buttons had no visible change and no control logs on node.
- Root causes:
  - stale browser JS cache,
  - frequent table re-renders interfering with click handling/feedback.
- Resolution:
  - forced cache-busting script version in template,
  - hardened button handlers,
  - immediate UI feedback + control status line,
  - new-tab hard refresh confirmed fix.

### 2) MQTT disconnect after reboot
- Symptom on serial:
  - `Publish: Losing qos0 data when client not connected`
- Interpretation:
  - node rebooted correctly, but broker connection had not re-established yet.
- Impact:
  - telemetry packets during disconnect windows are dropped and should not be used for stable-latency claims.

### 3) High-load watchdog events
- Symptom at high load (e.g., 900):
  - repeated `task_wdt` bark,
  - `compute_task` running while idle task starved.
- Interpretation:
  - this is a hard overload regime, not just chart noise.
- Decision:
  - treat WDT as explicit overload evidence in report; do not mix these windows with stable-regime metrics.

### 4) Core-mode confusion during experiments
- Symptom: unexpected CPU behavior until config re-checked.
- Finding:
  - `sdkconfig` had dual-core active when single-core was expected.
- Action:
  - verified and switched `CONFIG_FREERTOS_UNICORE=y` for single-core campaign.

## Key Observations (Today)

### A) 4-node repeated sweeps (500 to 1000)
- Stable behavior up to ~800 load.
- At 900/1000, miss window saturates (`miss_p95=20`) and volatility rises sharply.
- Confirms schedulable vs overload regime split in multi-node setup.

### B) Dual-core vs single-core (2 nodes, 3 runs each)
- Dual-core:
  - lower CPU floor at low load,
  - higher volatility near threshold.
- Single-core:
  - higher CPU floor at low load,
  - earlier approach to saturation.
- One dual-core session was contaminated and excluded for clean comparison.

### C) Control-path verification
- `curl` to `/api/control` consistently worked even when UI did not.
- Once UI cache/handler issues were fixed, reboot and MQTT toggle behavior aligned with firmware logs.

## Decisions Captured
- Use WDT bark as valid overload evidence in dissertation, not as an outlier to hide.
- Separate analyses into:
  - stable regime (no disconnect/WDT),
  - failure regime (includes WDT/disconnect behavior).
- Keep explicit node-control feedback in dashboard to avoid ambiguous operator actions.
- Retune compute intensity (`MATRIX_SIZE=20`) to recover low-load headroom for clearer sweeps.

## Remaining Work (Tracked)
- Add explicit invalidation flags in analysis for reboot/WDT/disconnect windows.
- Finalize clean single-core vs dual-core comparative table with contaminated runs excluded.
- Extend report sections:
  - threats to validity,
  - formal response-time grounding,
  - topology threshold comparison.

## Evidence Index

### 4-node repeated sweeps (500 to 1000)
- `experiments/analysis_outputs/4nodes-500_to_1000_100st_3l-test__session_20260319-152009`
- `experiments/analysis_outputs/4nodes-500_to_1000_100st_3l-test__session_20260319-152219`
- `experiments/analysis_outputs/4nodes-500_to_1000_100st_3l-test__session_20260319-152429`

### 2-node dual-core campaign (3 runs)
- `experiments/analysis_outputs/2nodes-dualcore-run__session_20260319-221932` (contaminated run)
- `experiments/analysis_outputs/2nodes-dualcore-run__session_20260319-222148`
- `experiments/analysis_outputs/2nodes-dualcore-run__session_20260319-222358`

### 2-node single-core campaign (3 runs)
- `experiments/analysis_outputs/2nodes-singlecore-run__session_20260319-230650`
- `experiments/analysis_outputs/2nodes-singlecore-run__session_20260319-230906`
- `experiments/analysis_outputs/2nodes-singlecore-run__session_20260319-231116`

### Supporting operational artifacts
- Dashboard interaction screenshot:
  - `Screenshot 2026-03-19 at 21-18-58 RTOS Telemetry Dashboard.png`
- Session log root:
  - `dashboard/telemetry_logs/`
- Analysis index:
  - `experiments/analysis_outputs/index.csv`
