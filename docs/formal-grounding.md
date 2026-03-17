# Formal Grounding: Real-Time Feasibility + Fault/Safety Model

This document anchors the implementation in standard real‑time systems theory and clarifies the fault/safety model used for evaluation.

---

## 1) Real‑Time Scheduling Assumptions

Current firmware runs on FreeRTOS with **fixed‑priority preemptive scheduling**. Priorities are assigned explicitly per task.

Tasks are periodic with (approximate) periods:

- `sensor_task`: `SENSOR_PERIOD_MS`
- `control_task`: `CONTROL_PERIOD_MS`
- `compute_task`: `COMPUTE_PERIOD_MS`
- `manager_task`: `MANAGER_PERIOD_MS`

We treat each periodic task `i` as having:

- Period `T_i`
- Worst‑case execution time `C_i`
- Deadline `D_i` (assumed `D_i = T_i` unless stated otherwise)

Where `C_i` is estimated empirically from measured execution windows (`exec_max`) for the compute task, and bounded measurement for other tasks if needed.

---

## 2) Utilization Tests (RM/EDF)

### Total Utilization

```
U = sum(C_i / T_i)
```

### EDF Feasibility (Sufficient Condition)

For independent periodic tasks with `D_i = T_i`:

```
U ≤ 1  => feasible under EDF
```

### RM Feasibility (Sufficient Condition)

For `n` periodic tasks with `D_i = T_i`:

```
U ≤ n(2^(1/n) − 1)
```

This bound approaches ~0.693 as `n` grows, but is conservative.

---

## 3) Mapping to Current System

The current design **does not** implement EDF or RM inside the kernel. It uses FreeRTOS fixed‑priority scheduling. However:

- Task priorities are assigned explicitly and can be aligned with RM ordering (shorter period ⇒ higher priority).
- Feasibility tests above provide a **theoretical baseline** for schedulability.

To connect theory ↔ implementation, the evaluation should:

1. Compute empirical `C_i` (from measured `exec_max`, plus bounded estimates for sensor/control/manager).
2. Compute `U` and compare to RM/EDF bounds.
3. Measure actual deadline misses to validate theory vs practice.

---

## 4) Fault & Safety Model

### Fault Model (What Can Go Wrong)

- **Node crash / power loss**: node disappears from MQTT telemetry.
- **Network partition**: broker reachable only by subset of nodes.
- **Overload**: task execution exceeds period (deadline miss).
- **Telemetry drop / delay**: missing or delayed MQTT messages.
- **Control message loss**: a control update is sent but not applied.

### Safety Assumptions

- The dashboard is **observability only**, never a coordinator.
- Nodes are autonomous; failure of a node must not collapse the entire system.
- Overload detection should degrade gracefully (e.g., reduce load or refuse delegation).

### Safety‑Relevant Properties

- **Bounded overload**: detect sustained deadline misses within a defined window.
- **Liveness detection**: identify missing nodes within `N` seconds.
- **No central point of failure**: control logic must remain in nodes.

---

## 5) Evaluation Hooks Needed

To support formal grounding in the dissertation, evaluations should produce:

- Empirical `C_i` per task and derived `U`
- Deadline miss ratios per task
- Comparison of measured schedulability vs RM/EDF bounds
- Failover / liveness detection timing

---

## 6) Notes

This document is a theoretical anchor, not a scheduler implementation. The goal is to show that empirical data from the system can be interpreted using classical feasibility theory, and to clearly articulate the failure model being evaluated.
