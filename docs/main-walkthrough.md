# Firmware `main/` Code Walkthrough and Design Decisions

This document explains how `firmware-esp32/main/` is structured, what each module does, and why the design looks the way it does. It is written as a senior‑dev walkthrough for a junior engineer.

## Big Picture
The firmware has four main concerns:
1. **Configuration** (`config/`) — single place for task periods, priorities, limits, and tunables.
2. **Core runtime state** (`core/`) — shared context, CPU metrics.
3. **Networking** (`network/`) — Wi‑Fi + MQTT, including control messages and telemetry.
4. **Tasks** (`tasks/`) — sensor, control, compute, and manager loops.

The flow is:
`app_main.c` → initialize context → Wi‑Fi → MQTT → spawn tasks.

## `app_main.c` — System Bring‑up and Task Wiring
**Purpose:** Create shared context, bring up system services, then start RTOS tasks.

**Key decisions:**
- Single shared `system_context_t` passed to all tasks and networking code. This keeps all telemetry fields and runtime knobs in one place for consistency.
- Wi‑Fi is brought up before MQTT and tasks to avoid early publish failures.
- Metrics hook (`metrics_init`) is optional (compile‑time). This allows baseline runs without profiling overhead.
- Tasks are created with fixed priorities from `config.h`.

**Why this design:**  
You want deterministic startup and a single source of truth for shared data. The context struct makes control + telemetry alignment easy.

## `config/config.h` — All Tunables in One Place
**Purpose:** Centralized constants for reproducibility and experimentation.

**Key decisions:**
- Explicit task periods and priorities for deterministic scheduling.
- Load and block limits (`DEFAULT_LOAD_START`, `DEFAULT_LOAD_FACTOR`, `PROCESSING_BLOCKS`) defined here so experiments can be repeated.
- Profiling switches (`ENABLE_METRICS`, `ENABLE_MANAGER_TASK`) allow baseline runs.

**Why this design:**  
Changing load behavior or priorities should not require hunting across the codebase. This file is the “experiment control panel”.

## `core/system_context.h` — Shared Runtime State
**Purpose:** A single struct that all tasks and network code share.

**Key fields:**
- `node_id`, `telemetry_topic`, `control_topic` — stable identity and topics.
- `cpu_usage`, `queue_depth`, `load_factor` — telemetry signals.
- `processing_exec_*`, `processing_window_*` — timing measurements.
- `active_blocks`, `effective_blocks` — compute scaling knobs.
- `boot_id`, `time_offset_ms`, `time_sync_ready` — telemetry time sync.

**Why this design:**  
All tasks contribute to telemetry and use common knobs. A shared context avoids duplicated state and mismatched metrics.

## `core/metrics.c` — CPU Usage Estimation
**Purpose:** Estimate CPU usage without intrusive profiling.

**Mechanism:**
- Register an **idle hook** (FreeRTOS) that increments `idle_counter`.
- Compute CPU usage as:
  - `idle_pct = delta_idle / idle_baseline`
  - `cpu_usage = 100 - idle_pct`
- Baseline is captured over `CPU_BASELINE_SAMPLES`.

**Design decisions:**
- Baseline gating ensures early measurements are stable.
- CPU usage is derived from idle time, which is low overhead and portable.

**Implications:**  
CPU values are only reliable after baseline is ready.

## `network/wifi.c` — Wi‑Fi Bring‑up
**Purpose:** Connect in STA mode with auto‑reconnect and signal readiness.

**Design decisions:**
- Use `WIFI_CONNECTED_BIT` in an event group.
- Retry on disconnect automatically.

**Why:**  
Keeps MQTT and telemetry resilient to transient Wi‑Fi drops.

## `network/mqtt.c` — Control + Telemetry Transport
**Purpose:** MQTT client initialization, telemetry publish, and control message parsing.

**Key decisions:**
- Node ID derived from MAC suffix for stable identity.
- Topics use templates (`cluster/{node_id}/telemetry`, `cluster/{node_id}/control`).
- Control payload is minimal JSON: `action`, `value`, optional `seq`.
- `SYNC_TIME` control aligns node tick time to wall‑clock epoch ms.

**Why:**  
This keeps control lightweight and deterministic while still enabling time‑sync and latency measurement.

## `tasks/sensor_task.c` — Simulated Sensor Producer
**Purpose:** Generate periodic data into a queue.

**Decisions:**
- Non‑blocking `xQueueSend` so the sensor loop doesn’t stall.
- Period is fixed to `SENSOR_PERIOD_MS`.

## `tasks/control_task.c` — Queue Consumer + Small Workload
**Purpose:** Drain the sensor queue and simulate a lightweight control compute.

**Decisions:**
- Drain multiple items per cycle (up to 4) to avoid queue buildup.
- Fixed spin loop to keep the control cost deterministic.

## `tasks/compute_task.c` — Main Load Generator
**Purpose:** The heavy compute task used for schedulability experiments.

**How load works:**
- `load_factor` scales from `0..DEFAULT_LOAD_FACTOR`.
- That maps to `PROCESSING_BLOCKS`, capped by `active_blocks`.
- Each block runs `compute_kernel()` (matrix multiply).

**Deadline tracking:**
- Each cycle compares finish time vs expected deadline:
  - `expected_deadline = last_wake + COMPUTE_PERIOD_MS`
  - `if end > expected_deadline → miss++`
- Every `PROCESSING_WINDOW_CYCLES`, the task publishes a windowed snapshot:
  - `exec_avg`, `exec_max`, `miss`, `window_ready=1`
  - then resets counters.

**Why this design:**  
It creates a clean knob (`load_factor`) to push the system from schedulable → overload.

## `tasks/manager_task.c` — Telemetry Aggregation
**Purpose:** Aggregate runtime metrics and publish telemetry every second.

**Key decisions:**
- Windowed stats are gated by `processing_window_ready` so only complete windows are published.
- `t_pub_epoch_ms` is derived using time sync to measure telemetry latency.
- Periodic log (every 5s) is available for debugging.

**Why this design:**  
Telemetry is intentionally lightweight and periodic, minimizing interference while still enabling end‑to‑end measurement.

## Telemetry Fields and Meaning
Published JSON fields include:
- `cpu`, `queue`, `load`, `blocks`, `eff_blocks`
- `exec_avg`, `exec_max`, `miss`, `window_ready`
- `t_pub_ms`, `t_pub_epoch_ms`, `boot_id`

These are the basis for schedulability and latency analysis in the dissertation.

## Key Tradeoffs
- **Windowed stats vs raw:** windowed gives stability, raw preserves detail.
- **Manager task priority:** above compute to ensure telemetry delivery but kept short to minimize interference.
- **Baseline measurement:** idle‑hook CPU avoids intrusive profiling.

## Pointers for Reading
Start here, in this order:
1. `main/app_main.c` — high‑level flow.
2. `core/system_context.h` — shared state.
3. `tasks/compute_task.c` — main workload and timing logic.
4. `tasks/manager_task.c` — telemetry and windowing.
5. `network/mqtt.c` — control parsing and time sync.
