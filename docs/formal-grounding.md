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

## 3.1) Response-Time Analysis (RTA) with Tindell/Burns Iteration

We compute fixed-priority worst-case response time using:

```
R_i^(k+1) = C_i + sum_{j in hp(i)} ceil(R_i^k / T_j) * C_j
```

Convergence when `R_i^(k+1) = R_i^k`. Schedulable if `R_i <= T_i`.

Assumed task parameters for formal grounding:

- `sensor_task`: `T=20ms`, `C=0.5ms` (highest priority)
- `control_task`: `T=50ms`, `C=1.0ms` (2nd)
- `compute_task`: `T=100ms`, `C=10.0ms` at load 800 (3rd)
- `manager_task`: `T=1000ms`, `C=2.0ms` (lowest)

### `sensor_task` (highest priority)

No higher-priority interference:

```
R_sensor = C_sensor = 0.5ms
```

`0.5 <= 20` => schedulable.

### `control_task`

`hp(control) = {sensor}`

```
R0 = C_control = 1.0
R1 = 1.0 + ceil(1.0/20)*0.5 = 1.5
R2 = 1.0 + ceil(1.5/20)*0.5 = 1.5  (converged)
```

`R_control = 1.5ms <= 50ms` => schedulable.

### `compute_task` at `C=10ms` (load 800)

`hp(compute) = {sensor, control}`

```
R0 = 10.0
R1 = 10.0 + ceil(10.0/20)*0.5 + ceil(10.0/50)*1.0
   = 10.0 + 1*0.5 + 1*1.0 = 11.5
R2 = 10.0 + ceil(11.5/20)*0.5 + ceil(11.5/50)*1.0
   = 10.0 + 1*0.5 + 1*1.0 = 11.5  (converged)
```

`R_compute = 11.5ms <= 100ms` => schedulable.

### `manager_task`

`hp(manager) = {sensor, control, compute}`

```
R0 = 2.0
R1 = 2.0 + ceil(2.0/20)*0.5 + ceil(2.0/50)*1.0 + ceil(2.0/100)*10.0
   = 2.0 + 0.5 + 1.0 + 10.0 = 13.5
R2 = 2.0 + ceil(13.5/20)*0.5 + ceil(13.5/50)*1.0 + ceil(13.5/100)*10.0
   = 2.0 + 0.5 + 1.0 + 10.0 = 13.5  (converged)
```

`R_manager = 13.5ms <= 1000ms` => schedulable.

### RTA Summary Table

| Task | `T_i` (ms) | `C_i` (ms) | `R_i` (ms) | Schedulable? |
|---|---:|---:|---:|---|
| sensor_task | 20 | 0.5 | 0.5 | Yes |
| control_task | 50 | 1.0 | 1.5 | Yes |
| compute_task | 100 | 10.0 | 11.5 | Yes |
| manager_task | 1000 | 2.0 | 13.5 | Yes |

### `compute_task` Sensitivity at `C=12ms` (load 900 case)

```
R0 = 12.0
R1 = 12.0 + ceil(12.0/20)*0.5 + ceil(12.0/50)*1.0
   = 12.0 + 1*0.5 + 1*1.0 = 13.5
R2 = 12.0 + ceil(13.5/20)*0.5 + ceil(13.5/50)*1.0
   = 12.0 + 1*0.5 + 1*1.0 = 13.5  (converged)
```

`R_compute(12ms) = 13.5ms <= 100ms` (still schedulable in this simplified model).

Important interpretation:

- With the provided `C_i` values, both utilization and classical fixed-priority RTA remain feasible.
- Empirical overload observed near high loads therefore indicates non-modeled effects (execution-time variance, shared-resource contention, communication jitter, windowed-observation artifacts), not just nominal `U` or single-point `C_i`.
- This is the key insight: **`U` alone is insufficient, and RTA with optimistic/aggregated `C_i` can still miss practical overload behavior**.

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
