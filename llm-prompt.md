# Dissertation Context + Project Brief (Distributed RTOS)

This document captures the current state of the dissertation project, the architecture, motivations, challenges, progress, and what remains. It is intended to give a new LLM the full context in one place.

---

## 1) One‑line Summary
A research‑driven engineering project that designs a fault‑tolerant, distributed runtime layer for resource‑constrained edge devices (FreeRTOS on ESP‑class hardware), where nodes detect overload, share telemetry, and coordinate without a central master; the dashboard is observability only.

---

## 2) Vision & Motivation
Most embedded systems assume stable execution and isolated control. This dissertation challenges that by treating a small cluster of edge devices as a distributed system. The goal is to build the engineering foundations for:

- Real‑time telemetry exchange between nodes
- Overload detection using CPU/queue metrics
- Peer awareness and logical failure handling
- Task delegation under stress
- No single point of failure, no central coordinator

The dashboard must not be the brain. It is intentionally passive: observability only.

---

## 3) System Architecture (High Level)

```
[Node A]   [Node B]   [Node C]
   |          |          |
   +-------- MQTT Broker --------+
                                |
                         Monitoring Dashboard
```

Each node publishes telemetry over MQTT. The dashboard subscribes and visualizes but does not schedule or coordinate.

---

## 4) Repository Structure (Key Folders)

- `firmware/` — ESP8266 FreeRTOS firmware (baseline phase)
- `firmware-esp32/` — ESP32 ESP‑IDF port (current phase)
- `dashboard/` — Flask + MQTT UI for observability
- `docs/engineering-logs/` — research engineering log (Day 1 & Day 2)
- `RUNBOOK.md` — build/flash workflows

---

## 5) Design Principles

- Strict separation between portable core logic and SDK‑specific APIs
- Only the network layer touches ESP SDKs
- Reproducible build environments (Docker for ESP8266, ESP‑IDF for ESP32)
- Observability before optimization
- Avoid a master node unless absolutely necessary
- Treat embedded clusters as distributed systems, not isolated devices

---

## 6) Current Firmware Architecture (ESP32)

In `firmware-esp32/main/`:

- **Core state**: `system_context_t` holds MQTT topics, queue, metrics, load, block caps, and exec stats
- **Tasks**:
  - `sensor_task`: periodic queue producer
  - `control_task`: drains queue and simulates control workload
  - `compute_task`: heavy matrix kernel scaled by load
  - `manager_task`: CPU/queue stats + MQTT telemetry
- **Networking**:
  - Wi‑Fi via ESP‑IDF `esp_wifi` / `esp_event`
  - MQTT via `esp_mqtt_client`

Telemetry fields currently include:

- `fw` (firmware tag)
- `cpu` (CPU usage)
- `queue` (queue depth)
- `load` (load factor)
- `blocks` (cap)
- `eff_blocks` (effective blocks after scaling)
- `exec_avg`, `exec_max`, `miss` (windowed stats)
- `window_ready`

Load scaling:

```
scaled_blocks = (load_factor * PROCESSING_BLOCKS) / DEFAULT_LOAD_FACTOR
blocks = min(scaled_blocks, active_blocks)
```

A baseline idle sample is collected at boot (CPU_BASELINE_SAMPLES), and compute is paused until baseline is ready to stabilize CPU metrics.

---

## 7) Dashboard (Observability Only)

`dashboard/` is a Flask app that:

- Subscribes to `cluster/+/telemetry`
- Stores latest per‑node state in memory
- Serves `/api/state` for polling UI
- Publishes control commands via `/api/control`

The UI includes:

- Real‑time graphs (CPU, queue, load) with node selector
- Table of nodes with FW tag, CPU, queue, load, eff_blocks, window_ready
- Slider + numeric inputs for load and blocks (no +/- buttons)

Notes:
- The dashboard is intentionally passive; it does not coordinate
- Table updates are paused while typing in inputs so the page doesn’t overwrite user edits

---

## 8) Tooling & Build

- ESP8266 (Phase 1): Dockerized build environment (`Dockerfile.esp8266`)
- ESP32 (Phase 2): ESP‑IDF v4.4 project in `firmware-esp32/`
- MQTT broker typically Mosquitto

Important issue discovered and documented:
- Mosquitto 2.x defaults to **local-only mode**. Must use a config like:

```
listener 1883 0.0.0.0
allow_anonymous true
```

---

## 9) Challenges Encountered (Key Lessons)

- **Toolchain instability** (Apple Silicon vs x86): required Docker isolation
- **MQTT broker binding**: default local-only mode blocked ESP devices
- **Telemetry calibration**: CPU metrics were unstable until baseline capture was fixed
- **Load tuning**: ongoing tuning of compute kernel size and blocks to meet target CPU curves
- **Dashboard UX**: frequent refresh conflicted with user input; fixed by pausing table updates during edit

---

## 10) Current Status (Progress)

Completed / Working:
- Telemetry pipeline: device → broker → dashboard
- ESP32 port with ESP‑IDF
- Node‑unique MQTT topics
- Load/blocks control via MQTT
- Firmware tag published to identify builds
- Debug logs for control changes and compact snapshots

Still In Progress:
- Precise CPU tuning targets (goal: ~10% CPU @ load 200, ~90% CPU @ load 900)
- Validation of windowed exec stats under realistic loads

---

## 11) What’s Remaining (Roadmap)

Near-term:
- Finalize CPU/load calibration curve
- Stabilize and validate telemetry reliability
- Heartbeat / liveness detection
- Overload heuristics

Mid-term:
- Delegation protocol (task offload when overloaded)
- Fault injection experiments

Long-term:
- Formal evaluation and dissertation write‑up
- Comparative analysis of scheduling/delegation strategies

---

## 12) Constraints & Assumptions

- Observability only; dashboard is not coordinator
- Embedded nodes must remain portable across ESP8266/ESP32
- Distributed system design principles are prioritized over quick demos

---

## 13) Practical Debug Tips (Current)

- Baseline CPU needs ~3 seconds after boot to become valid
- Windowed exec stats update every PROCESSING_WINDOW_CYCLES cycles
- Use FW tag to verify all nodes run the same build
- MQTT logs should show control updates (`SET_LOAD`, `SET_BLOCKS`)
- If telemetry JSON is invalid, check payload length and `manager_task` buffer size

---

## 14) What a New Contributor Should Know

This is not a dashboard project. It is a systems‑architecture dissertation. The dashboard is a lightweight visualization tool. The real work is in the node behavior, overload detection, and the eventual distributed delegation protocol.

A contributor should respect:
- Layer isolation (core/tasks vs network)
- Reproducible builds
- Avoid central coordination

---

## 15) Key Files to Read

- `README.md` (vision + roadmap)
- `RUNBOOK.md` (build/flash steps)
- `docs/engineering-logs/day01.md` (vision, toolchain, architecture decisions)
- `docs/engineering-logs/day02.md` (MQTT binding failure + fix)
- `firmware-esp32/main/*` (current active firmware)
- `dashboard/*` (observability layer)

---

## 16) Current Parameter Snapshot (as of fw-0.1.1)

These are the latest tuning values used for CPU/load calibration:

- `PROCESSING_BLOCKS = 16`
- `ACTIVE_BLOCKS = 16`
- `MATRIX_SIZE = 24`
- `COMPUTE_PERIOD_MS = 100`
- `DEFAULT_LOAD_FACTOR = 1000`
- `CPU_BASELINE_SAMPLES = 3`

These values are not final and will change based on calibration targets.

---

## 17) Known Pain Points Right Now

- CPU calibration curve still not aligned with target range
- CPU usage can appear unstable if baseline not captured correctly
- Telemetry fields still evolving
- The dashboard should remain minimal; avoid making it the system brain

---

If you are an LLM reading this, treat this as a long‑running systems dissertation. Provide solutions that respect layered architecture, embedded constraints, and distributed‑systems thinking. Avoid suggesting centralized coordination via the dashboard.
