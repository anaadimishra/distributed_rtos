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
#   ./run-lab.sh test            # run load sweep + analyze (server must be running)
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

# Docker build command (ESP-IDF v4.4)
BUILD_CMD=(
  docker run --rm -it --platform linux/amd64 \
    -v "$PROJ_ROOT:/project" \
    -w /project/firmware-esp32 \
    espressif/idf:release-v4.4 \
    bash -c "idf.py build && exit"
)

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

# ----------------------------------------------------------------------------
# Parse args
# ----------------------------------------------------------------------------

DO_BUILD=false
DO_FLASH=false
DO_SERVER=false
DO_TEST=false
STARTED_SERVER_FOR_TEST=false
TEST_LABEL="single_node"

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
    test) DO_TEST=true ;;
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
    --port)
      shift
      [[ $# -gt 0 ]] || abort "--port requires a value"
      DEVICE_PORTS+=("$1")
      AUTO_DISCOVER=false
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

if $DO_BUILD; then
  require_cmd docker
fi
if $DO_FLASH; then
  require_cmd python
fi
if $DO_SERVER; then
  require_cmd mosquitto
  require_cmd pgrep
fi
if $DO_TEST; then
  require_cmd python
  require_cmd mosquitto
  require_cmd pgrep
fi

# ----------------------------------------------------------------------------
# Build
# ----------------------------------------------------------------------------

if $DO_BUILD; then
  echo "[build] docker build for ESP-IDF"
  ( cd "$PROJ_ROOT" && "${BUILD_CMD[@]}" )
fi

# ----------------------------------------------------------------------------
# Flash
# ----------------------------------------------------------------------------

if $DO_FLASH; then
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
  cd "$prev_dir"
}

stop_server() {
  echo "[stop] stopping mqtt/dashboard"
  kill "${MQTT_PID:-}" "${DASH_PID:-}" 2>/dev/null || true
}

if $DO_SERVER; then
  if $DO_TEST; then
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

if $DO_TEST; then
  # Always restart server for a clean, quiet test run.
  echo "[test] starting mqtt/dashboard"
  start_server
  STARTED_SERVER_FOR_TEST=true
  # Give server a moment to come up.
  sleep 10

  echo "[test] running load sweep (server must be running)"
  ( cd "$PROJ_ROOT" && python experiments/load_sweep.py --repeat 3 --label "$TEST_LABEL" )

  for run_file in "${PROJ_ROOT}/experiments/last_run_"*.json; do
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
    for row in csv.DictReader(f):
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
    fi
  done

  if $STARTED_SERVER_FOR_TEST; then
    stop_server
  fi
fi
