# Distributed RTOS Telemetry and Overload Evaluation (ESP32)

A distributed real-time task runtime for ESP32 nodes running FreeRTOS, coordinated via MQTT with no central controller. Current evaluation across 1, 2, and 5-node topologies shows an overload boundary at load factor ~800 with the present compute workload.

## Badges (optional)
- Platform: ESP32 / FreeRTOS
- Language: C
- Broker: Mosquitto MQTT
- License: see `LICENCE`

## Repository Structure
```
firmware-esp32/
  main/
    config/       — task periods, priorities, tunables
    core/         — shared context, CPU metrics
    network/      — Wi-Fi + MQTT (only layer touching ESP-IDF)
    tasks/        — sensor, control, compute, manager tasks
experiments/
  load_sweep.py         — automated load sweep harness
  analyze_logs.py       — CSV + plot generation
  compare_topologies.py — cross-topology comparison
  analysis_outputs/     — derived CSV/plots
  run logs/             — per-session JSON metadata
 dashboard/
  app.py                — Flask observability dashboard
  telemetry_logs/       — session JSONL output (gitignored)
 docs/
  engineering-logs/     — day-by-day logs
  formal-grounding.md   — RTA and fault model
  system-state-model.md
  firmware-architecture.md
  threats-to-validity.md
  contributions.md
run-lab.sh              — one-command test harness
```

## Hardware Requirements
- ESP32 development boards (tested with 1, 2, 4, and 5 nodes)
- Shared Wi-Fi network
- Host machine running Mosquitto broker and dashboard

## Quick Start (4 steps)

**1) Build firmware (ESP-IDF in Docker)**
```bash
cd firmware-esp32
./build.sh  # or see RUNBOOK.md for your exact Docker/IDF setup
```

**2) Flash firmware (example)**
```bash
python -m esptool -p /dev/tty.usbserial-XXXX -b 460800 --before default-reset --after hard-reset --chip esp32 \
  write-flash --flash-mode dio --flash-size detect --flash-freq 40m \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/firmware_esp32.bin
```

**3) Start broker + dashboard**
```bash
./run-lab.sh server
```

**4) Run a load sweep + analysis**
```bash
./run-lab.sh test --label one-node-bench --min-load 100 --max-load 1000 --step 100 --hold-seconds 20
```

## Configuration
Key tunables in `firmware-esp32/main/config/config.h`:

| Constant | Default | Description |
|---|---|---|
| `MATRIX_SIZE` | 22 | Compute kernel size |
| `ACTIVE_BLOCKS` | 20 | Max blocks per cycle |
| `PROCESSING_BLOCKS` | 25 | Scaling cap for load factor |
| `PROCESSING_WINDOW_CYCLES` | 20 | Window size (~2s at 100ms period) |
| `ENABLE_METRICS` | 1 | Toggle CPU profiling |
| `ENABLE_MANAGER_TASK` | 1 | Toggle telemetry task |

## Reproducing Dissertation Results

**Single-node baseline**
```bash
./run-lab.sh test --label one-node-bench
```

**Two-node comparison**
```bash
./run-lab.sh test --label two-node-bench
```

**Five-node comparison**
```bash
./run-lab.sh test --label five-node-bench
```

**Cross-topology comparison**
```bash
python experiments/compare_topologies.py \
  --session "1-node=experiments/analysis_outputs/one-node-bench__session_.../summary_....csv" \
  --session "2-node=experiments/analysis_outputs/two-node-bench__session_.../summary_....csv" \
  --session "5-node=experiments/analysis_outputs/five-node-bench__session_.../summary_....csv"
```

## Key Findings (current)
- Overload boundary (first `miss_p95 > 0`) appears at load ~800 for the current compute kernel.
- Load scaling is deterministic: `eff_blocks = load * PROCESSING_BLOCKS / DEFAULT_LOAD_FACTOR`, capped by `ACTIVE_BLOCKS`.
- Telemetry and state models are observable and reproducible via session-based JSONL logs and analysis outputs.

## Telemetry Schema (node payload)
Typical fields published by each node:
- `fw`, `boot_id`
- `t_pub_ms`, `t_pub_epoch_ms`
- `t_actual_publish_ms`, `t_expected_publish_ms`, `drift_ms`
- `state` (SCHEDULABLE/SATURATED/OVERLOADED)
- `cpu`, `queue`, `load`, `blocks`, `eff_blocks`
- `last_ctrl_seq`
- `exec_avg`, `exec_max`, `miss`, `window_ready`

Dashboard adds derived fields in logs:
- `telemetry_latency_ms`, `ctrl_latency_ms`, `t_rx_ms`

## Fault Injection Controls (Dashboard)
- `REBOOT` — restarts node
- `FAIL_SILENT_ON/OFF` — stop/resume telemetry publish
- `MQTT ON/OFF` — UI alias for fail-silent state

## Dependencies
- Firmware: ESP-IDF + FreeRTOS (bundled)
- Broker: Mosquitto 2.x
- Dashboard: Python 3.9+, Flask, paho-mqtt
- Analysis: Python 3.9+, pandas, matplotlib
- Build: Docker (linux/amd64) for Apple Silicon

## License
See `LICENCE`.
