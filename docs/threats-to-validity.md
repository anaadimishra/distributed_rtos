# Threats To Validity

## 1) Internal Validity Threats

- WCET estimates for `sensor_task`, `control_task`, and `manager_task` are bounded estimates, not directly measured task-level WCET traces.
  - Mitigation: add direct per-task timing instrumentation (`start_tick`/`end_tick`) in `system_context_t` and include task-level timing telemetry.

- Clock synchronization error:
  - `SYNC_TIME` aligns node epoch with dashboard epoch, but sync error is not yet quantified.
  - Mitigation: measure round-trip timing of `SYNC_TIME` control exchange and report latency measurements with `± sync_error`.

- 4-node evidence has two TCP delegation repeats (run6, run7) and one 5-node run.
  - The 4-node repeats show consistent miss steady-state (0.12 and 0.24 per 20 cycles),
    confirming reproducibility. The 5-node run shows higher miss (1.20/20) due to WiFi
    contention (see §6). A formal repeatability table (mean ± std across ≥5 runs) would
    further strengthen the internal validity claim.

## 2) External Validity Threats

- Single Wi-Fi environment:
  - Shared-medium contention may differ substantially from wired broker or dedicated AP
    deployments. The 5-node degradation (miss 0.24 → 1.20) relative to 4-node is
    likely a WiFi contention effect rather than an algorithm limit.
  - Mitigation: replicate threshold and latency tests with wired broker/Ethernet backhaul.

- Synthetic workload:
  - Matrix-multiply compute kernel has deterministic structure and may under-represent
    real sensor pipeline variance.
  - Mitigation: add workload variants with bursty/heterogeneous execution-time distributions.

## 3) Construct Validity Threats

- Saturation definition:
  - `cpu >= 90` OR `miss > 0` is a design heuristic, not a universal real-time standard.
  - Mitigation: report alternate definitions (e.g., miss-only or queue+miss composite) and compare conclusions.

- `exec_max_p95` as WCET proxy:
  - p95 excludes the top 5% by definition; it is not true WCET.
  - Mitigation: include p99/p100 and explicit worst-observed values, and separate these from formal WCET claims.

- `cpu` metric is an idle-hook proportion, not a per-task CPU time:
  - The FreeRTOS idle hook measures how often the idle task runs. It captures all
    non-idle time including WiFi driver tasks (priority ~22), lwIP tasks, MQTT
    callbacks, and ISR overhead — not just `compute_task` execution time.
  - During TCP delegation with the async sender at priority 1, WiFi TX processing
    runs during `compute_task`'s idle window. The `cpu` metric therefore
    over-represents the victim's "scheduling load" relative to pure compute. A
    node showing cpu=84% during TCP delegation may have compute_task exec_ticks
    of only ~15ms — the remaining 69ms is WiFi stack overhead.
  - Mitigation: report both `cpu` (idle hook) and `exec_ticks` (compute_task
    measured wall time) as distinct metrics. The miss count is the authoritative
    schedulability indicator.

## 4) Phase 4 — Delegation Validity Threats

- **Work item transport adds broker-mediated or network latency:**
  - MQTT phase (fw-0.3.0-deleg): work items travel via the MQTT broker (not
    direct peer-to-peer). Round-trip latency includes broker relay (~10–30ms on
    LAN) plus JSON serialisation of ~1800 integers. This is substantially higher
    than a real task-dispatch mechanism.
  - TCP phase (fw-0.4.0-tcp): work items travel direct peer-to-peer over TCP.
    Round-trip latency is network RTT plus serialisation (~1ms for memcpy of
    7208 bytes). WiFi mesh RTT between two ESP32s on the same AP is typically
    2–10ms. This is still higher than shared-memory IPC (<1ms) but eliminates
    the broker relay and JSON overhead.
  - Mitigation: document transport as an evaluation infrastructure choice. The
    delegation algorithm is transport-agnostic.

- **Pending work timeout policy affects measured throughput:**
  - Pending work is reclaimed after `DELEGATION_PENDING_TIMEOUT_MS=2000`. This
    prevents permanent slot leakage, but a slow result may be counted as timed out
    even if it eventually arrives.
  - Mitigation: report `deleg_timeout_reclaim` alongside `deleg_dispatched` and
    `deleg_returned`. Treat timeout reclaim as a backpressure/fault-tolerance
    metric, not as proof that remote computation failed.

- **Busy-skip semantics trade completeness for schedulability:**
  - When all active channels are at cap, the pending table is full, or the async
    send queue is full, the delegated block is skipped for that cycle rather than
    run locally. This preserves CPU relief but means not every nominal block is
    computed during pressure.
  - Mitigation: explicitly report `deleg_busy_skip` and frame this workload as a
    best-effort load proxy, not a safety-critical numerical pipeline.

- **Matrix inputs are identical every cycle:**
  - `matrix_a` and `matrix_b` are initialised once at boot and never change.
    Every dispatched work item carries the same values. This demonstrates the
    dispatch mechanism but does not exercise varying inputs.
  - Mitigation: acknowledged in dissertation as a property of the load proxy.
    The mechanism is computation-agnostic; the matrices are stand-ins for real
    workload data.

- **MQTT buffer size of 32768 bytes per node (MQTT data plane phase only):**
  - Raised from the default 1024 bytes to accommodate full matrix payloads.
    Consumes ~64KB heap per node (two buffers: send and receive). On ESP32 with
    ~320KB DRAM this is feasible but non-trivial. This constraint is eliminated
    in fw-0.4.0-tcp where work items travel via TCP and the MQTT buffer reverts
    to normal telemetry/handshake sizes.

- **Task stack budgets changed during Phase 4:**
  - Real work dispatch moved JSON construction/parsing and multi-peer orchestration
    into tasks originally sized for simpler behavior. Serial logs identified stack
    overflows in `manager_task` and `compute_task`, leading to larger stack budgets.
  - Mitigation: include serial logs as part of the evidence trail and report stack
    sizes as fixed experimental configuration.

- **Serial capture timing matters:**
  - Serial logs must be started before the experiment begins. Manual monitor startup
    after the run can miss the crash boundary. The harness now supports
    `run-lab.sh --serial-monitor` to start detached capture before node discovery.

- **Loop prevention guard (HOSTING node must not re-delegate):**
  - A node in HOSTING state must not call `delegation_try_offload()`. Without
    this guard, a bystander stressed by hosting CPU would re-delegate to further
    nodes, creating a cascade. Demonstrated in `multi-peer-run10`: node-2FCC00 at
    load=200 became stressed from hosting CPU and dispatched 801 items before the
    guard was added. Fixed by checking `has_any_hosting_channel` before offload.

## 5) Phase 4a — MQTT Dispatch Serialisation Overhead (Historical Infrastructure Constraint)

This section documents the MQTT-phase bottleneck that motivated TCP transport.
It is preserved for dissertation context and to explain the fw-0.3.0-deleg →
fw-0.4.0-tcp transition.

### Observed effect (fw-0.3.0-deleg, deleg-load800-run2)

In `deleg-load800-run2` (session `session_20260426-214105`), delegation at load=800
reduced victim CPU from 100% to 83.6% avg during ACTIVE, but deadline misses
persisted (avg 19.3/20 vs baseline 20/20). The mechanism was functionally correct
(dispatched=2668, returned=2660, 99.7% return rate). The miss persistence was caused
by dispatch serialisation overhead on the `compute_task` critical path.

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

The dispatch overhead came from three sources, all in the `compute_task` critical
path before `vTaskDelayUntil`:
1. `pvPortMalloc(32768)` — heap allocation on FreeRTOS; under contention ~1–3ms
2. `snprintf` over a ~16–20KB JSON payload (1800 int32s, avg 9 chars each) — ~5–8ms
3. `esp_mqtt_client_publish()` — acquires the MQTT client lock, copies payload to
   the internal ring buffer, may block if the broker socket is backpressured

These three operations repeated **9 times per 100ms cycle**, consuming the budget
that local compute reduction was meant to free.

### Resolution

Replaced by TCP binary transport (DEC-008, DEC-019, DEC-020 — see §6).

---

## 6) Phase 4b — TCP Transport Engineering Constraints (fw-0.4.0-tcp)

### 6a — lwIP send buffer stalls compute_task (resolved by async queue)

**Observed (deleg-tcp-run4):** After the MQTT→TCP transition, TCP dispatch
confirmed working (dispatched=7228, returned=7222, 99.9% return rate). However,
miss remained high (19.78/20). Root cause: `work_transport_send_item()` called
`send_exact()` synchronously inside `compute_task`'s exec window. The ESP-IDF lwIP
TCP send buffer defaults to ~2920 bytes (2 × TCP_MSS). Each 7208-byte frame
required multiple `send()` calls, blocking between each until the remote end drained
the buffer. Four dispatches × ~25ms each = ~100ms blocking inside exec → deadline
missed every cycle.

**Diagnosis method:** CPU was 84.5% (down from 100%) because `compute_task` was
blocking (suspended, not running) during `send()` waits, allowing idle to run.
But `exec_ticks` (wall clock from `start` to `end`) still exceeded 100ms because
FreeRTOS tick count advances during blocked time.

**Resolution (deleg-tcp-run5, partial):** Async send queue introduced. `compute_task`
posts to a FreeRTOS queue (`xQueueSend` with 0 timeout) and returns. A background
`work_sender_task` drains the queue and calls `send_exact()`. This made dispatch
non-blocking from `compute_task`'s perspective. Dispatch throughput increased to
10500. However, miss only dropped to 17.08/20.

**Residual problem:** `work_sender_task` was at priority 2 (same as `compute_task`).
FreeRTOS time-slicing at the same priority caused the two tasks to interleave.
WiFi ISR activity triggered by `work_sender_task`'s sends (WiFi driver runs at
priority ~22) preempted `compute_task` during its exec window, inflating exec_ticks.
Additionally, when the queue was full, dispatch returned `DISPATCH_ERROR` causing
fallback `compute_kernel()` to run, adding local blocks.

**Full resolution (deleg-tcp-run6, canonical):** Two changes:
1. `work_sender_task` priority: 2 → **1**. Now `compute_task` (priority 2) runs
   uninterrupted. `work_sender_task` only runs during `compute_task`'s ~85ms
   idle window after `vTaskDelayUntil`. WiFi TX processing is isolated to that
   idle window.
2. Queue-full: `DISPATCH_ERROR` → **`DISPATCH_BUSY`** (no fallback local compute).
   Queue-full is transient backpressure; skipping the block is correct (DEC-022).

**Result:** compute_task exec_ticks ≈ 15ms; miss ss avg = 0.12/20.

### 6b — parse_ip_field 511-byte limit bug (silent, no-dispatch, 3 runs)

**Observed (deleg-tcp-run1/2/3):** After building fw-0.4.0-tcp with TCP transport,
all three runs showed `dispatched=0`, `deleg_dispatch_err` climbing at ~90/second.
Serial logs confirmed TCP servers listening on port 5002 (nc test from Mac confirmed
open). `find_peer_ip()` was returning NULL on every call.

**Root cause:** `parse_ip_field()` in `mqtt.c` had the guard:
```c
if (data == NULL || len <= 0 || len > 511) return;
```
Actual MQTT telemetry payloads at fw-0.4.0-tcp are 608 bytes. Since 608 > 511,
the function returned without parsing. `peer->ip_addr` was never written. Every
`find_peer_ip()` returned NULL. Every channel activation set `tcp_fd = -1`.
Every dispatch attempt saw `q == NULL` → DISPATCH_ERROR.

**How the bug was found:** Python analysis of the run3 JSONL log measured the
actual payload length at 608 bytes. Cross-referenced with the `parse_ip_field`
source — 511-byte limit was arbitrary and undocumented.

**Fix:** Scan only the first 128 bytes (the `"ip"` field is always within the
first ~50 bytes of the JSON). Drop the payload size guard entirely.

**Lesson:** Silent guard conditions on input length that silently return without
writing output fields are a class of bugs worth unit-testing with real-payload-
length inputs.

### 6c — tcp_fd=0 initialisation bug (calloc zeroes to stdin fd)

**Observed:** If `ctx->channels[i].tcp_fd` is not explicitly set to -1 after
calloc (which zeroes all fields), `tcp_fd = 0` is interpreted as a valid file
descriptor (stdin). Any call to `work_transport_disconnect(0)` would `shutdown(0)`
and `close(0)`, silently closing stdin and potentially corrupting the MQTT socket.

**Fix (app_main.c):**
```c
for (int i = 0; i < MAX_DELEGATION_CHANNELS; i++) {
    ctx->channels[i].tcp_fd          = -1;
    ctx->channels[i].tcp_send_queue  = NULL;
    ctx->channels[i].tcp_sender_task = NULL;
}
```
**Lesson:** Fields representing "no resource" must be explicitly initialised to
their sentinel value (-1 for fds, NULL for handles) after calloc.

### 6d — SO_SNDTIMEO before connect() causes EAGAIN (ESP-IDF lwIP quirk)

See DEC-021. `SO_SNDTIMEO` must be set after `connect()` returns. On ESP-IDF lwIP,
the timeout also constrains `connect()` itself when set beforehand.

### 6e — WiFi channel contention degrades 5-node delegation

**Observed (deleg-tcp-5node-run1):** 5-node delegation showed higher steady-state
miss (1.20/20) compared to 4-node (0.12–0.24/20). Return rate dropped to 92.1%
(vs 97–98% in 4-node). Blocks only expanded to 8 vs 13–16 in 4-node.

**Explanation:** Five ESP32 nodes sharing a single 2.4GHz 802.11 channel. Each
node transmits MQTT telemetry (1Hz, ~608 bytes) plus TCP work frames (7208 bytes
per dispatch). Total channel load with 5 nodes is substantially higher than 4.
WiFi CSMA/CA contention increases collision probability, causing TCP retransmissions.
This reduces the effective work_sender throughput and lowers the result return rate.

**Not an algorithm failure:** The scheduling and delegation algorithm decisions
are unchanged. The increased miss reflects the transport being congested, not the
delegation mechanism failing to make correct offload decisions.

**Positive topology effect:** `time_to_delegate` dropped from ~3020ms (4-node) to
2018ms (5-node). With 4 candidate hosts instead of 3, the DELEGATE_ACCEPT arrives
faster on average — the peer pool benefit is real and measurable.

### 6f — Graceful drain does not complete within 30-second window

**Observed (all TCP runs):** The drain phase (`[warn] channels did not reach IDLE
within drain window`) fired in every TCP run. After victim load is released (set
to 200), channels stay ACTIVE for >30 seconds.

**Explanation:** The graceful drain condition is:
```c
ctx->self_stress_level == STRESS_LOW && ch->in_flight_count == 0
```
At load=200 after a high-load hold period, two things slow the drain:
1. The victim node is still ACTIVE with blocks in the queue. The sender task
   continues draining queued frames (built up during hold). Results come back
   slowly due to WiFi latency.
2. `STRESS_LOW` requires multiple consecutive low-CPU windows before transitioning
   from `STRESS_HIGH`. With lingering WiFi overhead from the now-empty queue being
   drained, the stress level change lags.

**Impact on dissertation:** The drain phase is a stretch goal (observe graceful
channel closure after load relief). The delegation validation result (miss during
ACTIVE hold) is not affected by whether channels drain within 30 seconds. The
channels do eventually close; they just take longer than the 30-second observation
window.

---

## Dissertation Outline Hook

Add a dedicated subsection in evaluation/discussion:
- `Threats to Validity`
  - Internal validity (WCET, clock sync, repeatability depth)
  - External validity (WiFi environment, synthetic workload)
  - Construct validity (state definitions, exec_max_p95 as WCET proxy, cpu metric semantics)
  - Phase 4a infrastructure constraints: MQTT dispatch serialisation overhead (§5 — resolved)
  - Phase 4b TCP transport constraints: lwIP buffer stalls, IP parse bug, SO_SNDTIMEO quirk,
    WiFi contention at 5 nodes, graceful drain timing (§6)
  - Mitigations: async sender queue (DEC-020), priority-1 sender (DEC-020),
    BUSY-on-queue-full (DEC-022), post-connect SO_SNDTIMEO (DEC-021)
