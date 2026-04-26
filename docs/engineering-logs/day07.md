# Day 07 Engineering Log (2026-03-22)

## Focus
- Start Phase 2 (distributed adaptation) after midterm submission.
- Move from observability-only telemetry to decentralized runtime adaptation.
- Keep constraints strict: deterministic behavior, no dynamic allocation, no central coordinator.

## Starting Point (Before Phase 2)
- Architecture:
  - ESP32 + FreeRTOS nodes, autonomous, MQTT-based communication.
  - No central scheduler/coordinator.
- Runtime mode:
  - Experimental baseline validated in unicore mode.
- Telemetry already available:
  - `cpu`, `load`, `exec_avg`, `exec_max`, `miss`, `window_ready`, timestamps, `state`, `drift_ms`.
- Existing evaluation pipeline:
  - `run-lab.sh`, `load_sweep.py`, `analyze_logs.py`, `compare_topologies.py`.
  - Repeated 1-node, 2-node, and 5-node sweeps analyzed.
- Known interpretation from latest sweeps:
  - Current overload threshold observed around load factor ~800 across 1/2/5 nodes for the active compute profile.

## What We Implemented Now (Phase 2 Core)

### 1) Deterministic stress model
- Added stress state machine with priority order:
  - `miss > 0` -> `STRESS_HIGH`
  - `exec_max > STRESS_EXEC_THRESHOLD_TICKS` -> `STRESS_MEDIUM`
  - `cpu > STRESS_CPU_THRESHOLD_PCT` -> `STRESS_MEDIUM`
  - else -> `STRESS_LOW`
- Implemented in manager task context (O(1), no floats).

### 2) Telemetry extension
- Added `stress_level` to telemetry payload.
- Backward compatible (existing fields unchanged).

### 3) Peer state table (fixed-size)
- Added fixed peer table with:
  - `node_id`, `stress_level`, `last_seen_ms`, `valid`
- Implemented as static members in shared context (`MAX_PEERS` bounded array).
- MQTT callback updates peer entries from `cluster/+/telemetry`.
- No blocking or heavy work in callback.

### 4) Manager-task-only adaptation logic
- Decision rule:
  - If self `STRESS_HIGH` and any peer is `STRESS_LOW` -> reduce local `load_factor`.
- Added anti-oscillation controls:
  - one adjustment per window (`PROCESSING_WINDOW_CYCLES * COMPUTE_PERIOD_MS`)
  - gradual increase only after multiple LOW windows (`ADAPT_LOW_WINDOWS_TO_INCREASE`)
  - clamp to `LOAD_FACTOR_MIN` / `LOAD_FACTOR_MAX`
- Added stale peer eviction in manager task via `PEER_TIMEOUT_MS`.

### 5) Observability hooks for adaptation
- Added logs for:
  - stress transitions
  - load adaptations (reduce/increase)
  - peer-assisted decisions

## Dashboard Alignment
- Dashboard now parses and displays:
  - `state`
  - `stress_level` (`LOW` / `MEDIUM` / `HIGH`)

## Config and Runtime Alignment
- Switched SDK config back to unicore:
  - `CONFIG_FREERTOS_UNICORE=y`
  - CPU1 idle watchdog checks disabled for unicore consistency.

## Files Updated in This Phase-2 Start Slice
- `firmware-esp32/main/core/system_context.h`
- `firmware-esp32/main/network/mqtt.c`
- `firmware-esp32/main/tasks/manager_task.c`
- `firmware-esp32/main/config/config.h`
- `firmware-esp32/sdkconfig`
- `dashboard/app.py`
- `dashboard/static/app.js`
- `dashboard/templates/index.html`

## Immediate Validation Plan (Next)
1. Flash 2+ nodes with current firmware.
2. Verify telemetry includes `stress_level` and dashboard stress column updates live.
3. Trigger controlled stress asymmetry between nodes and confirm:
   - stressed node reduces load when low-stress peer exists,
   - no more than one adjustment per window,
   - no rapid oscillation.
4. Capture adaptation sessions and update analysis/documentation for dissertation Phase 2 section.

## Scope Guardrails (Still Not Implemented)
- No task migration.
- No consensus protocol.
- No centralized scheduling.
- No complex balancing heuristics beyond fixed-step local adaptation.
