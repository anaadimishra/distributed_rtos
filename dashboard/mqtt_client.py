import json
import os
import threading
import time
import paho.mqtt.client as mqtt

_BROKER = "localhost"
_PORT = 1883

_client = None
_callback = None
_QUIET = os.getenv("DASHBOARD_QUIET", "0") == "1"


def _log(msg):
    if not _QUIET:
        print(msg)


def _on_connect(client, userdata, flags, rc):
    if rc == 0:
        _log(f"[mqtt] connected rc={rc}")
        client.subscribe("cluster/+/telemetry")
        _log("[mqtt] subscribed to cluster/+/telemetry")
    else:
        _log(f"[mqtt] connect failed rc={rc}")


def _on_message(client, userdata, msg):
    try:
        _log(f"[mqtt] message received topic={msg.topic} payload={msg.payload}")
        topic = msg.topic.split("/")
        if len(topic) != 3:
            return
        node_id = topic[1]
        payload = json.loads(msg.payload.decode("utf-8"))
        _log(f"[mqtt] parsed node_id={node_id} json={payload}")
    except Exception:
        _log("[mqtt] parse error")
        return
    if _callback:
        _callback(node_id, payload)


def start_mqtt(update_callback, broker=_BROKER, port=_PORT):
    global _client, _callback
    _callback = update_callback
    _client = mqtt.Client()
    _client.on_connect = _on_connect
    _client.on_message = _on_message
    _client.connect(broker, port, keepalive=60)

    thread = threading.Thread(target=_client.loop_forever, daemon=True)
    thread.start()


def publish_control(node, payload):
    if not _client:
        return
    topic = f"cluster/{node}/control"
    _client.publish(topic, json.dumps(payload))
