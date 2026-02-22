import json
import threading
import time
import paho.mqtt.client as mqtt

_BROKER = "localhost"
_PORT = 1883

_client = None
_callback = None


def _on_connect(client, userdata, flags, rc):
    if rc == 0:
        client.subscribe("cluster/+/telemetry")


def _on_message(client, userdata, msg):
    try:
        topic = msg.topic.split("/")
        if len(topic) != 3:
            return
        node_id = topic[1]
        payload = json.loads(msg.payload.decode("utf-8"))
    except Exception:
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


def publish_control(node, load):
    if not _client:
        return
    topic = f"cluster/{node}/control"
    payload = json.dumps({"action": "SET_LOAD", "value": load})
    _client.publish(topic, payload)
