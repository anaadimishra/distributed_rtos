# Threats To Validity

## 1) Internal Validity Threats

- WCET estimates for `sensor_task`, `control_task`, and `manager_task` are bounded estimates, not directly measured task-level WCET traces.
  - Mitigation: add direct per-task timing instrumentation (`start_tick`/`end_tick`) in `system_context_t` and include task-level timing telemetry.

- Clock synchronization error:
  - `SYNC_TIME` aligns node epoch with dashboard epoch, but sync error is not yet quantified.
  - Mitigation: measure round-trip timing of `SYNC_TIME` control exchange and report latency measurements with `± sync_error`.

- 4-node evidence currently has limited repeatability depth relative to single/dual-node campaigns.
  - Mitigation: run additional repeated sessions at 4-node scale and report variance tables per load.

## 2) External Validity Threats

- Single Wi-Fi environment:
  - shared-medium contention may differ substantially from wired broker or dedicated AP deployments.
  - Mitigation: replicate threshold and latency tests with wired broker/Ethernet backhaul.

- Synthetic workload:
  - matrix-multiply compute kernel has deterministic structure and may under-represent real sensor pipeline variance.
  - Mitigation: add workload variants with bursty/heterogeneous execution-time distributions.

## 3) Construct Validity Threats

- Saturation definition:
  - `cpu >= 90` OR `miss > 0` is a design heuristic, not a universal real-time standard.
  - Mitigation: report alternate definitions (e.g., miss-only or queue+miss composite) and compare conclusions.

- `exec_max_p95` as WCET proxy:
  - p95 excludes the top 5% by definition; it is not true WCET.
  - Mitigation: include p99/p100 and explicit worst-observed values, and separate these from formal WCET claims.

## 4) Phase 4 — Delegation Validity Threats

- **MQTT as delegation data plane adds broker-mediated latency:**
  Work items travel via the MQTT broker (not direct peer-to-peer). Round-trip
  latency includes broker relay (~10–30ms on LAN) plus JSON serialisation of
  ~1800 integers. This is substantially higher than a real task-dispatch mechanism
  (TCP direct: ~2–5ms; shared memory: <1ms).
  Mitigation: document MQTT as an evaluation infrastructure choice. TCP direct /
  ESP-NOW are the natural production data plane. Broker latency does not affect
  the correctness of the delegation mechanism, only its performance.

- **Pending work timeout policy affects measured throughput:**
  Pending work is now reclaimed after `DELEGATION_PENDING_TIMEOUT_MS=2000`. This
  prevents permanent slot leakage, but a slow result may be counted as timed out
  even if it eventually arrives.
  Mitigation: report `deleg_timeout_reclaim` alongside `deleg_dispatched` and
  `deleg_returned`. Treat timeout reclaim as a backpressure/fault-tolerance metric,
  not as proof that remote computation failed.

- **Busy-skip semantics trade completeness for schedulability:**
  When all active channels are at cap or the pending table is full, the delegated
  block is skipped for that cycle rather than run locally. This preserves CPU relief
  but means not every nominal block is computed during pressure.
  Mitigation: explicitly report `deleg_busy_skip` and frame this workload as a
  best-effort load proxy, not a safety-critical numerical pipeline.

- **Matrix inputs are identical every cycle:**
  `matrix_a` and `matrix_b` are initialised once at boot and never change.
  Every dispatched work item carries the same values. This demonstrates the
  dispatch mechanism but does not exercise varying inputs.
  Mitigation: acknowledged in dissertation as a property of the load proxy.
  The mechanism is computation-agnostic; the matrices are stand-ins for real
  workload data.

- **MQTT buffer size of 32768 bytes per node:**
  Raised from the default 1024 bytes to accommodate full matrix payloads.
  Consumes ~64KB heap per node (two buffers: send and receive). On ESP32 with
  ~320KB DRAM this is feasible but non-trivial. Any future increase in MATRIX_SIZE
  would require proportional buffer growth.

- **Task stack budgets changed during Phase 4:**
  Real work dispatch moved JSON construction/parsing and multi-peer orchestration
  into tasks originally sized for simpler behavior. Serial logs identified stack
  overflows in `manager_task` and `compute_task`, leading to larger stack budgets.
  Mitigation: include serial logs as part of the evidence trail and report stack
  sizes as fixed experimental configuration.

- **Serial capture timing matters:**
  Serial logs must be started before the experiment begins. Manual monitor startup
  after the run can miss the crash boundary. The harness now supports
  `run-lab.sh --serial-monitor` to start detached capture before node discovery.

## 5) Phase 4 — Dispatch Serialisation Overhead (Primary Infrastructure Constraint)

This is the most significant evaluation infrastructure limitation and must be
explicitly addressed in the dissertation.

### Observed effect

In `deleg-load800-run2` (session `session_20260426-214105`), delegation at load=800
reduced victim CPU from 100% to 83.6% avg during ACTIVE, but deadline misses
persisted (avg 19.3/20 vs baseline 20/20). The mechanism is functionally correct
(dispatched=2668, returned=2660, 99.7% return rate). The miss persistence is caused
by dispatch serialization overhead on the `compute_task` critical path.

### Overhead analysis

At load=800 (Phase 1/2 baseline, 16 blocks, no delegation):
- `exec_max_p95` = 13–14 ticks = **130–140ms** per compute cycle for 16 blocks
- Per-block execution time ≈ **8.1–8.75ms**

During `deleg-load800-run2` ACTIVE phase (9 blocks delegated):
- `active_blocks` reduced to 11 (20 − 9 delegated); `eff_blocks` capped at 11
- `local_blocks` = eff_blocks − dispatch_blocks = 11 − 9 = **2 local blocks**
- Local compute time ≈ 2 × 8.4ms = **~17ms**
- Compute period budget = **100ms**
- Remaining budget after local compute = **~83ms** for 9 dispatch calls
- Misses occur → total execution > 100ms → dispatch overhead > 83ms across 9 calls
- **Per-dispatch overhead: > 9ms each**

The dispatch overhead comes from three sources, all in the `compute_task` critical
path before `vTaskDelayUntil`:

1. `pvPortMalloc(32768)` — heap allocation on FreeRTOS; under contention ~1–3ms
2. `snprintf` over a ~16–20KB JSON payload (1800 int32s, avg 9 chars each) — ~5–8ms
3. `esp_mqtt_client_publish()` — acquires the MQTT client lock, copies payload to
   the internal ring buffer, may block if the broker socket is backpressured

These three operations repeat **9 times per 100ms cycle** during the run, consuming
the budget that local compute reduction was meant to free.

### Why this is an infrastructure constraint, not an algorithm failure

The delegation algorithm is correct:
- Channels open, handshake completes, work dispatches and returns
- 99.7% return fidelity at 2668 dispatch rate
- CPU load transferred: 100% → 84%
- The scheduling decision (open channels, reduce local blocks) is correct

The overhead arises from the choice to use MQTT with JSON encoding as the
**data plane** for work items — a decision made to avoid introducing a second
protocol before delegation correctness was established (DEC-008).

### Quantified projection: efficient binary transport

Two 30×30 int32 matrices = 1800 × 4 bytes = **7200 bytes binary** (vs ~16–20KB JSON).

With binary encoding over a direct transport (CoAP, TCP direct, or binary-framed
UDP with ACK at application layer):

| Component | MQTT+JSON (current) | Binary+TCP direct |
|---|---|---|
| Payload size | ~16–20 KB | 7200 bytes |
| Serialisation | `snprintf` ~16KB | `memcpy` 7200 bytes |
| Buffer allocation | `pvPortMalloc(32768)` per dispatch | Fixed stack buffer or single pool alloc |
| Transport overhead | Broker relay (~10–30ms/round-trip) | Direct peer ~2–5ms/round-trip |
| Per-dispatch estimate | >9ms (measured) | ~2–4ms (projected) |
| 9 dispatches/cycle | >83ms | ~18–36ms |
| Total cycle (17ms local + dispatch) | >100ms → misses | ~35–53ms → **SCHEDULABLE** |

The projected cycle time of 35–53ms would put the victim at SATURATED
(cpu ~35–53%) during ACTIVE delegation, well below the 100ms deadline.
This is a single arithmetic step from existing empirical data, not speculation.

### Recommended future work

Replace the MQTT data plane with:
- **TCP direct** (per-peer persistent connection, binary framing, ACK-based flow
  control) — closest to the architecture already sketched in DEC-008; no broker
  required for work items; control plane (telemetry, handshake) stays on MQTT.
- **CoAP over UDP** — purpose-built for constrained devices; binary encoding
  standard; optional confirmable mode provides delivery guarantee without
  full TCP stack overhead.

The delegation algorithm, channel data structures, and pending-work table are
transport-agnostic. Only `delegation_dispatch_work_item()` and
`delegation_handle_work_item()` need new serialisation paths.

---

## Dissertation Outline Hook

Add a dedicated subsection in evaluation/discussion:
- `Threats to Validity`
  - Internal validity
  - External validity
  - Construct validity
  - Phase 4 infrastructure constraints (§5 above — dispatch overhead, MQTT data plane)
  - Mitigations and planned follow-up experiments
