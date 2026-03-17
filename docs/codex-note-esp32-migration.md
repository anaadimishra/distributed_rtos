# Codex Note: ESP32 Migration (firmware-esp32)

Date: 2026-02-22

Summary:
- Created a new ESP-IDF v4.4 project in `firmware-esp32/` with standard structure and CMake files.
- Copied `core/`, `tasks/`, and `config/` from `firmware/` unchanged.
- Rewrote only the `network/` layer for ESP-IDF:
  - `wifi.c` uses `esp_netif`, `esp_event`, `esp_wifi`, `WIFI_EVENT`, and `IP_EVENT`.
  - `mqtt.c` uses `esp_mqtt_client` with event handler callback and auto-reconnect enabled.
- `app_main.c` now initializes NVS, `esp_netif`, default event loop, WiFi, MQTT, then creates tasks.
- Telemetry JSON format and MQTT topics remain identical.

Files created/updated:
- `firmware-esp32/CMakeLists.txt`
- `firmware-esp32/main/CMakeLists.txt`
- `firmware-esp32/main/app_main.c`
- `firmware-esp32/main/network/wifi.c`
- `firmware-esp32/main/network/mqtt.c`
- Copied unchanged: `firmware-esp32/main/core/*`, `firmware-esp32/main/tasks/*`, `firmware-esp32/main/config/config.h`

Notes:
- `mqtt_publish_telemetry(ctx, payload)` API preserved.
- Task priorities and scheduling logic unchanged.
- No new features added.
