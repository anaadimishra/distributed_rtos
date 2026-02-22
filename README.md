## Designing a Fault-Tolerant Edge Cluster

An experimental distributed runtime layer for resource-constrained edge devices using FreeRTOS and ESP32-class hardware.

This project explores how embedded nodes can detect overload, share state, and cooperate without relying on a central master node.

It is being developed as part of a research-oriented engineering dissertation and documented as an ongoing engineering log series.

---

## Project Vision

Most embedded systems assume stable execution and isolated control.

This project challenges that assumption by treating edge devices as distributed systems.

The goal is to design and implement:

* Real-time telemetry exchange between nodes
* Overload detection using CPU and queue metrics
* Peer awareness without centralized coordination
* Task delegation under stress
* Logical failure handling

The dashboard provides observability only.
All system intelligence resides within the nodes.

---

## System Architecture

```
[Node A]      [Node B]      [Node C]
     ↓              ↓              ↓
                MQTT Broker
                     ↓
            Monitoring Dashboard
```

Each node publishes telemetry:

* CPU usage
* Queue depth
* Load factor

The monitoring dashboard subscribes to cluster telemetry but does not coordinate execution.

---

## Repository Structure

```
firmware/            Embedded application (FreeRTOS-based)
dashboard/           Flask-based monitoring interface
docs/                Architecture notes and engineering logs
Dockerfile.esp8266   Reproducible ESP8266 build environment
RUNBOOK.md           Command reference and workflows
```

External SDKs and toolchains are intentionally excluded from version control.

---

## Phase 1: ESP8266 Telemetry Validation

Completed:

* Modular FreeRTOS task structure
* Metrics collection (CPU, queue depth, load factor)
* MQTT telemetry publishing
* Flask-based dashboard
* Dockerized firmware build
* LAN MQTT connectivity

This phase validates the telemetry and observability pipeline.

Tagged release:

```
v0.1-esp8266-baseline
```

---

## Phase 2: ESP32 Migration (In Progress)

Migration to ESP32 using ESP-IDF.

Goals:

* Preserve portable task and core logic
* Rewrite only network layer
* Maintain identical telemetry format
* Prepare for distributed scheduling and delegation

---

## Engineering Log

The project is documented as a running engineering series.

Topics include:

* Toolchain isolation with Docker
* Network binding and MQTT debugging
* Layered architecture design
* Trade-offs in distributed scheduling
* Fault simulation and overload handling

Logs are stored in:

```
docs/engineering-log/
```

---

## Design Principles

* Strict separation of portable logic and SDK-specific APIs
* Reproducible build environments
* Observability before optimization
* No central scheduler unless absolutely necessary
* Treat embedded clusters as distributed systems

---

## Roadmap

1. ESP32 bring-up
2. Heartbeat and node liveness detection
3. Overload detection heuristics
4. Delegation protocol design
5. Fault injection experiments
6. Performance evaluation and analysis
