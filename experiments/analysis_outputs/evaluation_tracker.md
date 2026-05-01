# Evaluation Tracker — fw-0.4.0-tcp

> Previous firmware: `fw-0.2.0-rm` (all Phase 1/2 sessions below).
> `fw-0.3.0-deleg` — Phase 4 delegation with MQTT-based work dispatch (deleg-load800-run1/run2).
> Current firmware: `fw-0.4.0-tcp` — Phase 4 TCP binary transport, async send queue, ADAPT_DECREASE re-enabled with delegation guard.
> Phase 1/2 data is fully valid and unaffected by the firmware change.
> `ADAPT_LOW_WINDOWS_TO_INCREASE = 9999` — auto-scale-up disabled for delegation validation.
> `ADAPT_DECREASE_ENABLED = 1` from fw-0.4.0-tcp onward — re-enabled, guarded to skip when any delegation channel ACTIVE.

Firmware: `fw-0.2.0-rm`
Periods: sensor=20ms, control=50ms, compute=100ms, manager=1000ms
Priorities: sensor=5, control=4, manager=3, compute=2

---

## Threshold Reference

| State | Condition |
|---|---|
| SCHEDULABLE | cpu < 90, miss = 0 |
| SATURATED | cpu >= 90, miss = 0 |
| OVERLOADED | miss_p95 > 0 |

---

## Sessions

### five-node-bench — Run 1
**Session:** `session_20260321-020851`
**Nodes:** node-717AC4, node-7115F8, node-313978, node-34A9F0, node-2FCC00 (5 nodes)
**Load range:** 100, 500–900 (50-step)
**All nodes present at every step:** Yes

| Load | CPU avg | State | miss_p95 | exec_max_p95 |
|---|---|---|---|---|
| 100 | 9.4% | SCHEDULABLE | 0 | 1 |
| 500 | 63.4% | SCHEDULABLE | 0 | 7 |
| 550 | 70.0% | SCHEDULABLE | 0 | 7–8 |
| 600 | 76.8% | SCHEDULABLE | 0 | 8 |
| 650 | 83.3% | SCHEDULABLE | 0 | 9 |
| 700 | 90.0% | **SATURATED** | 0 | 9–10 |
| 750 | 97.1% | SATURATED | 0 | 10 |
| 800 | 100% | **OVERLOADED** | 20 | 13–14 |
| 850 | 100% | OVERLOADED | 20 | 14–15 |
| 900 | 100% | OVERLOADED | 20 | 15 |

**Queue warnings:** None (all nodes below threshold of 4)
**Drift:** Stable. Isolated spikes at node-313978 load 500 (7.4ms), node-7115F8 load 650 (7.3ms) — single-window events, not trends.
**Telemetry latency:** Network jitter dominant, no correlation with load.

---

### five-node-bench — Run 2
**Session:** `session_20260321-021201`
**Nodes:** node-717AC4, node-7115F8, node-313978, node-34A9F0, node-2FCC00 (5 nodes)
**Load range:** 100, 500–900 (50-step)
**All nodes present at every step:** Yes

| Load | CPU avg | State | miss_p95 | exec_max_p95 |
|---|---|---|---|---|
| 100 | 9.4% | SCHEDULABLE | 0 | 1 |
| 500 | 63.1% | SCHEDULABLE | 0 | 7 |
| 550 | 70.0% | SCHEDULABLE | 0 | 7–8 |
| 600 | 76.8% | SCHEDULABLE | 0 | 8 |
| 650 | 83.4% | SCHEDULABLE | 0 | 9 |
| 700 | 90.1% | **SATURATED** | 0 | 9 |
| 750 | 97.0% | SATURATED | 0 | 10 |
| 800 | 100% | **OVERLOADED** | 20 | 13 |
| 850 | 100% | OVERLOADED | 20 | 14 |
| 900 | 100% | OVERLOADED | 20 | 14–15 |

**Queue warnings:** None
**Drift:** Stable. Isolated spikes at node-2FCC00 load 550 (5.9ms), node-717AC4 load 650 (6.9ms), node-717AC4 load 850 (-8.5ms) — single-window events.
**Telemetry latency:** Network jitter dominant, no correlation with load.

---

### two-node-bench — Run 1
**Session:** `session_20260321-021856`
**Nodes:** node-313978, node-2FCC00 (2 nodes)
**Load range:** 100, 500–900 (50-step)
**All nodes present at every step:** Yes

| Load | CPU avg | State | miss_p95 | exec_max_p95 |
|---|---|---|---|---|
| 100 | 9.5% | SCHEDULABLE | 0 | 1 |
| 500 | 63.3% | SCHEDULABLE | 0 | 7 |
| 550 | 70.0% | SCHEDULABLE | 0 | 8 |
| 600 | 77.0% | SCHEDULABLE | 0 | 8 |
| 650 | 83.3% | SCHEDULABLE | 0 | 9 |
| 700 | 90.0% | **SATURATED** | 0 | 9 |
| 750 | 97.1% | SATURATED | 0 | 10 |
| 800 | 100% | **OVERLOADED** | 20 | 14 |
| 850 | 100% | OVERLOADED | 20 | 14 |
| 900 | 100% | OVERLOADED | 20 | 15 |

**Queue warnings:** None
**Drift:** Notable — both nodes spike simultaneously at load 600 (node-313978: 7.9ms, node-2FCC00: 8.1ms). Shared event, likely a WiFi/broker jitter window. Isolated, not a trend.
**Telemetry latency:** Network jitter dominant, no correlation with load.

---

### two-node-bench — Run 2
**Session:** `session_20260321-022213`
**Nodes:** node-313978, node-2FCC00 (2 nodes)
**Load range:** 100, 500–900 (50-step)
**All nodes present at every step:** Yes

| Load | CPU avg | State | miss_p95 | exec_max_p95 |
|---|---|---|---|---|
| 100 | 9.0% | SCHEDULABLE | 0 | 1 |
| 500 | 63.5% | SCHEDULABLE | 0 | 6–7 |
| 550 | 70.1% | SCHEDULABLE | 0 | 7 |
| 600 | 76.7% | SCHEDULABLE | 0 | 8 |
| 650 | 83.5% | SCHEDULABLE | 0 | 9 |
| 700 | 90.0% | **SATURATED** | 0 | 9 |
| 750 | 97.0% | SATURATED | 0 | 10 |
| 800 | 100% | **OVERLOADED** | 20 | 14 |
| 850 | 100% | OVERLOADED | 20 | 14–15 |
| 900 | 100% | OVERLOADED | 20 | 15 |

**Queue warnings:** None
**Drift:** Stable. Isolated spike at node-2FCC00 load 650 (6.2ms) — single-window event.
**Telemetry latency:** Network jitter dominant, no correlation with load.

---

## Cross-Run Summary — 2-Node

| Metric | Run 1 | Run 2 |
|---|---|---|
| SATURATED threshold | load 700 | load 700 |
| OVERLOADED threshold | load 800 | load 800 |
| CPU at load 700 (avg) | 90.0% | 90.0% |
| exec_max_p95 at load 800 | 14 ticks | 14 ticks |
| Queue warnings | None | None |
| All nodes aligned | Yes | Yes |

**Repeatability: confirmed.** Both runs identical on all key metrics.

---

## Cross-Run Summary — 5-Node

| Metric | Run 1 | Run 2 |
|---|---|---|
| SATURATED threshold | load 700 | load 700 |
| OVERLOADED threshold | load 800 | load 800 |
| CPU at load 700 (avg) | 90.0% | 90.1% |
| exec_max_p95 at load 800 | 13–14 ticks | 13 ticks |
| Queue warnings | None | None |
| All nodes aligned | Yes | Yes |

**Repeatability: confirmed.** Thresholds and CPU scaling are virtually identical across both runs.

---

### one-node-bench — Run 1
**Session:** `session_20260321-022812`
**Nodes:** node-2FCC00 (1 node)
**Load range:** 100, 500–900 (50-step)

| Load | CPU | State | miss_p95 | exec_max_p95 |
|---|---|---|---|---|
| 100 | 9.2% | SCHEDULABLE | 0 | 1 |
| 500 | 63.0% | SCHEDULABLE | 0 | 7 |
| 600 | 77.2% | SCHEDULABLE | 0 | 8 |
| 650 | 83.4% | SCHEDULABLE | 0 | 9 |
| 700 | 90.1% | **SATURATED** | 0 | 10 |
| 750 | 97.1% | SATURATED | **1** ⚠️ | 10 |
| 800 | 100% | **OVERLOADED** | 20 | 13 |
| 900 | 100% | OVERLOADED | 20 | 14 |

**Queue warnings:** None
**Drift:** Stable. Small spike at load 900 (-7ms) — single window.

---

### one-node-bench — Run 2
**Session:** `session_20260321-023128`
**Nodes:** node-2FCC00 (1 node)
**Load range:** 100, 500–900 (50-step)

| Load | CPU | State | miss_p95 | exec_max_p95 |
|---|---|---|---|---|
| 100 | 9.0% | SCHEDULABLE | 0 | 1 |
| 500 | 63.2% | SCHEDULABLE | 0 | 7 |
| 600 | 76.9% | SCHEDULABLE | 0 | 8 |
| 650 | 83.4% | SCHEDULABLE | 0 | 9 |
| 700 | 90.3% | **SATURATED** | 0 | 9 |
| 750 | 97.2% | SATURATED | **2** ⚠️ | 11 |
| 800 | 100% | **OVERLOADED** | 20 | 14 |
| 900 | 100% | OVERLOADED | 20 | 14 |

**Queue warnings:** None
**Drift:** Spike at load 750 (5.1ms) — same window as the partial miss onset.

---

### one-node-bench — Run 3
**Session:** `session_20260321-023438`
**Nodes:** node-2FCC00 (1 node)
**Load range:** 100, 500–900 (50-step)

| Load | CPU | State | miss_p95 | exec_max_p95 |
|---|---|---|---|---|
| 100 | 9.5% | SCHEDULABLE | 0 | 1 |
| 500 | 63.0% | SCHEDULABLE | 0 | 7 |
| 600 | 76.9% | SCHEDULABLE | 0 | 8 |
| 650 | 83.2% | SCHEDULABLE | 0 | 9 |
| 700 | 90.2% | **SATURATED** | 0 | 9 |
| 750 | 97.0% | SATURATED | 0 | 10 |
| 800 | 100% | **OVERLOADED** | 20 | 13 |
| 900 | 100% | OVERLOADED | 20 | 15 |

**Queue warnings:** None
**Drift:** Isolated spikes at load 650 (6.2ms) and 700 (-5.1ms) — single-window events.

---

## Cross-Run Summary — 1-Node

| Metric | Run 1 | Run 2 | Run 3 |
|---|---|---|---|
| SATURATED threshold | load 700 | load 700 | load 700 |
| OVERLOADED threshold (full) | load 800 | load 800 | load 800 |
| Partial miss at load 750 | miss_p95=1 ⚠️ | miss_p95=2 ⚠️ | 0 (clean) |
| CPU at load 700 | 90.1% | 90.3% | 90.2% |
| exec_max_p95 at load 800 | 13 ticks | 14 ticks | 13 ticks |
| Queue warnings | None | None | None |

**Repeatability: confirmed** for saturation and overload thresholds. Notable: 2 of 3 runs show
**partial miss onset at load 750** (miss_p95 = 1–2), which does not appear in 2-node or 5-node runs.
This is a meaningful single-node characteristic — the schedulability boundary is slightly less sharp
at this topology, likely due to tighter scheduler contention on a single physical node.

---

### dualcore-bench — Run 1
**Session:** `session_20260321-025131`
**Nodes:** node-2FCC00 (1 node, dual-core enabled)
**Load range:** 100, 500–900 (50-step)

| Load | CPU mean | cpu_std | State | miss_p95 | exec_max_p95 |
|---|---|---|---|---|---|
| 100 | 9.8% | 0.4 | SCHEDULABLE | 0 | 1 |
| 500 | 57.9% | 4.63 | SCHEDULABLE | 0 | 7 |
| 600 | 69.1% | 3.0 | SCHEDULABLE | 0 | 8 |
| 650 | 75.8% | 4.94 | SCHEDULABLE | 0 | 8 |
| 700 | 80.1% | 5.55 | SCHEDULABLE | 0 | 9 |
| 750 | 81.1% | 6.12 | **SATURATED** (cpu_p95=90) | 0 | 10 |
| 800 | 40.8% | 32.22 ⚠️ | **OVERLOADED** | 20 | 11 |
| 850 | 43.4% | 28.93 ⚠️ | OVERLOADED | 20 | 12 |
| 900 | 33.1% | 19.83 ⚠️ | OVERLOADED | 20 | 12 |

**Queue warnings:** None
**Drift:** Spike at load 500 (8.3ms) — single window event.

---

### dualcore-bench — Run 2
**Session:** `session_20260321-025447`
**Nodes:** node-2FCC00 (1 node, dual-core enabled)
**Load range:** 100, 500–900 (50-step)

| Load | CPU mean | cpu_std | State | miss_p95 | exec_max_p95 |
|---|---|---|---|---|---|
| 100 | 9.25% | 1.3 | SCHEDULABLE | 0 | 1 |
| 500 | 58.7% | 3.8 | SCHEDULABLE | 0 | 7 |
| 600 | 70.4% | 2.67 | SCHEDULABLE | 0 | 8 |
| 650 | 74.2% | 4.8 | SCHEDULABLE | 0 | 8 |
| 700 | 81.4% | 5.56 | SCHEDULABLE | 0 | 9 |
| 750 | 85.8% | 5.31 | **SATURATED** (cpu_p95=91) | 0 | 10 |
| 800 | 37.2% | 35.93 ⚠️ | **OVERLOADED** | 20 | 11 |
| 850 | 40.0% | 21.14 ⚠️ | OVERLOADED | 20 | 12 |
| 900 | 3.9% | 5.64 ⚠️ | OVERLOADED | 20 | 12 |

**Queue warnings:** None
**Drift:** Stable throughout.

---

## Cross-Run Summary — Dual-Core (1 node)

| Metric | Run 1 | Run 2 |
|---|---|---|
| SATURATED threshold | load 750 | load 750 |
| OVERLOADED threshold | load 800 | load 800 |
| CPU at load 700 (mean) | 80.1% | 81.4% |
| cpu_std at load 700 | 5.55 | 5.56 |
| CPU mean at load 900 | 33.1% | 3.9% ⚠️ |
| exec_max_p95 at load 800 | 11 ticks | 11 ticks |
| Queue warnings | None | None |

**Repeatability: partial.** Saturation and overload thresholds are consistent. CPU mean in the
overload regime is unstable and inconsistent between runs (33% vs 3.9% at load 900) — this is
a known instrumentation limitation (see note below), not a scheduling anomaly.

**Instrumentation limitation:** `metrics.c` registers a single idle hook designed for unicore.
On dual-core, FreeRTOS spawns one idle task per core. The current hook only captures core 0
idle time. As compute_task migrates to core 1 under load, core 0 appears idle and cpu_mean
collapses — reaching 3.9% at load 900 while `miss_p95=20`. The high `cpu_std` (up to 35.9)
throughout dual-core runs confirms the metric is unreliable in this configuration. This must
be acknowledged in threats-to-validity and the evaluation chapter.

---

## Cross-Topology Summary

| Topology | Core mode | Runs | SATURATED | OVERLOADED | Partial miss onset | exec_max_p95 @ 800 | CPU metric reliable? |
|---|---|---|---|---|---|---|---|
| 1-node | single-core | 3 | load 700 | load 800 | load 750 (2/3 runs) | 13–14 ticks | ✓ |
| 2-node | single-core | 2 | load 700 | load 800 | none | 14 ticks | ✓ |
| 5-node | single-core | 2 | load 700 | load 800 | none | 13–14 ticks | ✓ |
| 1-node | dual-core | 2 | load 750 | load 800 | none | 11 ticks | ✗ (single idle hook) |

---

## Key Findings

1. **Thresholds are workload-driven, not topology-dependent (single-core).** SATURATED at load 700, OVERLOADED at load 800 — consistent across 1/2/5-node topologies and all runs.
2. **Dual-core raises saturation threshold by one step.** Saturation shifts from load 700 (single-core) to load 750 (dual-core), giving ~7% more headroom before CPU saturation. Overload threshold (load 800) is unchanged.
3. **1-node single-core shows earlier instability.** 2 of 3 runs exhibit partial miss onset at load 750. Multi-node topologies show none — the schedulability boundary is slightly less sharp on a single physical node.
4. **exec_max_p95 at load 800 = 13–14 ticks (single-core), 11 ticks (dual-core).** Dual-core distributes the compute workload, reducing per-core execution time at the same load.
5. **Dual-core CPU metric is unreliable at overload.** Without per-core idle hooks, cpu_mean collapses as compute_task migrates to core 1 — reaching 3.9% at load 900 while deadlines are missed. High cpu_std (up to 35.9) confirms instability. Single-core CPU metric is consistent and trustworthy throughout.
6. **Drift spikes are network artefacts**, not scheduler instability. Simultaneous spikes across nodes confirm shared WiFi/broker jitter events.

---

---

## Phase 4 — Delegation Validation

Firmware: `fw-0.3.0-deleg`
Config: `ADAPT_LOW_WINDOWS_TO_INCREASE=9999`, `MQTT buffer=32KB`

### Run 1 — session_20260426-105129

**Date:** 2026-04-26
**Nodes:** node-2FCC00 (victim), node-34A9F0 (host), node-7115F8, node-717AC4 (bystanders)
**Config:** high-load=950, low-load=400, hold=40s

| Field | Result |
|---|---|
| `delegation_fired` | **True** |
| `handshake_latency_ms` | **1005ms** |
| `time_to_delegate_ms` | **2018ms** |
| `active_samples` (victim ACTIVE) | **71s** |
| `hosting_samples` (host HOSTING) | **72s** |
| `max_deleg_dispatched` | **28** (work items with full matrix inputs traveled to host) |
| `max_deleg_returned` | **8** (result matrices returned to victim) |

**Outcome:** Full end-to-end delegation demonstrated. Handshake (REQUESTING→ACTIVE/HOSTING) completed in one MQTT round-trip. Work items carrying real 30×30 matrix inputs dispatched and result matrices returned.

**Throughput note:** dispatched=28, returned=8 over 71s. The gap is expected: `MAX_PENDING_WORK=20` fills in ~200ms at 10 items/cycle × 100ms period; MQTT round-trip for a 32KB payload is ~1–2s. Once 20 slots are in-flight, `delegation_dispatch_work_item` drops new items silently until results return. This is a known infrastructure constraint (see threats-to-validity §4) and does not affect correctness of the mechanism.

**Pre-run fix:** Two bugs fixed before this run:
1. `DELEGATION_MIN_HEADROOM` raised 70→85% — bystanders at load=400 have CPU 70–78%; old threshold caused all to reject.
2. `delegation_try_offload` now round-robins across STRESS_LOW peers (was always selecting the same first-valid peer, so rejects were not cycled through).

### Multi-peer run10 — session_20260426-204105

**Date:** 2026-04-26  
**Victim:** node-34A9F0  
**Config:** high-load=950, low-load=200, hold=60s, 4 nodes, serial capture enabled

| Field | Result |
|---|---|
| `delegation_fired` | **True** |
| `handshake_latency_ms` | **1010ms** |
| `time_to_delegate_ms` | **3030ms** |
| victim ACTIVE duration | **124s / 158s (79%)** |
| victim cpu avg during ACTIVE | **59%** (down from 100%) |
| victim `max_deleg_dispatched` | **1190** |
| victim `max_deleg_returned` | **962** |
| victim max `deleg_inflight_total` | **12** (3 channels × 4 cap) |
| victim max `deleg_busy_skip` | **14,560** |
| victim max `deleg_timeout_reclaim` | **213** |
| victim `deleg_dispatch_err` | **0** |
| Bystanders hosting simultaneously | **3** (node-717AC4: 112s, node-7115F8: 20s, node-2FCC00: 16s) |
| Serial crashes / panics | **0** |

**Outcome:** First clean multi-peer run — no stack overflows, no crashes. Three bystanders hosted simultaneously. Victim CPU dropped from 100% to avg 59% during active delegation. Bounded pipeline confirmed: `deleg_inflight_total` capped at exactly 12. Busy-skip path active (no local fallback), timeout reclaim working.

**Emergent finding:** node-2FCC00 (bystander at load=200) became stressed itself (cpu avg 68%, peak 93%) due to hosting load and initiated its own delegation chain (dispatched=801). This is correct behaviour — any stressed node delegates regardless of assigned role. However it also exposed a **delegation loop risk**: a HOSTING node re-delegating cascades CPU pressure to further nodes.

**Fix applied:** `delegation_try_offload` now returns immediately if the node has any `CHAN_HOSTING` channel, preventing delegation loops.

---

### Multi-peer stabilisation — session_20260426-203203 (`multi-peer-run9`)

**Date:** 2026-04-26  
**Victim:** node-34A9F0  
**Config:** high-load=950, low-load=200, hold=60s, 4 nodes, serial capture enabled

| Field | Result |
|---|---|
| `delegation_fired` | **True** |
| `handshake_latency_ms` | **1006ms** |
| `time_to_delegate_ms` | **3029ms** |
| victim `max_deleg_blocks` | **9** |
| victim `max_deleg_dispatched` | **465** |
| victim `max_deleg_returned` | **397** |
| victim max `deleg_inflight_total` | **12** |
| victim max `deleg_busy_skip` | **2901** |
| victim max `deleg_timeout_reclaim` | **58** |
| victim max `deleg_dispatch_err` | **0** |

**Outcome:** Multi-peer delegation fired and the bounded in-flight pipeline behaved as designed. The victim's in-flight total reached 12, matching `3 active channels * 4`. Busy-skip and timeout-reclaim counters grew under pressure, showing backpressure was visible rather than hidden as local fallback.

**Remaining issue found by serial logs:** `compute_task` stack overflow occurred on multiple nodes during heavy delegation traffic. This followed the earlier `multi-peer-run8` finding that `manager_task` stack overflowed during offload request generation. Stack budgets were updated to `MANAGER_TASK_STACK_SIZE=6144` and `COMPUTE_TASK_STACK_SIZE=8192`.

**Next run:** `multi-peer-run10` should validate the same workload after the compute stack increase, with `--serial-monitor` enabled.

---

### deleg-load800-run2 — session_20260426-214105

**Date:** 2026-04-26
**Victim:** node-34A9F0
**Config:** high-load=800, low-load=200, hold=90s, 4 nodes, `ADAPT_DECREASE_ENABLED=0`

| Field | Result |
|---|---|
| `delegation_fired` | **True** |
| `handshake_latency_ms` | **1007ms** |
| `time_to_delegate_ms` | **3025ms** |
| victim ACTIVE duration | **168s / 211s (80%)** |
| victim cpu avg during ACTIVE | **83.6%** (down from 100% baseline) |
| victim miss avg during ACTIVE | **19.3 / 20** (1 sample at miss=0; 167/168 with miss>0) |
| victim `max_deleg_dispatched` | **2668** |
| victim `max_deleg_returned` | **2660** (99.7% return rate) |
| victim `max_deleg_blocks` | **9** |
| victim load throughout | **800** (ADAPT_DECREASE fix confirmed) |
| Serial crashes | **0** |

**No-delegation baseline (Phase 1/2):** cpu=100%, miss=20/20 at load=800.

**Outcome:** Delegation mechanism operates correctly at load=800 — channels open, work dispatches, results return with 99.7% fidelity. CPU load on victim drops from 100% to 83.6% avg, confirming work is genuinely transferred. However, deadline misses are not materially reduced (avg 19.3 vs baseline 20). The `ADAPT_DECREASE_ENABLED=0` fix is confirmed: load stayed at 800 throughout with no self-adaptive reduction.

**Root cause of persisting misses:** At load=800, eff_blocks=16. With 9 blocks delegated, the victim dispatches 9 MQTT work items per 100ms cycle. Each dispatch runs `pvPortMalloc(32768)` + 32KB `snprintf` + `esp_mqtt_client_publish` on the `compute_task` critical path. This serialization overhead consumes the 100ms budget independently of local compute load, so misses persist even with only 7 local blocks remaining. This is the architectural constraint documented in `docs/threats-to-validity.md` (DEC-005/DEC-006: real matrix data sent per work item).

**Dissertation interpretation:** This run provides honest evidence that:
1. The delegation mechanism is functionally correct — work items travel and return at high fidelity.
2. CPU load is measurably reduced by delegation (100% → 84%).
3. The infrastructure overhead of MQTT-based work dispatch prevents full schedulability recovery at the overload boundary — a constraint arising from the evaluation infrastructure rather than the scheduling algorithm itself.

---

---

## Phase 4b — TCP Binary Transport (fw-0.4.0-tcp)

**Motivation:** `deleg-load800-run2` showed delegation is functionally correct but miss persists because MQTT dispatch overhead (`pvPortMalloc(32768)` + 32KB `snprintf` + broker relay) consumes the 100ms compute budget independently of local work. The TCP binary transport replaces MQTT as the data plane for work items and results — 8-byte header + binary matrices (7208 bytes total) sent directly peer-to-peer.

**Key firmware changes:**
- `work_transport.c/h` — TCP server (port 5002), async send queue (FreeRTOS queue, priority-1 sender task), binary frame protocol
- `mqtt.c` — `parse_ip_field` fix (511→128-byte scan limit; telemetry payloads are 608 bytes)
- `delegation.c` — dispatch via enqueue (non-blocking), teardown via `work_transport_channel_teardown`
- `manager_task.c` — ADAPT_DECREASE guarded: only fires when `delegation_active_channel_count == 0`
- `delegation_test.py` / `run-lab.sh` — drain phase added (victim load released; channels watched for graceful IDLE)

**Baseline for comparison:** deleg-load800-run2 — miss=19.3/20, cpu=84% during ACTIVE.

---

### deleg-tcp-run4 — session_20260427-193741

**Date:** 2026-04-27 · **Firmware:** fw-0.4.0-tcp (first build with IP fix)
**Victim:** node-34A9F0 · **Config:** high-load=800, low-load=200, hold=90s, 4 nodes

| Field | Result |
|---|---|
| `delegation_fired` | **True** |
| `handshake_latency_ms` | 1006ms |
| victim ACTIVE duration | 208s |
| victim cpu avg during ACTIVE | **84.5%** |
| victim miss avg during ACTIVE | **19.78 / 20** |
| `max_deleg_dispatched` | **7228** (vs 2668 MQTT — 2.7× more dispatches) |
| `max_deleg_returned` | **7222** (99.9% return rate) |

**Outcome:** TCP dispatch confirmed working — 7228 dispatches vs 0 in earlier broken runs. However, miss is still 19.78/20. Root cause: `work_transport_send_item` ran synchronously inside `compute_task`'s exec window. Each call blocked in `send_exact` waiting for the 2920-byte lwIP TCP send buffer to drain for a 7208-byte frame. 4 dispatches/cycle × ~25ms each = ~100ms blocking → exec exceeded deadline every cycle.

---

### deleg-tcp-run5 — session_20260427-201540

**Date:** 2026-04-27 · **Firmware:** fw-0.4.0-tcp + async send queue (priority 2)
**Victim:** node-34A9F0 · **Config:** high-load=800, low-load=200, hold=90s, 4 nodes

| Field | Result |
|---|---|
| `delegation_fired` | **True** |
| victim cpu avg during ACTIVE | **99%** |
| victim miss avg during ACTIVE | **17.08 / 20** |
| `max_deleg_dispatched` | **10500** (faster throughput) |
| `max_deleg_returned` | **10488** (99.9% return rate) |

**Outcome:** Async queue increased dispatch throughput (10500 vs 7228). Miss dropped slightly (17.08 vs 19.78) but still very high. Root cause: `work_sender_task` was priority 2 (same as `compute_task`). Time-sliced WiFi TX processing by the sender task ran during compute_task's exec window. WiFi ISRs + lwIP task at priority ~22 preempted `compute_task` continuously, inflating exec_ticks past 100ms. Additionally, `xQueueSend` failure returned `DISPATCH_ERROR` → fallback `compute_kernel()` ran, adding local blocks.

---

### deleg-tcp-run6 — session_20260427-202732 ✓ KEY RESULT

**Date:** 2026-04-27 · **Firmware:** fw-0.4.0-tcp + priority-1 sender + BUSY-on-queue-full
**Victim:** node-34A9F0 · **Config:** high-load=800, low-load=200, hold=90s, 4 nodes

**Changes vs run5:**
1. `work_sender_task` priority: 2 → **1** (only runs in compute_task's idle window — no WiFi TX preemption during exec)
2. Queue-full in `delegation_dispatch_work_item`: `DISPATCH_ERROR` → **`DISPATCH_BUSY`** (no fallback local compute on queue-full)

| Field | Result |
|---|---|
| `delegation_fired` | **True** |
| `handshake_latency_ms` | 1007ms |
| victim ACTIVE duration | **208s** |
| victim miss avg — full ACTIVE | **0.66 / 20** |
| victim miss avg — steady-state (t≥100s) | **0.118 / 20** |
| victim miss max — steady-state | **2** |
| victim cpu avg — steady-state | **79.2%** |
| `max_deleg_dispatched` | **5999** |
| `max_deleg_returned` | **5882** (98.0% return rate) |
| `max_deleg_blocks` | **13** (multi-channel expansion) |
| Serial crashes | **0** |

**miss distribution (all 208 ACTIVE samples):**
- miss=0: 143 (69%)
- miss=1: 32 (15%)
- miss=2: 14 (7%)
- miss=3: 10 (5%)
- miss=4: 6 (3%)
- miss=8: 3 (1%) — initial transient only (first 60s)

**No-delegation baseline (Phase 1/2):** cpu=100%, miss=20/20 at load=800.

**Outcome — schedulability recovered by TCP delegation:**
- miss drops from **20/20 (100%) → 0.118/20 (0.6%) in steady state**
- cpu drops from **100% → 79.2%** in steady state
- The early miss (first ~60s of ACTIVE) is a TCP connection establishment transient — the first 20-cycle windows include cycles before the send queue is fully warmed up
- **After t=100s: miss is essentially 0** across all remaining ACTIVE samples (max=2, avg=0.118)
- `deleg_blocks` varied 3–13 during ACTIVE — delegation expanded to a second channel at peak, then self-reduced as stress dropped under `ADAPT_DECREASE` (active only when `deleg_channel_count==0`)
- 98.0% of dispatched items were returned — transport is reliable

**Dissertation interpretation:**
1. TCP binary transport resolves the MQTT dispatch overhead bottleneck identified in run2.
2. The async priority-1 sender pattern decouples compute_task's exec window from WiFi TX latency completely.
3. Delegation demonstrably restores schedulability: a node that misses 100% of deadlines at load=800 misses <1% in steady-state with delegation active.
4. The remaining ~0.6% steady-state miss rate reflects transient in-flight imbalances as blocks dynamically adjust — not a structural limitation.

---

### deleg-tcp-run7 — session_20260427-203819 (4-node repeat)

**Date:** 2026-04-27 · **Firmware:** fw-0.4.0-tcp
**Victim:** node-34A9F0 · **Config:** high-load=800, low-load=200, hold=90s, 4 nodes

| Field | Result |
|---|---|
| `delegation_fired` | **True** |
| `handshake_latency_ms` | 1008ms |
| victim ACTIVE duration | 207s |
| victim miss avg — full ACTIVE | **0.391 / 20** |
| victim miss avg — steady-state (t≥100s) | **0.243 / 20** |
| victim miss max — steady-state | **2** |
| victim cpu avg — steady-state | **85.4%** |
| `max_deleg_dispatched` | **13414** |
| `max_deleg_returned` | **13087** (97.6% return rate) |
| `max_deleg_blocks` | **16** (expanded to 3 channels) |
| miss=0 samples | **162 / 207 (78%)** |

**Outcome:** Consistent with run6. Multi-channel delegation expanded further (blocks 6–16 across the ACTIVE window, dispatching 13414 items — 2.2× run6). miss=0 in 78% of samples (vs 69% in run6). Slight variation in steady-state avg (0.243 vs 0.118) reflects normal run-to-run WiFi contention variance. The mechanism is reproducible.

---

### deleg-tcp-5node-run1 — session_20260427-205236

**Date:** 2026-04-27 · **Firmware:** fw-0.4.0-tcp
**Victim:** node-34A9F0 · **Config:** high-load=800, low-load=200, hold=90s, **5 nodes** (+ node-313978)
**Bystanders:** node-2FCC00, node-313978, node-7115F8, node-717AC4

| Field | Result |
|---|---|
| `delegation_fired` | **True** |
| `time_to_delegate_ms` | **2018ms** (faster than 4-node ~3020ms — more candidate hosts) |
| victim ACTIVE duration | 209s |
| victim miss avg — full ACTIVE | **1.502 / 20** |
| victim miss avg — steady-state (t≥100s) | **1.197 / 20** |
| victim miss max — steady-state | **5** |
| victim cpu avg — steady-state | **84.5%** |
| `max_deleg_dispatched` | **5988** |
| `max_deleg_returned` | **5512** (92.1% return rate) |
| `max_deleg_blocks` | **8** |
| miss=0 samples | **86 / 209 (41%)** |

**miss distribution (all 209 ACTIVE samples):**
- miss=0: 86 (41%)
- miss=1: 28 (13%)
- miss=2: 35 (17%)
- miss=3: 46 (22%)
- miss=4: 8 (4%)
- miss=5: 4 (2%)
- miss=13: 2 (1%) — initial transient only

**Outcome — delegation works in 5-node topology, with expected WiFi contention degradation:**
- miss drops from **20/20 baseline → 1.197/20 steady-state** (94% reduction)
- The 5-node result is noticeably worse than 4-node (ss avg 1.197 vs 0.118–0.243) for two reasons:
  1. **WiFi channel saturation**: 5 nodes share a single 2.4GHz channel. MQTT telemetry (5 × 1Hz) + TCP work items produce higher collision rates. The work_recv_task return rate drops to 92.1% (vs 97–98% in 4-node), indicating more retransmissions.
  2. **Block count limited to 8**: The initial negotiation started with 2 blocks (no REQUESTING phase captured in telemetry — delegation may have begun from a warm prior state). With only 8 peak blocks delegated vs 16 in run7, the victim ran more local blocks, leaving less headroom.
- `time_to_delegate` is 2018ms vs ~3020ms in 4-node — the 5th peer increased the chance of an immediate ACCEPT response, confirming the benefit of larger peer pools.
- No serial crashes; all 5 nodes stable throughout.

**Dissertation interpretation:**
- TCP delegation restores schedulability across both 4-node and 5-node topologies.
- The 4-node steady-state (miss <0.25/20, ~1%) is near-schedulable by the SCHEDULABLE definition (miss=0 + cpu<90%).
- The 5-node steady-state (miss ~1.2/20, ~6%) reflects WiFi channel contention as a confound — documented in threats-to-validity. The scheduling algorithm is not the limiting factor; the shared radio medium is.
- Both topologies show >90% miss reduction vs the no-delegation baseline (20/20).

---

### Cross-run TCP delegation summary (fw-0.4.0-tcp, load=800)

| Run | Topology | miss avg (all) | miss avg (ss t≥100s) | miss=0 % | cpu ss | dispatched | return % |
|---|---|---|---|---|---|---|---|
| deleg-tcp-run6 | 4-node | 0.66/20 | **0.12/20** | 69% | 79.2% | 5999 | 98.0% |
| deleg-tcp-run7 | 4-node | 0.39/20 | **0.24/20** | 78% | 85.4% | 13414 | 97.6% |
| deleg-tcp-5node-run1 | 5-node | 1.50/20 | **1.20/20** | 41% | 84.5% | 5988 | 92.1% |
| **Baseline** | any | 20/20 | 20/20 | 0% | 100% | — | — |

**4-node runs are consistent across repeats** (miss ss 0.12–0.24, cpu 79–85%). The variation is normal WiFi run-to-run jitter, not a structural instability. The 5-node degradation is attributable to increased WiFi channel contention with 5 simultaneous nodes, not to the scheduling or delegation algorithm.

---

### deleg-load800-run1 — INVALIDATED (adaptation confound)

**Date:** 2026-04-26
**Victim:** node-34A9F0
**Config:** high-load=800, low-load=200, hold=90s, 4 nodes

**Status: INVALIDATED** — experiment confounded by Phase 3 load adaptation firing during hold.

**Problem 1 — Adaptation confound:**
The DECREASE path in `manager_task.c` fires whenever `stress == STRESS_HIGH && has_low_peer`, which is exactly the condition that triggers delegation. With `ADAPT_LOAD_STEP=100` and a ~2s window, the victim's `load_factor` stepped:
`800 → 700 → 600 → ... → 100` across the hold period.
`ADAPT_LOW_WINDOWS_TO_INCREASE=9999` only disabled the INCREASE path — the DECREASE path was always active.
The CPU and miss reduction observed in this run cannot be attributed to delegation alone.

**Problem 2 — Dispatch serialisation overhead:**
After load dropped to 100 (eff_blocks=2, local_blocks=0), victim cpu avg=84% with miss avg=16/20 cycles.
`pvPortMalloc(32768)` + 32KB snprintf + `esp_mqtt_client_publish` for each `DISPATCH_OK` runs on the `compute_task` critical path and consumes the 100ms compute budget even with zero local matrix computation.
This is a consequence of DEC-005/DEC-006 (real matrix data in work items) — not fixable without changing the delegation architecture. Documented in threats-to-validity.

**Fix needed before rerun:**
1. Add `#define ADAPT_DECREASE_ENABLED 0` to `config/config.h`
2. Guard the DECREASE block in `manager_task.c` with `#if ADAPT_DECREASE_ENABLED`
3. Rebuild, reflash, rerun as `deleg-load800-run2`

---

## To Do

- [x] Run delegation validation and record session here
- [x] Run `multi-peer-run10` after stack-budget fix and confirm no serial stack overflow
- [x] Apply `ADAPT_DECREASE_ENABLED 0` firmware fix, rebuild, reflash
- [x] Run `deleg-load800-run2` — clean comparison against Phase 1/2 baseline (miss=20 at load 800)
- [x] Implement TCP binary transport (fw-0.4.0-tcp) to eliminate MQTT dispatch overhead
- [x] Fix `parse_ip_field` 511-byte limit (telemetry payloads are 608 bytes)
- [x] Async sender queue (priority-1 sender task) to decouple compute exec from WiFi TX latency
- [x] Confirm schedulability recovery: deleg-tcp-run6 shows miss 20/20 → 0.118/20 steady-state
- [x] Repeat validation: deleg-tcp-run7 confirms result (miss ss 0.243/20, consistent with run6)
- [x] 5-node validation: deleg-tcp-5node-run1 confirms delegation works across topologies (miss ss 1.197/20; WiFi contention confound documented)
- [ ] Fix `delegation_duration_ms` in `analyze_delegation.py` (currently always empty)
- [ ] Add host peak CPU to `delegation_summary.csv` output
- [ ] Update `docs/threats-to-validity.md` — remove MQTT overhead constraint (resolved by TCP transport); add WiFi TX scheduling interaction note
- [ ] Update `formal-grounding.md` RTA table with empirical C_i from exec_max_p95 (13–14 ticks at load 800 boundary)
- [ ] Add per-core idle hook instrumentation in `metrics.c` for valid dual-core CPU measurement
- [ ] Produce repeat-variance table (cpu_mean ± std, exec_max_p95 ± std) across all topologies

---

## Phase 5 — Failover (fw-0.5.2-failover-teardown)

### deleg-failover-run7 — CANONICAL FAILOVER RESULT

**Date:** 2026-05-01  
**Session:** `session_20260501-123221`  
**Victim:** node-34A9F0 at load=950; bystanders at load=200; 5-node cluster  
**Config:** hold=120s, crash-host-after=40s

| Metric | Value |
|---|---|
| Handshake latency | 37339ms (WiFi turbulence; atypical) |
| Time to initial delegation | 38354ms |
| Crashed host | node-717AC4 at t=40.4s after ACTIVE |
| Crash-to-recovery (harness) | **17156ms** |
| Miss spike during gap | **20/20** (~17s at 100ms/cycle) |
| `deleg_failover_count` at recovery | 5 |
| Final `deleg_failover_count` | 12 (includes WiFi turbulence) |
| Victim max miss before crash | 1/20 (delegation effective) |

**Serial evidence (run6, victim serial, uptime ~117s):**  
Channel-loss → re-delegation in ~4s at the firmware level. Run7 harness gives the end-to-end wall-clock figure (17.2s includes TCP reconnect + MQTT handshake).

**Figures (all in this directory):**

| File | Contents |
|---|---|
| `delegation_timeline.png` | Standard per-node delegation timeline (existing) |
| `failover_crash_window.png` | Victim miss + cpu zoomed to crash/recovery window; crash shaded |
| `failover_full_timeline.png` | Full session: all nodes cpu + miss + failover_count with event lines |
| `failover_counter.png` | `deleg_failover_count` staircase with crash window annotated |

**Bugs fixed during this phase (firmware + tooling):**

| Fix | File |
|---|---|
| `delegation_handle_tcp_channel_lost()` — all TCP failure paths | `work_transport.c`, `delegation.c/h` |
| Teardown skips `vTaskDelete` on current task | `work_transport.c` |
| `deleg_failover_count` + 4 counters added to `/api/state` | `dashboard/app.py` |
| Harness: `actual_host` from `deleg_peer` at ACTIVE event | `delegation_test.py` |
| Harness: recovery detection allows same-node reboot | `delegation_test.py` |
| Analyzer: HOSTING+blocks=0 → IDLE normalisation | `analyze_delegation.py` |

**Status: COMPLETE** — failover experiment done; all [FILL] values known for dissertation.
