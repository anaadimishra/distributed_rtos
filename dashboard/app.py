import json
import threading
import time
from flask import Flask, jsonify, request, render_template
from mqtt_client import start_mqtt, publish_control

app = Flask(__name__)

_state = {}
_state_lock = threading.Lock()


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/state", methods=["GET"])
def api_state():
    with _state_lock:
        snapshot = json.loads(json.dumps(_state))
    return jsonify(snapshot)


@app.route("/api/control", methods=["POST"])
def api_control():
    data = request.get_json(silent=True) or {}
    node = data.get("node")
    load = data.get("load")
    if not node or not isinstance(load, int):
        return jsonify({"error": "Invalid payload"}), 400
    publish_control(node, load)
    return jsonify({"status": "ok"})


def update_state(node_id, payload):
    now = time.time()
    with _state_lock:
        _state[node_id] = {
            "cpu": int(payload.get("cpu", 0)),
            "queue": int(payload.get("queue", 0)),
            "load": int(payload.get("load", 0)),
            "last_seen": now,
        }


if __name__ == "__main__":
    start_mqtt(update_state)
    app.run(host="0.0.0.0", port=5000, debug=False)
