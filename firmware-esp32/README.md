# Distributed RTOS Telemetry (ESP32 + Dashboard)

This repo has two cooperating pieces:

- `main/*`: ESP32 firmware that simulates a small distributed RTOS workload, measures timing/CPU/queue behavior, and publishes telemetry over MQTT. It also listens for control commands to adjust the workload.
- `../dashboard/*`: A small Flask + MQTT web dashboard that subscribes to telemetry and exposes a web UI + REST API to send control commands back to the nodes.

The sections below explain what each part does, why it is shaped this way, and how the pieces connect end-to-end.

**Repo Map**
- `main/app_main.c`: Firmware entry point, bootstraps Wi‑Fi, MQTT, metrics, and FreeRTOS tasks.
- `main/config/config.h`: Central configuration (Wi‑Fi creds, MQTT broker, periods, queue sizes, task priorities, processing limits).
- `main/core/*`: Shared system state (`system_context_t`) and CPU load metrics.
- `main/tasks/*`: RTOS tasks that simulate sensors, control processing, compute workload, and management/telemetry.
- `main/network/*`: Wi‑Fi bring‑up and MQTT publish/subscribe.
- `../dashboard/app.py`: Flask app + REST API + in‑memory state.
- `../dashboard/mqtt_client.py`: MQTT subscribe/publish helper.
- `../dashboard/static/app.js`: Browser UI logic (polls `/api/state`, sends control commands).
- `../dashboard/templates/index.html`: Table UI.
- `../dashboard/mosquitto.conf`: Local broker config.

---

**Firmware (ESP32) Architecture**

The firmware is organized around a shared `system_context_t` that holds node identity, MQTT handles, FreeRTOS primitives, and telemetry fields. This makes it easy for tasks and network code to share state without global variables.

**System Context** (`main/core/system_context.h`)
- Holds the node ID, MQTT topics, and a `QueueHandle_t` for sensor values.
- Stores runtime telemetry fields: CPU usage, queue depth, compute execution timing stats, and active processing blocks.
- Acts as the glue between tasks and the MQTT layer.

**Tasks and Why They Exist**
- `sensor_task` (`main/tasks/sensor_task.c`)
  - Periodically increments a counter and pushes it into a queue.
  - This simulates periodic sensor sampling and generates a load on the queue.
- `control_task` (`main/tasks/control_task.c`)
  - Pulls values off the queue and runs a small spin loop.
  - This simulates lightweight control/actuation workload triggered by incoming data.
- `compute_task` (`main/tasks/compute_task.c`)
  - Repeated matrix‑multiply kernel, executed in “blocks.”
  - This represents a heavy, periodic compute workload. `active_blocks` sets the base block count, and `load_factor` scales it (relative to `DEFAULT_LOAD_FACTOR`).
  - Tracks execution time, windowed averages, max, and deadline misses to observe RTOS scheduling behavior.
- `manager_task` (`main/tasks/manager_task.c`)
  - Computes CPU load using the idle hook, records queue depth, and publishes telemetry via MQTT.
  - This task is the “observability” hub and publishes what the dashboard displays.

**Metrics** (`main/core/metrics.c`)
- Registers a FreeRTOS idle hook to count idle cycles.
- Computes CPU usage as an inverse of idle time (relative baseline). This yields a simple CPU load estimate without extra timers.

**Networking**
- `wifi_init_and_connect` (`main/network/wifi.c`)
  - Sets up STA mode, auto‑reconnect, and uses an event group bit to signal connectivity.
- `mqtt_start` / `mqtt_publish_telemetry` (`main/network/mqtt.c`)
  - Builds a node ID from the ESP32 MAC and derives `cluster/<node>/telemetry` and `cluster/<node>/control` topics.
  - Subscribes to control topic and updates runtime settings when control messages arrive.

---

**Firmware Runtime Flow**
1. `app_main` allocates `system_context_t`, initializes FreeRTOS primitives, NVS, netif, and event loop.
2. Wi‑Fi connects; the app blocks briefly waiting for a connection.
3. Metrics hook is registered and MQTT starts.
4. Four tasks are created and run on periodic schedules:
   - Sensor → enqueue data
   - Control → consume data
   - Compute → heavy kernel + timing stats
   - Manager → publish telemetry

---

**MQTT Topics & Payloads**

**Telemetry** (published by firmware)
- Topic: `cluster/<node_id>/telemetry`
- Example payload:
  ```json
  {
    "cpu": 25,
    "queue": 3,
    "load": 1000,
    "blocks": 8,
    "exec_avg": 12,
    "exec_max": 15,
    "miss": 0
  }
  ```

**Control** (sent by dashboard)
- Topic: `cluster/<node_id>/control`
- Expected payloads:
  ```json
  {"action": "SET_LOAD", "value": 1200}
  {"action": "SET_BLOCKS", "value": 6}
  ```

In the firmware, `SET_LOAD` updates `ctx->load_factor`, and `SET_BLOCKS` is intended to update `ctx->active_blocks`. The compute workload scales with `active_blocks`, so block changes are how you throttle the heavy compute task.

---

**Dashboard Architecture**

The dashboard is a minimal Flask + MQTT consumer that caches the most recent telemetry per node and exposes it via HTTP to the web UI.

**Server** (`../dashboard/app.py`)
- Starts an MQTT listener via `start_mqtt(update_state)`.
- `update_state` stores the latest telemetry per node in `_state` (thread‑safe).
- `/api/state`: returns the current in‑memory snapshot as JSON.
- `/api/control`: validates incoming control requests and publishes MQTT control messages.

**MQTT Client** (`../dashboard/mqtt_client.py`)
- Subscribes to `cluster/+/telemetry` and parses payloads.
- Invokes the provided callback to update shared state.
- `publish_control` publishes a control payload to `cluster/<node>/control`.

**Web UI** (`../dashboard/templates/index.html`, `../dashboard/static/app.js`)
- Polls `/api/state` every second.
- Renders per‑node telemetry and online/offline status (based on last seen timestamp).
- Provides buttons to adjust `load` and `blocks` by sending `/api/control` requests.

---

**End‑to‑End Data Flow**
1. Firmware publishes telemetry to `cluster/<node>/telemetry`.
2. Dashboard MQTT client receives telemetry, parses JSON, and updates `_state`.
3. Browser polls `/api/state` and renders the table.
4. User clicks a control button; UI sends `/api/control`.
5. Flask publishes a control message to `cluster/<node>/control`.
6. Firmware receives it and updates runtime parameters.

---

**Build, Flash, and Run**

Firmware build (ESP‑IDF 4.4, from `instructions.txt`):
```bash
idf.py build
```

Firmware flash (example command from `instructions.txt`):
```bash
/opt/esp/python_env/idf4.4_py3.8_env/bin/python ../../opt/esp/idf/components/esptool_py/esptool/esptool.py -p (PORT) -b 460800 --before default_reset --after hard_reset --chip esp32  write_flash --flash_mode dio --flash_size detect --flash_freq 40m 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/firmware_esp32.bin
```

Dashboard run:
```bash
cd ../dashboard
pip install -r requirements.txt
python app.py
```

MQTT broker (local example, `../dashboard/mosquitto.conf`):
```bash
mosquitto -c ../dashboard/mosquitto.conf
```

---

**Configuration**

Firmware configuration lives in `main/config/config.h`:
- `WIFI_SSID`, `WIFI_PASSWORD`: STA credentials.
- `MQTT_BROKER_HOST`, `MQTT_BROKER_PORT`: MQTT target.
- Task periods, queue size, and compute workload parameters.

Dashboard MQTT config is in `../dashboard/mqtt_client.py`:
- `_BROKER` and `_PORT` default to `localhost:1883`.

For a typical setup:
- ESP32 publishes to your broker IP (`MQTT_BROKER_HOST`).
- The dashboard connects to the same broker (`_BROKER`).

---

**Notes and Current Behavior**

- `SET_LOAD` scales compute cost by adjusting the effective number of blocks (relative to `DEFAULT_LOAD_FACTOR`), while `SET_BLOCKS` sets the base block count.
- Control messages support both `SET_LOAD` and `SET_BLOCKS` end‑to‑end: the firmware parses and applies both actions, and the dashboard publishes the payloads as‑is.
