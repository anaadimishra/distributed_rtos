# Firmware Architecture

## Module Structure

```mermaid
graph TD
  app_main --> config
  app_main --> core
  app_main --> network
  app_main --> tasks
  tasks --> core
  tasks --> config
  network --> core
  network --> config
  core --> config
```

## Module Annotations

| Module | Responsibility | Uses ESP-IDF APIs? |
|---|---|---|
| `app_main` | system bootstrap, init order, task creation, runtime wiring | Yes |
| `config` | compile-time constants and tunables | No |
| `core` | shared runtime context and metrics logic | Minimal/indirect (metrics hook) |
| `network` | Wi-Fi + MQTT connectivity and control/telemetry transport | Yes |
| `tasks` | periodic runtime workloads (`sensor`, `control`, `compute`, `manager`) | Mostly No (FreeRTOS + shared context) |

Layering rule: ESP-IDF specifics are primarily isolated to `app_main` and `network`, while scheduling/workload logic stays task/core-centric.

## Data Flow

```mermaid
graph LR
  sensor_task[sensor_task] --> q[(queue)]
  q --> control_task[control_task]

  compute_task[compute_task] --> ctx[(system_context)]
  manager_task[manager_task] --> ctx
  ctx --> manager_task

  manager_task --> mqtt_pub[MQTT publish]
  mqtt_pub --> broker[(MQTT broker)]

  broker --> mqtt_ctrl[MQTT control topic]
  mqtt_ctrl --> manager_task

  broker --> dashboard[dashboard]
```

### Notes
- `compute_task` writes execution/miss/load-related stats into `system_context`.
- `manager_task` reads aggregated context and publishes telemetry periodically.
- Control messages originate from dashboard/API via broker and are applied on-node through MQTT control handling.

## Rendering
- This file uses Mermaid blocks that render in GitHub Markdown.
- Export for dissertation figures with Mermaid CLI (`mmdc`), e.g.:
  - `mmdc -i docs/firmware-architecture.md -o docs/figures/firmware-architecture.png`
