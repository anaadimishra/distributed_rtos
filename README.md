# Distributed RTOS Telemetry and Overload Evaluation (ESP32)

A distributed real-time task scheduling system for ESP32/FreeRTOS nodes coordinated via MQTT with no central controller. Empirically characterises schedulability boundaries and implements bounded multi-peer task delegation across 1–5 node clusters.

**Current firmware:** `fw-0.4.0-tcp` (Phase 4b — TCP binary delegation)

## Key Results

| Condition | Topology | Steady-state miss/20 | CPU |
|---|---|---|---|
| Baseline (no delegation, load=800) | any | 20/20 | 100% |
| TCP delegation, load=800 | 4-node | **0.12/20** (99% reduction) | 79% |
| TCP delegation, load=800 | 5-node | **1.20/20** (94% reduction) | 85% |

Overload boundary: `load=700` → SATURATED (cpu≥90%, miss=0); `load=800` → OVERLOADED (miss>0). Consistent across 1/2/5-node topologies.

## Badges
- Platform: ESP32 / FreeRTOS (unicore)
- Language: C (firmware), Python (experiments/dashboard)
- Broker: Mosquitto MQTT
- License: see `LICENCE`

## Repository Structure

```
firmware-esp32/
  main/
    config/           — compile-time tunables (periods, priorities, MATRIX_SIZE, thresholds)
    core/             — shared system_context_t, CPU metrics (idle-hook)
    network/          — Wi-Fi, MQTT, delegation state machine, TCP work transport
    tasks/            — sensor, control, compute, manager tasks
experiments/
  load_sweep.py           — automated load-sweep harness
  delegation_test.py      — delegation validation orchestrator
  analyze_logs.py         — per-session CSV + plots
  analyze_delegation.py   — delegation timeline and summary CSV
  compare_topologies.py   — cross-topology comparison
  analysis_outputs/       — derived CSV/plots/JSONL (tracked in git)
dashboard/
  app.py                  — Flask observability dashboard + REST API
  telemetry_logs/         — live session JSONL (not committed)
docs/
  contributions.md        — 6 dissertation contributions
  decisions.md            — 22 design decisions (DEC-001–022)
  firmware-architecture.md
  system-state-model.md
  threats-to-validity.md
  formal-grounding.md
serial_logs/              — ESP32 serial captures from Phase 4 runs
run-lab.sh                — one-command experiment harness
RUNBOOK.md                — build/flash reference
```

## Hardware Requirements

- ESP32 development boards (tested with 1, 2, 4, and 5 nodes)
- Shared 2.4GHz Wi-Fi network + host machine on same AP
- Host machine running Mosquitto broker and dashboard

## Quick Start

**1) Build firmware (ESP-IDF in Docker)**
```bash
docker run --rm -it --platform linux/amd64 \
  -v $PWD:/project -w /project/firmware-esp32 \
  espressif/idf:release-v4.4 \
  bash -c "idf.py build && exit"
```

**2) Flash firmware**
```bash
python -m esptool -p /dev/tty.usbserial-XXXX -b 460800 \
  --before default-reset --after hard-reset --chip esp32 \
  write-flash --flash-mode dio --flash-size detect --flash-freq 40m \
  0x1000 firmware-esp32/build/bootloader/bootloader.bin \
  0x8000 firmware-esp32/build/partition_table/partition-table.bin \
  0x10000 firmware-esp32/build/firmware_esp32.bin
```

**3) Start broker + dashboard**
```bash
./run-lab.sh server
```

**4) Run a load sweep**
```bash
./run-lab.sh test --label one-node-bench --min-load 100 --max-load 1000 --step 100 --hold-seconds 20
```

**5) Run delegation validation**
```bash
./run-lab.sh delegation --high-load 800 --low-load 200 --hold-seconds 90
```

## Configuration

Key tunables in `firmware-esp32/main/config/config.h`:

| Constant | Value | Description |
|---|---|---|
| `MATRIX_SIZE` | 30 | Compute kernel size (fixed — all evaluation data at this value) |
| `ACTIVE_BLOCKS` | 20 | Max compute blocks per 100ms cycle |
| `DEFAULT_LOAD_FACTOR` | 1000 | Reference load (eff_blocks = load × PROCESSING_BLOCKS / 1000) |
| `PROCESSING_WINDOW_CYCLES` | 20 | Window size (~2s at 100ms period) |
| `MAX_DELEGATION_CHANNELS` | 4 | Max simultaneous delegation peers |
| `DELEGATION_MAX_INFLIGHT_PER_CHANNEL` | 4 | In-flight cap per channel |
| `WORK_TRANSPORT_PORT` | 5002 | TCP port for binary work transport |
| `DELEGATION_MIN_HEADROOM` | 85% | Min CPU headroom to accept delegation |

**Do not change `MATRIX_SIZE`** — all Phase 1/2 load characterisation data (SAT=700, OVL=800) was collected at size 30.

## Reproducing Dissertation Results

**Phase 1/2 — Load sweep (single/dual/five-node)**
```bash
./run-lab.sh test --label one-node-bench --min-load 100 --max-load 1000 --step 100 --hold-seconds 20
./run-lab.sh test --label two-node-bench --min-load 100 --max-load 1000 --step 100 --hold-seconds 20
./run-lab.sh test --label five-node-bench --min-load 100 --max-load 1000 --step 100 --hold-seconds 20
```

**Phase 4 — TCP delegation validation (canonical: 4-node, load=800)**
```bash
./run-lab.sh delegation --high-load 800 --low-load 200 --hold-seconds 90
```

**Re-run analysis on existing session**
```bash
python experiments/analyze_delegation.py \
  --log-file experiments/analysis_outputs/<label>__<session>/<session>.jsonl \
  --events-file experiments/analysis_outputs/<label>__<session>/delegation_events_<session>.json \
  --out-dir experiments/analysis_outputs/<label>__<session>
```

**Cross-topology comparison**
```bash
python experiments/compare_topologies.py \
  --session "1-node=experiments/analysis_outputs/one-node-bench__<session>/summary_<session>.csv" \
  --session "2-node=experiments/analysis_outputs/two-node-bench__<session>/summary_<session>.csv" \
  --session "5-node=experiments/analysis_outputs/five-node-bench__<session>/summary_<session>.csv"
```

## System State Model

| State | Condition |
|---|---|
| `SCHEDULABLE` | `miss == 0` and `cpu < 90%` |
| `SATURATED` | `miss == 0` and `cpu >= 90%` |
| `OVERLOADED` | `miss > 0` |
| `DEGRADED` | node missing from telemetry beyond failover timeout (dashboard-derived) |

## Delegation Architecture (fw-0.4.0-tcp)

Each stressed node opens up to 4 direct TCP channels to STRESS_LOW peers. Work items (30×30 matrix pairs) are sent as 7208-byte binary frames; results return as 3608-byte frames. The async send queue + priority-1 `work_sender_task` decouples TCP WiFi processing from `compute_task`'s exec window.

Control plane (handshake): MQTT. Data plane (work items): direct TCP peer-to-peer.

See `docs/firmware-architecture.md` for full data flow, frame format, and task resource budget.

## Telemetry Schema

Fields published by each node (`cluster/{node_id}/telemetry`):
- `fw`, `boot_id`, `state`, `load`, `cpu`, `miss`, `blocks`, `eff_blocks`
- `exec_avg`, `exec_max`, `exec_max_p95`, `window_ready`
- `ip` — used by peers to open TCP delegation channels
- `deleg_role`, `deleg_dispatched`, `deleg_returned`, `deleg_busy_skip`, `deleg_timeout_reclaim`, `deleg_dispatch_err`, `deleg_inflight_total`

## Dependencies

- Firmware: ESP-IDF release-v4.4 + FreeRTOS (unicore, `CONFIG_FREERTOS_UNICORE=y`)
- Build: Docker (linux/amd64) for Apple Silicon
- Broker: Mosquitto 2.x
- Dashboard: Python 3.9+, Flask, paho-mqtt
- Analysis: Python 3.9+, pandas, matplotlib

## License

See `LICENCE`.
