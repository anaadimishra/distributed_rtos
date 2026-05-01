#!/usr/bin/env bash
set -euo pipefail

# ----------------------------------------------------------------------------
# run-lab.sh
# Build (Docker/ESP-IDF), flash devices, restart MQTT, restart dashboard.
# Usage:
#   ./run-lab.sh                 # build + flash + server
#   ./run-lab.sh build           # only build
#   ./run-lab.sh flash           # only flash
#   ./run-lab.sh server          # only restart MQTT + dashboard (blocking)
#   ./run-lab.sh monitor         # open minicom on a selected/auto-detected serial port
#   ./run-lab.sh test            # run load sweep + analyze (server must be running)
#   ./run-lab.sh baseline        # build with ENABLE_METRICS=0 + baseline sweep (100,500,800)
#   ./run-lab.sh delegation      # run delegation validation experiment (3 nodes recommended)
#   ./run-lab.sh test --label "2nodes-base" --min-load 700 --max-load 1000 --step 50 --hold-seconds 20 --repeat 1
#   ./run-lab.sh delegation --high-load 950 --low-load 400 --hold-seconds 40 --label delegation-bench
#   ./run-lab.sh delegation --victim node-AABBCC --high-load 950 --low-load 400
#   ./run-lab.sh delegation --serial-monitor --label delegation-with-serial
#   ./run-lab.sh delegation --label deleg-failover-run1 --high-load 950 --low-load 200 --hold-seconds 120 --crash-host-after 40 --serial-monitor
#   ./run-lab.sh build flash     # build then flash
#   ./run-lab.sh flash --port /dev/tty.usbserial-0001
# ----------------------------------------------------------------------------

# Project root (adjust if needed)
PROJ_ROOT="/Volumes/GAIA_PRIME/devn/distributed-rtos"
FIRMWARE_DIR="$PROJ_ROOT/firmware-esp32"
DASHBOARD_DIR="$PROJ_ROOT/dashboard"

# Static device list (optional). If empty, auto-discover from /dev/tty.*
DEVICE_PORTS=()
AUTO_DISCOVER=true

# MQTT verbosity: set to 0 for quiet output
MQTT_VERBOSE=0

# ----------------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------------

abort() {
  echo "[error] $*" >&2
  exit 1
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || abort "Missing required command: $1"
}

kill_if_running() {
  local name="$1"
  local pids
  pids=$(pgrep -f "$name" || true)
  if [[ -n "$pids" ]]; then
    echo "[stop] $name (pids: $pids)"
    kill $pids || true
  fi
}

flash_device() {
  local port="$1"
  echo "[flash] device=$port"
  python -m esptool \
    -p "$port" \
    -b 460800 \
    --before default-reset \
    --after hard-reset \
    --chip esp32 \
    write-flash --flash-mode dio --flash-size detect --flash-freq 40m \
    0x1000 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0x10000 build/firmware_esp32.bin
}

build_firmware() {
  local extra_cflags="${1:-}"
  local idf_cmd="idf.py build && exit"
  if [[ -n "$extra_cflags" ]]; then
    idf_cmd="CFLAGS=\"$extra_cflags\" idf.py build && exit"
  fi
  docker run --rm -it --platform linux/amd64 \
    -v "$PROJ_ROOT:/project" \
    -w /project/firmware-esp32 \
    espressif/idf:release-v4.4 \
    bash -c "$idf_cmd"
}

# ----------------------------------------------------------------------------
# Parse args
# ----------------------------------------------------------------------------

DO_BUILD=false
DO_FLASH=false
DO_SERVER=false
DO_MONITOR=false
DO_TEST=false
DO_BASELINE=false
DO_DELEGATION=false
STARTED_SERVER_FOR_TEST=false
TEST_LABEL="single_node"
TEST_MIN_LOAD=100
TEST_MAX_LOAD=1000
TEST_STEP=100
TEST_HOLD_SECONDS=20
TEST_REPEAT=3
TEST_WARMUP_LOAD=100
TEST_WARMUP_SECONDS=10
TEST_LOADS=""
MONITOR_PORT=""
MINICOM_BAUD=115200
SERIAL_MONITOR=false
SERIAL_BAUD=115200
SERIAL_LOG_ROOT="$PROJ_ROOT/serial_logs"
SERIAL_MONITOR_STATE=""
SERIAL_MONITOR_OUT=""
DELEG_HIGH_LOAD=950
DELEG_LOW_LOAD=400
DELEG_WARMUP_SECONDS=30
DELEG_HOLD_SECONDS=40
DELEG_DRAIN_SECONDS=30
DELEG_RECOVERY_SECONDS=30
DELEG_VICTIM=""
DELEG_CRASH_HOST_AFTER=0
DELEG_MIN_NODES=2
DELEG_NODES_TIMEOUT=90
DELEG_NODES_SETTLE_SECONDS=8

if [[ $# -eq 0 ]]; then
  DO_BUILD=true
  DO_FLASH=true
  DO_SERVER=true
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    build) DO_BUILD=true ;;
    flash) DO_FLASH=true ;;
    server) DO_SERVER=true ;;
    monitor) DO_MONITOR=true ;;
    test) DO_TEST=true ;;
    baseline) DO_BASELINE=true ;;
    delegation) DO_DELEGATION=true ;;
    all)
      DO_BUILD=true
      DO_FLASH=true
      DO_SERVER=true
      ;;
    --label)
      shift
      [[ $# -gt 0 ]] || abort "--label requires a value"
      TEST_LABEL="$1"
      ;;
    --min-load)
      shift
      [[ $# -gt 0 ]] || abort "--min-load requires a value"
      TEST_MIN_LOAD="$1"
      ;;
    --max-load)
      shift
      [[ $# -gt 0 ]] || abort "--max-load requires a value"
      TEST_MAX_LOAD="$1"
      ;;
    --step)
      shift
      [[ $# -gt 0 ]] || abort "--step requires a value"
      TEST_STEP="$1"
      ;;
    --loads)
      shift
      [[ $# -gt 0 ]] || abort "--loads requires a value"
      TEST_LOADS="$1"
      ;;
    --hold-seconds)
      shift
      [[ $# -gt 0 ]] || abort "--hold-seconds requires a value"
      TEST_HOLD_SECONDS="$1"
      DELEG_HOLD_SECONDS="$1"
      ;;
    --drain-seconds)
      shift
      [[ $# -gt 0 ]] || abort "--drain-seconds requires a value"
      DELEG_DRAIN_SECONDS="$1"
      ;;
    --recovery-seconds)
      shift
      [[ $# -gt 0 ]] || abort "--recovery-seconds requires a value"
      DELEG_RECOVERY_SECONDS="$1"
      ;;
    --repeat)
      shift
      [[ $# -gt 0 ]] || abort "--repeat requires a value"
      TEST_REPEAT="$1"
      ;;
    --warmup-load)
      shift
      [[ $# -gt 0 ]] || abort "--warmup-load requires a value"
      TEST_WARMUP_LOAD="$1"
      ;;
    --warmup-seconds)
      shift
      [[ $# -gt 0 ]] || abort "--warmup-seconds requires a value"
      TEST_WARMUP_SECONDS="$1"
      ;;
    --port)
      shift
      [[ $# -gt 0 ]] || abort "--port requires a value"
      DEVICE_PORTS+=("$1")
      AUTO_DISCOVER=false
      ;;
    --monitor-port)
      shift
      [[ $# -gt 0 ]] || abort "--monitor-port requires a value"
      MONITOR_PORT="$1"
      ;;
    --baud)
      shift
      [[ $# -gt 0 ]] || abort "--baud requires a value"
      MINICOM_BAUD="$1"
      SERIAL_BAUD="$1"
      ;;
    --serial-monitor)
      SERIAL_MONITOR=true
      ;;
    --serial-log-dir)
      shift
      [[ $# -gt 0 ]] || abort "--serial-log-dir requires a value"
      SERIAL_LOG_ROOT="$1"
      ;;
    --serial-baud)
      shift
      [[ $# -gt 0 ]] || abort "--serial-baud requires a value"
      SERIAL_BAUD="$1"
      ;;
    --high-load)
      shift
      [[ $# -gt 0 ]] || abort "--high-load requires a value"
      DELEG_HIGH_LOAD="$1"
      ;;
    --low-load)
      shift
      [[ $# -gt 0 ]] || abort "--low-load requires a value"
      DELEG_LOW_LOAD="$1"
      ;;
    --victim)
      shift
      [[ $# -gt 0 ]] || abort "--victim requires a value"
      DELEG_VICTIM="$1"
      ;;
    --crash-host-after)
      shift
      [[ $# -gt 0 ]] || abort "--crash-host-after requires a value"
      DELEG_CRASH_HOST_AFTER="$1"
      ;;
    --min-nodes)
      shift
      [[ $# -gt 0 ]] || abort "--min-nodes requires a value"
      DELEG_MIN_NODES="$1"
      ;;
    --nodes-timeout)
      shift
      [[ $# -gt 0 ]] || abort "--nodes-timeout requires a value"
      DELEG_NODES_TIMEOUT="$1"
      ;;
    --nodes-settle-seconds)
      shift
      [[ $# -gt 0 ]] || abort "--nodes-settle-seconds requires a value"
      DELEG_NODES_SETTLE_SECONDS="$1"
      ;;
    --no-auto)
      AUTO_DISCOVER=false
      ;;
    --mqtt-verbose)
      MQTT_VERBOSE=1
      ;;
    -h|--help)
      sed -n '1,20p' "$0"
      exit 0
      ;;
    *) abort "Unknown argument: $1" ;;
  esac
  shift
done

# ----------------------------------------------------------------------------
# Preconditions
# ----------------------------------------------------------------------------

[[ -d "$PROJ_ROOT" ]] || abort "PROJ_ROOT not found: $PROJ_ROOT"
[[ -d "$FIRMWARE_DIR" ]] || abort "FIRMWARE_DIR not found: $FIRMWARE_DIR"
[[ -d "$DASHBOARD_DIR" ]] || abort "DASHBOARD_DIR not found: $DASHBOARD_DIR"

if [[ "$DO_BUILD" == true ]]; then
  require_cmd docker
fi
if [[ "$DO_FLASH" == true ]]; then
  require_cmd python
fi
if [[ "$DO_SERVER" == true ]]; then
  require_cmd mosquitto
  require_cmd pgrep
fi
if [[ "$DO_MONITOR" == true ]]; then
  require_cmd minicom
fi
if [[ "$DO_TEST" == true ]]; then
  require_cmd python
  require_cmd mosquitto
  require_cmd pgrep
fi
if [[ "$DO_BASELINE" == true ]]; then
  require_cmd docker
  require_cmd python
  require_cmd mosquitto
  require_cmd pgrep
fi
if [[ "$DO_DELEGATION" == true ]]; then
  require_cmd python
  require_cmd mosquitto
  require_cmd pgrep
fi

# ----------------------------------------------------------------------------
# Build
# ----------------------------------------------------------------------------

if [[ "$DO_BUILD" == true ]]; then
  echo "[build] docker build for ESP-IDF"
  ( cd "$PROJ_ROOT" && build_firmware "" )
fi

# ----------------------------------------------------------------------------
# Flash
# ----------------------------------------------------------------------------

if [[ "$DO_FLASH" == true ]]; then
  if $AUTO_DISCOVER && [[ ${#DEVICE_PORTS[@]} -eq 0 ]]; then
    # Auto-discover ports (macOS default: /dev/tty.*).
    while IFS= read -r port; do
      DEVICE_PORTS+=("$port")
    done < <(ls /dev/tty.* 2>/dev/null | grep -E "usbserial|usbmodem" || true)
  fi

  if [[ ${#DEVICE_PORTS[@]} -eq 0 ]]; then
    echo "[flash] no devices found. Skipping flash."
  else
    echo "[flash] flashing ${#DEVICE_PORTS[@]} device(s)"
    cd "$FIRMWARE_DIR"
    for port in "${DEVICE_PORTS[@]}"; do
      flash_device "$port"
    done
  fi
fi

# ----------------------------------------------------------------------------
# Monitor (minicom)
# ----------------------------------------------------------------------------

if [[ "$DO_MONITOR" == true ]]; then
  if [[ -z "$MONITOR_PORT" ]]; then
    if [[ ${#DEVICE_PORTS[@]} -eq 0 ]]; then
      while IFS= read -r port; do
        DEVICE_PORTS+=("$port")
      done < <(ls /dev/tty.* 2>/dev/null | grep -E "usbserial|usbmodem" || true)
    fi
    [[ ${#DEVICE_PORTS[@]} -gt 0 ]] || abort "No serial ports found for minicom."
    MONITOR_PORT="${DEVICE_PORTS[0]}"
  fi

  echo "[monitor] starting minicom on $MONITOR_PORT @ ${MINICOM_BAUD} baud"
  exec minicom -D "$MONITOR_PORT" -b "$MINICOM_BAUD"
fi

# ----------------------------------------------------------------------------
# Restart MQTT + Dashboard
# ----------------------------------------------------------------------------

start_server() {
  kill_if_running "mosquitto"
  kill_if_running "python app.py"

  local prev_dir="$PWD"
  cd "$DASHBOARD_DIR"

  echo "[mqtt] starting broker"
  if [[ "$MQTT_VERBOSE" -eq 1 ]]; then
    mosquitto -c mosquitto.conf -v >>/tmp/distributed-rtos-mosquitto.log 2>&1 &
  else
    mosquitto -c mosquitto.conf >>/tmp/distributed-rtos-mosquitto.log 2>&1 &
  fi
  MQTT_PID=$!

  echo "[dashboard] starting Flask app"
  DASHBOARD_QUIET=1 python app.py >>/tmp/distributed-rtos-dashboard.log 2>&1 &
  DASH_PID=$!
  echo "[dashboard] http://localhost:5000"
  cd "$prev_dir"
}

stop_server() {
  echo "[stop] stopping mqtt/dashboard"
  kill "${MQTT_PID:-}" "${DASH_PID:-}" 2>/dev/null || true
}

start_serial_capture() {
  [[ "$SERIAL_MONITOR" == true ]] || return 0

  require_cmd screen
  local stamp
  stamp="$(date +%Y%m%d-%H%M%S)"
  mkdir -p "$SERIAL_LOG_ROOT"
  SERIAL_MONITOR_STATE="$SERIAL_LOG_ROOT/runlab_${stamp}.sessions"
  SERIAL_MONITOR_OUT="$SERIAL_LOG_ROOT/runlab_${stamp}.out"

  echo "[serial] starting detached monitors"
  echo "[serial] output log: $SERIAL_MONITOR_OUT"
  (
    cd "$PROJ_ROOT"
    ./tools/serial-monitors.sh \
      --background \
      --baud "$SERIAL_BAUD" \
      --log-dir "$SERIAL_LOG_ROOT" \
      --state-file "$SERIAL_MONITOR_STATE"
  ) | tee "$SERIAL_MONITOR_OUT"
}

stop_serial_capture() {
  [[ "$SERIAL_MONITOR" == true ]] || return 0
  [[ -n "$SERIAL_MONITOR_STATE" && -f "$SERIAL_MONITOR_STATE" ]] || return 0

  echo "[serial] stopping detached monitors"
  while IFS= read -r session_name; do
    [[ -n "$session_name" ]] || continue
    screen -S "$session_name" -X quit 2>/dev/null || true
  done < "$SERIAL_MONITOR_STATE"
  echo "[serial] stopped sessions listed in $SERIAL_MONITOR_STATE"
}

if [[ "$DO_SERVER" == true ]]; then
  if [[ "$DO_TEST" == true ]]; then
    abort "Cannot run server (blocking) and test together. Use two terminals or run ./run-lab.sh test"
  fi

  start_server

  cleanup() {
    stop_server
  }
  trap cleanup INT TERM

  echo "[running] Press Ctrl+C to stop."
  wait
fi

# ----------------------------------------------------------------------------
# Test (load sweep + analyze)
# ----------------------------------------------------------------------------

if [[ "$DO_TEST" == true ]]; then
  # Always restart server for a clean, quiet test run.
  start_serial_capture
  echo "[test] starting mqtt/dashboard"
  start_server
  STARTED_SERVER_FOR_TEST=true
  cleanup_test_run() {
    stop_serial_capture
    if [[ "$STARTED_SERVER_FOR_TEST" == true ]]; then
      stop_server
    fi
  }
  trap cleanup_test_run EXIT INT TERM
  # Give server a moment to come up.
  sleep 10

  echo "[test] running load sweep (server must be running)"
  RUN_OUT_BASE="experiments/last_run_${TEST_LABEL}_$(date +%Y%m%d-%H%M%S).json"
  SWEEP_CMD=(python experiments/load_sweep.py
      --repeat "$TEST_REPEAT"
      --label "$TEST_LABEL"
      --min-load "$TEST_MIN_LOAD"
      --max-load "$TEST_MAX_LOAD"
      --step "$TEST_STEP"
      --hold-seconds "$TEST_HOLD_SECONDS"
      --warmup-load "$TEST_WARMUP_LOAD"
      --warmup-seconds "$TEST_WARMUP_SECONDS"
      --out "$RUN_OUT_BASE")
  if [[ -n "$TEST_LOADS" ]]; then
    SWEEP_CMD+=(--loads "$TEST_LOADS")
  fi
  ( cd "$PROJ_ROOT" && "${SWEEP_CMD[@]}" )

  if [[ "$TEST_REPEAT" -gt 1 ]]; then
    run_files=( "${PROJ_ROOT}/${RUN_OUT_BASE%.json}"_*.json )
  else
    run_files=( "${PROJ_ROOT}/${RUN_OUT_BASE}" )
  fi

  for run_file in "${run_files[@]}"; do
    session_id=$(python - <<PY
import json
with open("${run_file}", "r", encoding="utf-8") as f:
    print(json.load(f).get("session_id", ""))
PY
)

    if [[ -z "$session_id" ]]; then
      abort "Failed to read session_id from ${run_file}"
    fi

    log_path="${PROJ_ROOT}/experiments/${TEST_LABEL}__${session_id}/${session_id}.jsonl"
    if [[ ! -f "$log_path" ]]; then
      abort "Log not found at $log_path (did load_sweep move it?)"
    fi

    echo "[test] analyzing $log_path"
    ( cd "$PROJ_ROOT" && python experiments/analyze_logs.py --log-file "$log_path" --skip-seconds 2 --window-ready-only --label "$TEST_LABEL" )
    echo "[test] analysis outputs: $PROJ_ROOT/experiments/analysis_outputs/${TEST_LABEL}__${session_id}"

    # If two nodes are present, auto-generate a comparison table.
    summary_csv="${PROJ_ROOT}/experiments/analysis_outputs/${TEST_LABEL}__${session_id}/summary_${session_id}.csv"
    if [[ -f "$summary_csv" ]]; then
      nodes=$(python - <<PY
import csv
nodes=set()
with open("${summary_csv}", "r", encoding="utf-8") as f:
    rows=[line for line in f if not line.startswith("#")]
    for row in csv.DictReader(rows):
        nodes.add(row["node_id"])
print("\\n".join(sorted(nodes)))
PY
)
      node_count=$(echo "$nodes" | sed '/^$/d' | wc -l | tr -d ' ')
      if [[ "$node_count" -eq 2 ]]; then
        node_a=$(echo "$nodes" | sed -n '1p')
        node_b=$(echo "$nodes" | sed -n '2p')
        echo "[test] comparing nodes $node_a vs $node_b"
        ( cd "$PROJ_ROOT" && python experiments/compare_nodes.py --summary "$summary_csv" --node-a "$node_a" --node-b "$node_b" > "experiments/analysis_outputs/${TEST_LABEL}__${session_id}/compare_nodes_${node_a}_vs_${node_b}.csv" )
        echo "[test] node comparison: $PROJ_ROOT/experiments/analysis_outputs/${TEST_LABEL}__${session_id}/compare_nodes_${node_a}_vs_${node_b}.csv"
      fi
      if [[ "$node_count" -ge 2 ]]; then
        echo "[test] generating multi-node aggregate comparison"
        ( cd "$PROJ_ROOT" && python experiments/compare_nodes_multi.py --summary "$summary_csv" )
      fi
    fi
  done

  if [[ "$STARTED_SERVER_FOR_TEST" == true ]]; then
    stop_server
  fi
  stop_serial_capture
  trap - EXIT INT TERM
fi

# ----------------------------------------------------------------------------
# Baseline (no-metrics) run
# ----------------------------------------------------------------------------

if [[ "$DO_BASELINE" == true ]]; then
  BASELINE_LABEL="baseline_no_metrics"
  BASELINE_OUT_DIR="experiments/analysis_outputs/baseline_no_metrics"
  echo "[baseline] building firmware with ENABLE_METRICS=0"
  ( cd "$PROJ_ROOT" && build_firmware "-DENABLE_METRICS=0" )

  start_serial_capture
  echo "[baseline] starting mqtt/dashboard"
  start_server
  STARTED_SERVER_FOR_TEST=true
  cleanup_baseline_run() {
    stop_serial_capture
    if [[ "$STARTED_SERVER_FOR_TEST" == true ]]; then
      stop_server
    fi
  }
  trap cleanup_baseline_run EXIT INT TERM
  sleep 10

  echo "[baseline] running fixed sweep: loads=100,500,800 hold=60s"
  ( cd "$PROJ_ROOT" && python experiments/load_sweep.py \
      --repeat 1 \
      --label "$BASELINE_LABEL" \
      --loads "100,500,800" \
      --hold-seconds 60 \
      --warmup-load 100 \
      --warmup-seconds 10 )

  run_file="${PROJ_ROOT}/experiments/last_run.json"
  if [[ ! -f "$run_file" ]]; then
    run_file=$(ls -1 "${PROJ_ROOT}/experiments/last_run_"*.json 2>/dev/null | tail -n 1 || true)
  fi
  [[ -f "$run_file" ]] || abort "No run metadata file found after baseline sweep"

  session_id=$(python - <<PY
import json
with open("${run_file}", "r", encoding="utf-8") as f:
    print(json.load(f).get("session_id", ""))
PY
)
  [[ -n "$session_id" ]] || abort "Failed to read session_id from ${run_file}"

  log_path="${PROJ_ROOT}/experiments/${BASELINE_LABEL}__${session_id}/${session_id}.jsonl"
  [[ -f "$log_path" ]] || abort "Baseline log not found at $log_path"

  echo "[baseline] analyzing $log_path"
  ( cd "$PROJ_ROOT" && python experiments/analyze_logs.py \
      --log-file "$log_path" \
      --skip-seconds 2 \
      --window-ready-only \
      --label "$BASELINE_LABEL" \
      --out-dir "$BASELINE_OUT_DIR" )

  echo "[baseline] outputs: $PROJ_ROOT/$BASELINE_OUT_DIR"

  if [[ "$STARTED_SERVER_FOR_TEST" == true ]]; then
    stop_server
  fi
  stop_serial_capture
  trap - EXIT INT TERM
fi

# ----------------------------------------------------------------------------
# Delegation validation experiment
# ----------------------------------------------------------------------------

if [[ "$DO_DELEGATION" == true ]]; then
  start_serial_capture
  echo "[delegation] starting mqtt/dashboard"
  start_server
  STARTED_SERVER_FOR_TEST=true
  cleanup_delegation_run() {
    stop_serial_capture
    if [[ "$STARTED_SERVER_FOR_TEST" == true ]]; then
      stop_server
    fi
  }
  trap cleanup_delegation_run EXIT INT TERM
  sleep 10

  VICTIM_ARG=()
  if [[ -n "$DELEG_VICTIM" ]]; then
    VICTIM_ARG=(--victim "$DELEG_VICTIM")
  fi

  CRASH_ARG=()
  if [[ "$DELEG_CRASH_HOST_AFTER" -gt 0 ]]; then
    CRASH_ARG=(--crash-host-after "$DELEG_CRASH_HOST_AFTER")
  fi

  echo "[delegation] high-load=${DELEG_HIGH_LOAD} low-load=${DELEG_LOW_LOAD} hold=${DELEG_HOLD_SECONDS}s drain=${DELEG_DRAIN_SECONDS}s label=${TEST_LABEL} min_nodes=${DELEG_MIN_NODES} nodes_timeout=${DELEG_NODES_TIMEOUT}s settle=${DELEG_NODES_SETTLE_SECONDS}s crash-host-after=${DELEG_CRASH_HOST_AFTER}s"
  ( cd "$PROJ_ROOT" && python experiments/delegation_test.py \
      --base-url http://localhost:5000 \
      --high-load "$DELEG_HIGH_LOAD" \
      --low-load  "$DELEG_LOW_LOAD" \
      --warmup-seconds "$DELEG_WARMUP_SECONDS" \
      --hold-seconds   "$DELEG_HOLD_SECONDS" \
      --drain-seconds    "$DELEG_DRAIN_SECONDS" \
      --recovery-seconds "$DELEG_RECOVERY_SECONDS" \
      --min-nodes "$DELEG_MIN_NODES" \
      --nodes-timeout "$DELEG_NODES_TIMEOUT" \
      --nodes-settle-seconds "$DELEG_NODES_SETTLE_SECONDS" \
      --label "$TEST_LABEL" \
      "${VICTIM_ARG[@]+"${VICTIM_ARG[@]}"}" \
      "${CRASH_ARG[@]+"${CRASH_ARG[@]}"}" )

  if [[ "$STARTED_SERVER_FOR_TEST" == true ]]; then
    stop_server
  fi
  stop_serial_capture
  trap - EXIT INT TERM
fi
