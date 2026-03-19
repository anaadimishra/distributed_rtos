import json
import logging
import os
import threading
import time
from flask import Flask, jsonify, request, render_template
from mqtt_client import start_mqtt, publish_control

app = Flask(__name__)
_QUIET = os.getenv("DASHBOARD_QUIET", "0") == "1"


def _log(msg):
    if not _QUIET:
        print(msg)

_state = {}
_state_lock = threading.Lock()
_ctrl_sent = {}
_fault_mode = {}
_last_boot = {}
_time_sync = {}
_last_seen = {}
_fail_detected = {}

_FAILOVER_TIMEOUT_SEC = 5

_TIME_SYNC_INTERVAL_SEC = 30
_log_session_id = None
_log_path = None

_LOG_DIR = os.path.join(os.path.dirname(__file__), "telemetry_logs")
os.makedirs(_LOG_DIR, exist_ok=True)


def _start_new_log_session(reason="startup"):
    global _log_session_id, _log_path
    ts = time.strftime("%Y%m%d-%H%M%S")
    _log_session_id = f"session_{ts}"
    _log_path = os.path.join(_LOG_DIR, f"{_log_session_id}.jsonl")
    _log(f"[log] new session id={_log_session_id} reason={reason} path={_log_path}")


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/state", methods=["GET"])
def api_state():
    with _state_lock:
        snapshot = json.loads(json.dumps(_state))
        for node_id in snapshot.keys():
            snapshot[node_id]["fault_mode"] = _fault_mode.get(node_id, "NORMAL")
    _log(f"[api] /api/state snapshot={snapshot}")
    return jsonify(snapshot)


@app.route("/api/logging/status", methods=["GET"])
def logging_status():
    return jsonify({"session_id": _log_session_id})


@app.route("/api/logging/restart", methods=["POST"])
def logging_restart():
    _start_new_log_session(reason="manual")
    return jsonify({"session_id": _log_session_id})


@app.route("/api/control", methods=["POST"])
def control():
    data = request.get_json(silent=True) or {}
    _log(f"[api] /api/control request={data}")
    node = data.get("node")

    if not node:
        return jsonify({"error": "missing node"}), 400

    if node in ("all", "*"):
        with _state_lock:
            targets = sorted(set(_state.keys()) | set(_last_seen.keys()))
        if not targets:
            return jsonify({"error": "no nodes available"}), 400
    else:
        targets = [node]

    payload = None

    if "load" in data:
        seq = int(time.time() * 1000)
        for target in targets:
            _ctrl_sent[target] = {"seq": seq, "t_sent": time.time()}
        payload = {
            "action": "SET_LOAD",
            "value": data["load"],
            "seq": seq
        }

    elif "blocks" in data:
        seq = int(time.time() * 1000)
        for target in targets:
            _ctrl_sent[target] = {"seq": seq, "t_sent": time.time()}
        payload = {
            "action": "SET_BLOCKS",
            "value": data["blocks"],
            "seq": seq
        }
    elif data.get("action") in ("REBOOT", "POWEROFF", "FAIL_SILENT_ON", "FAIL_SILENT_OFF"):
        seq = int(time.time() * 1000)
        payload = {
            "action": data["action"],
            "seq": seq
        }

    else:
        return jsonify({"error": "invalid control"}), 400

    for target in targets:
        publish_control(target, payload)
    _log(f"[api] /api/control dispatched targets={targets} payload={payload}")
    action = payload.get("action")
    if action in ("FAIL_SILENT_ON", "FAIL_SILENT_OFF", "REBOOT", "POWEROFF"):
        with _state_lock:
            for target in targets:
                if action == "FAIL_SILENT_ON":
                    _fault_mode[target] = "SILENT"
                elif action == "FAIL_SILENT_OFF":
                    _fault_mode[target] = "NORMAL"
                elif action == "REBOOT":
                    _fault_mode[target] = "REBOOTING"
                elif action == "POWEROFF":
                    _fault_mode[target] = "POWEROFF"
    return jsonify({"status": "ok", "targets": targets, "payload": payload})


def send_time_sync(node_id):
    seq = int(time.time() * 1000)
    payload = {
        "action": "SYNC_TIME",
        "value": int(time.time() * 1000),
        "seq": seq
    }
    publish_control(node_id, payload)
    _time_sync[node_id] = time.time()
'''def api_control():
    data = request.get_json(silent=True) or {}
    node = data.get("node")
    load = data.get("load")
    if not node or not isinstance(load, int):
        return jsonify({"error": "Invalid payload"}), 400
    publish_control(node, load)
    return jsonify({"status": "ok"})
'''

def update_state(node_id, payload):
    now = time.time()
    last_seq = int(payload.get("last_ctrl_seq", 0))
    latency_ms = 0
    if node_id in _ctrl_sent and _ctrl_sent[node_id]["seq"] == last_seq:
        latency_ms = int((now - _ctrl_sent[node_id]["t_sent"]) * 1000)
    # Telemetry latency: compare node's publish epoch timestamp to receive time.
    # boot_id prevents false spikes after reboot (t_pub_ms resets to 0).
    boot_id = int(payload.get("boot_id", 0))
    t_pub_ms = int(payload.get("t_pub_ms", 0))
    t_pub_epoch_ms = int(payload.get("t_pub_epoch_ms", 0))
    t_rx_ms = int(now * 1000)
    telem_latency_ms = 0
    if node_id not in _last_boot or _last_boot[node_id] != boot_id:
        _last_boot[node_id] = boot_id
    if t_pub_epoch_ms > 0:
        telem_latency_ms = max(0, t_rx_ms - t_pub_epoch_ms)

    # Trigger time sync on first sighting or if interval elapsed.
    if node_id not in _time_sync or (now - _time_sync[node_id]) > _TIME_SYNC_INTERVAL_SEC:
        send_time_sync(node_id)
    with _state_lock:
        _last_seen[node_id] = now
        # Append JSONL telemetry to the current logging session file.
        log_record = {
            "session_id": _log_session_id,
            "node_id": node_id,
            "boot_id": boot_id,
            "t_rx_ms": t_rx_ms,
            "telemetry_latency_ms": telem_latency_ms,
            "ctrl_latency_ms": latency_ms,
            "payload": payload,
        }
        if _log_path is not None:
            with open(_log_path, "a", encoding="utf-8") as f:
                f.write(json.dumps(log_record) + "\n")

        _state[node_id] = {
            "fw": str(payload.get("fw", "")),
            "boot_id": boot_id,
            "cpu": int(payload.get("cpu", 0)),
            "queue": int(payload.get("queue", 0)),
            "load": int(payload.get("load", 0)),
            "eff_blocks": int(payload.get("eff_blocks", 0)),
            "ctrl_latency_ms": latency_ms,
            "telemetry_latency_ms": telem_latency_ms,
            "exec_avg": int(payload.get("exec_avg", 0)),
            "exec_max": int(payload.get("exec_max", 0)),
            "miss": int(payload.get("miss", 0)),
            "blocks": int(payload.get("blocks", 0)),
            "window_ready": int(payload.get("window_ready", 0)),
            "fault_mode": _fault_mode.get(node_id, "NORMAL"),
            "last_seen": time.time(),
        }
        if _fault_mode.get(node_id) in ("REBOOTING", "POWEROFF"):
            # Any telemetry from the node means it is alive again.
            _fault_mode[node_id] = "NORMAL"
            _state[node_id]["fault_mode"] = "NORMAL"
        _log(f"[state] updated node_id={node_id} state={_state}")


@app.route("/api/failover", methods=["GET"])
def api_failover():
    now = time.time()
    events = []
    with _state_lock:
        for node_id, last in _last_seen.items():
            age = now - last
            if age > _FAILOVER_TIMEOUT_SEC:
                if node_id not in _fail_detected:
                    _fail_detected[node_id] = now
                events.append({
                    "node_id": node_id,
                    "status": "failed",
                    "age_sec": round(age, 2),
                    "detected_at": _fail_detected[node_id],
                })
            else:
                events.append({
                    "node_id": node_id,
                    "status": "ok",
                    "age_sec": round(age, 2),
                    "detected_at": _fail_detected.get(node_id),
                })
    return jsonify({"events": events, "timeout_sec": _FAILOVER_TIMEOUT_SEC})


if __name__ == "__main__":
    if _QUIET:
        logging.getLogger("werkzeug").setLevel(logging.ERROR)
        app.logger.disabled = True
    _start_new_log_session(reason="startup")
    start_mqtt(update_state)
    app.run(host="0.0.0.0", port=5000, debug=False)
