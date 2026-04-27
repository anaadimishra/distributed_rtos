# Architecture & Design Decisions

Running log of non-obvious decisions made during development. Each entry records
what was decided, what was rejected, and why.

---

## DEC-001 — Rate Monotonic priority assignment with manager elevation
**Date:** Day 01–02
**Decision:** Assign task priorities following Rate Monotonic (shorter period =
higher priority): sensor (20ms, pri 5) > control (50ms, pri 4) > compute (100ms,
pri 2). Manager task (1000ms) is deliberately elevated to pri 3, above compute.
**Reason:** Manager publishes telemetry. Under compute saturation, if manager ran
at RM-correct priority (below compute) it would be starved and telemetry would
stop. Observability is a hard requirement for the dissertation evaluation pipeline.
**Trade-off:** Violates strict RM. Documented as a deliberate observability design
choice in telemetry comments.

---

## DEC-002 — Single-core (unicore) mode
**Date:** Day 05–06
**Decision:** Lock firmware to `CONFIG_FREERTOS_UNICORE=y`.
**Reason:** Dual-core mode introduced CPU measurement volatility — idle task split
across two cores made `cpu_usage` non-deterministic. Unicore gives reproducible
scheduling behaviour for dissertation evaluation.
**Trade-off:** Halves available compute. Acceptable because the experiment is about
scheduling policy, not raw throughput.

---

## DEC-003 — Fixed-size peer table, no dynamic allocation in adaptation logic
**Date:** Day 07
**Decision:** Peer table is a static array of `MAX_PEERS=8` entries in
`system_context_t`. No `malloc`, no linked lists. Stale entries evicted by age
in manager task context.
**Reason:** FreeRTOS tasks have small stacks (2–3 KB). Dynamic allocation in
interrupt/callback context risks fragmentation and hard faults. Fixed bounds are
also easier to reason about for dissertation correctness arguments.

---

## DEC-004 — Delegation signalling only (REQUESTING/ACTIVE/HOSTING) vs true I/O exchange
**Date:** Day 07–08
**Decision:** Initial implementation negotiated `active_blocks` counts between
nodes (one reduces, other increases). This was identified as insufficient —
no data moves between nodes.
**Rejected approach:** "Signalling-only delegation" — coordination of local
compute parameters without actual work dispatch.
**Adopted approach:** Full work-item dispatch. Delegating node sends both input
matrices over the data plane; host executes and returns full result matrix. Both
directions carry real data.
**Reason:** The dissertation argument is about actual task delegation, not
admission control. A reader (or examiner) asking "what data moved between nodes?"
must have a concrete answer.

---

## DEC-005 — Seed-based matrix generation rejected
**Date:** Day 08
**Decision:** Rejected the approach of sending a seed integer instead of full
matrix data. Under a seed approach the peer regenerates the same matrices locally
— no real inputs travel.
**Reason:** Circular. The claim "we delegated work with inputs" requires inputs
to actually travel. The matrix is a load proxy; the mechanism must generalise to
real workloads where inputs are not regeneratable from a seed.

---

## DEC-006 — MATRIX_SIZE=30 preserved; MQTT buffer raised instead
**Date:** Day 08
**Decision:** Full 30×30 matrices transmitted in work_item (≈16–20 KB JSON for
MQTT phase, 7208 bytes binary for TCP phase). MQTT client buffer raised to 32768
bytes via `config.buffer_size = 32768` for the MQTT data plane phase.
**Rejected:** Reducing MATRIX_SIZE (e.g. to 8) to fit default 1024-byte buffer.
**Reason:** All Phase 1/2 load characterisation data (SAT=700, OVL=800 thresholds,
exec timing, deadline miss profiles) was collected at MATRIX_SIZE=30. Changing it
would make the delegation phase data incomparable to the baseline.
**Phase 4b note:** The TCP binary transport (fw-0.4.0-tcp) transmits the same
7200-byte payload as a packed binary frame — no JSON, no MQTT overhead — which
is why per-dispatch cost drops from >9ms to ~1ms.

---

## DEC-007 — Host does not inflate active_blocks on delegation accept
**Date:** Day 08
**Decision:** `delegation_handle_request()` no longer increases `ctx->active_blocks`
when accepting a delegation request.
**Reason:** With true work-item dispatch, the host's extra compute work arrives via
the work transport and executes in the host task. Inflating `active_blocks` would
cause the local compute loop to also run extra blocks — double-counting the
delegated work. The host's load comes from executing received work items (in
`work_hosting_task` for TCP), not from its own scheduling loop.

---

## DEC-008 — Transport protocol evolution: MQTT data plane → TCP binary
**Date:** Day 08 (initial), updated 2026-04-27 (implementation complete)

### Phase 4a decision (fw-0.3.0-deleg): MQTT for data plane
**Decision (original):** Use MQTT for work_item and work_result dispatch (data plane)
as well as for telemetry and delegation handshake (control plane).
**Alternatives evaluated at the time:**
- **ESP-NOW:** ~1ms latency, no broker, peer-to-peer at WiFi MAC. Rejected —
  250-byte payload limit cannot carry full 30×30 matrices.
- **UDP:** Low latency, binary encoding, no broker. Rejected — no delivery
  guarantee. Silent packet loss leaves `pending_work` slots in_flight indefinitely.
  Would require application-layer retransmission (reimplementing TCP).
- **TCP direct:** Reliable, ordered, ACK built in. Binary encoding removes JSON
  overhead. Persistent per-peer connections map cleanly to the peer table.
  Valid architecture but adds socket lifecycle, peer IP resolution, connection
  management before any delegation evidence exists.
- **Dual-protocol (MQTT control + TCP data):** Architecturally clean, mirrors
  production distributed systems (e.g. Kubernetes: etcd for control, direct pod
  networking for data). Documented as the natural production evolution.
**Reason for MQTT first:** Delegation validation evidence was not yet captured.
Introducing a second protocol before the first successful run risked new failure
modes with no baseline. MQTT provided full observability at no extra cost.

### Phase 4b decision (fw-0.4.0-tcp): TCP binary transport implemented
**Empirical motivation:** `deleg-load800-run2` confirmed the MQTT data plane causes
>9ms per dispatch (pvPortMalloc(32768) + snprintf on ~16KB + MQTT publish), which
consumed the full 100ms budget for 9 dispatches per cycle. Miss persisted at 19.3/20
despite correct CPU reduction (100% → 84%). This matched the projected overhead
documented in `docs/threats-to-validity.md §5` exactly.

**Decision (implemented):** Replace work_item and work_result MQTT topics with a
direct TCP binary protocol. MQTT retained as the control plane (telemetry,
DELEGATE_REQUEST, DELEGATE_REPLY).

**Implementation:**
- New file: `network/work_transport.c` — TCP server (port 5002), binary frame protocol
- Frame: 8-byte packed header (`magic=0xDA7A`, `type`, `block_id`, `cycle_id`) +
  7200-byte binary payload = 7208 bytes total (vs ~16–20KB JSON)
- Per-peer persistent TCP socket opened on DELEGATE_ACCEPT, closed on channel reset
- Peer IP propagated via `"ip"` field in MQTT telemetry → `peer->ip_addr[16]`

**Result:** `deleg-tcp-run6` (canonical result): miss drops from 20/20 → 0.12/20
steady-state; CPU 100% → 79%. The algorithm is the same; only the transport changed.

---

## DEC-009 — ADAPT_LOW_WINDOWS_TO_INCREASE = 9999 during delegation validation
**Date:** Day 07
**Decision:** Disable the auto-scale-up policy during delegation validation runs.
**Reason:** With `ADAPT_LOW_WINDOWS_TO_INCREASE = 3`, bystander nodes were
climbing their own load every ~6 seconds during the warmup phase, corrupting
the controlled asymmetry needed for delegation testing. Setting to 9999
effectively disables the increase path while preserving the decrease path
(stress-triggered load reduction still fires).
**Note:** This remains in effect in fw-0.4.0-tcp. It isolates the delegation
mechanism from load adaptation confounds.

---

## DEC-010 — Round-robin peer selection for work-item dispatch
**Date:** Day 08
**Decision:** Static `rr_index` cycles across eligible channels on each
`delegation_dispatch_work_item()` call.
**Reason:** Even distribution across available channels without a separate scheduler
or priority queue. Simple, deterministic, O(n) in channel count. Sufficient for
a 2–5 node cluster.

---

## DEC-011 — Victim reduces local blocks when DELEGATION_ACTIVE
**Date:** Day 09
**Decision:** When delegation channels are ACTIVE, `compute_task` runs
`blocks - delegation_blocks` locally and dispatches `delegation_blocks` to the
host. Previously it ran all blocks locally AND dispatched on top — delegation had
zero effect on the victim's CPU.
**Reason:** The dissertation argument is that delegation provides load relief.
Without this fix, the victim stays at 100% CPU and OVERLOADED throughout delegation
— the mechanism is demonstrated but the benefit is not. With this fix, the victim's
local compute drops by the delegated fraction, producing an observable state
transition (OVERLOADED → SATURATED or SCHEDULABLE) that is the core evaluation claim.

---

## DEC-012 — Multi-peer delegation uses fixed channel slots
**Date:** Day 09
**Decision:** Replace the single delegation peer state with
`channels[MAX_DELEGATION_CHANNELS]`, where each channel independently tracks
`IDLE`, `REQUESTING`, `ACTIVE`, or `HOSTING`.
**Reason:** One-to-one delegation was insufficient for a four-node validation. A
stressed victim should be able to request help from every reachable low-stress
peer while keeping the implementation deterministic and bounded.
**Trade-off:** The fixed channel array is less flexible than a dynamic peer list,
but it avoids heap allocation and gives a clear upper bound for dissertation
analysis.

---

## DEC-013 — Busy dispatch is skipped, not run locally
**Date:** Day 09
**Decision:** `delegation_dispatch_work_item()` returns `DISPATCH_OK`,
`DISPATCH_BUSY`, or `DISPATCH_ERROR`. `DISPATCH_BUSY` increments telemetry and
skips the delegated block for that compute cycle. Only `DISPATCH_ERROR` uses a
local fallback path.
**Reason:** Treating a full pending table as "run locally" recreated the original
overload on the victim. Busy-skip makes backpressure visible and preserves the
load-shedding semantics of delegation.
**Evidence:** In `multi-peer-run9`, `deleg_busy_skip` reached 2901 while
`deleg_dispatch_err` remained 0. The victim's in-flight total stayed bounded.
**Extended in Phase 4b (DEC-022):** Queue-full on the async TCP send queue is also
treated as DISPATCH_BUSY (not DISPATCH_ERROR) — see DEC-022.

---

## DEC-014 — Per-channel in-flight cap and pending timeout reclaim
**Date:** Day 09
**Decision:** Cap in-flight work per active channel at
`DELEGATION_MAX_INFLIGHT_PER_CHANNEL=4` and reclaim pending slots older than
`DELEGATION_PENDING_TIMEOUT_MS=2000`.
**Reason:** Work results can be delayed or lost. Without timeout reclaim, fixed
pending slots can leak permanently. A cap of 4 prevents one peer from monopolising
the pending table and keeps `MAX_PENDING_WORK=20` usable across multiple peers.
**Evidence:** In `multi-peer-run9`, victim `deleg_inflight_total` maxed at 12,
matching `3 active channels * 4`, and `deleg_timeout_reclaim` reached 58.

---

## DEC-015 — Serial capture is part of the evaluation harness
**Date:** Day 09
**Decision:** Add `run-lab.sh --serial-monitor`, backed by detached `screen`
sessions, to capture one serial log per attached ESP32 during experiments.
**Reason:** Dashboard telemetry can show that a node rebooted via `boot_id`
changes, but it cannot identify stack overflow, watchdog reset, panic class, or
backtrace. Serial logs turned ambiguous failures into concrete fixes.
**Evidence:** `multi-peer-run8` identified `manager_task` stack overflow.
`multi-peer-run9` identified `compute_task` stack overflow.
**Further evidence (fw-0.4.0-tcp):** During TCP debugging (deleg-tcp-run1/2/3),
serial logs on all 4 nodes confirmed TCP servers were listening (port 5002 open,
nc test passing) before the IP parse bug was identified as the root cause of
zero dispatches. Serial logs also confirmed firmware flashed correctly by showing
`fw-0.4.0-tcp` version string in boot output.

---

## DEC-016 — Stack budgets updated after delegation became real I/O
**Date:** Day 09
**Decision:** Increase stack budgets for tasks that now carry delegation work:
`MANAGER_TASK_STACK_SIZE=6144` and `COMPUTE_TASK_STACK_SIZE=8192`.
**Reason:** Full matrix dispatch, telemetry formatting, multi-peer request
generation, and work-item parsing increased stack pressure beyond the original
pre-delegation task budgets.
**Trade-off:** Higher static RAM use. Accepted because the alternative is
non-deterministic resets during the evaluation workload.
**Phase 4b TCP additions:** `work_hosting_task` (WORK_HOSTING_TASK_STACK=8192)
allocates 3 × 3600-byte heap buffers; `work_recv_task` (WORK_RECV_TASK_STACK=4096)
allocates one 3600-byte result buffer; `work_sender_task` (WORK_SENDER_TASK_STACK=3072)
drains the async send queue. All three are ephemeral (created on ACCEPT, deleted
on channel reset).

---

## DEC-017 — ADAPT_DECREASE guarded by delegation state (Phase 4 TCP run)
**Date:** 2026-04-26 (original), updated 2026-04-27 (re-enabled with guard)
**Decision:** The Phase 3 DECREASE path (`stress == STRESS_HIGH && has_low_peer →
load_factor -= ADAPT_LOAD_STEP`) is re-enabled (`ADAPT_DECREASE_ENABLED=1`) but
gated: fires only when `delegation_active_channel_count(ctx) == 0`.

**History:**
- `deleg-load800-run1`: DECREASE fired alongside delegation, stepping load
  800 → 700 → ... → 100 every ~2s. CPU and miss reduction were confounded.
  INVALIDATED.
- Fix: `ADAPT_DECREASE_ENABLED=0` for isolation — run2 confirmed delegation
  mechanism correct (dispatched=2668, returned=2660) but miss persisted due to
  MQTT dispatch overhead.
- Re-enabled with delegation guard in fw-0.4.0-tcp: delegation is the primary
  relief mechanism when channels are ACTIVE; DECREASE is the fallback if
  delegation is not engaged. Correct priority ordering for realistic operation.

**Trade-off:** During ACTIVE delegation the load stays high (by design — delegation
carries the relief). Once delegation goes IDLE, DECREASE can fire normally.

---

## DEC-018 — MQTT data plane confirmed insufficient; TCP implemented (Phase 4b)
**Date:** 2026-04-26 (observation), 2026-04-27 (TCP implementation complete)
**Decision:** After `deleg-load800-run2` empirically confirmed the MQTT dispatch
serialisation overhead (>9ms per dispatch, 9 × per cycle, miss persists), the
MQTT data plane was replaced with TCP binary transport as documented in DEC-008.
This decision entry records the *empirical confirmation* of the projected overhead
and the *implementation outcome*.

**Measured (deleg-load800-run2):**
- dispatch overhead: >9ms each → >83ms/cycle for 9 dispatches
- total cycle: 17ms local + >83ms dispatch = >100ms → miss every cycle

**Implemented (fw-0.4.0-tcp, deleg-tcp-run6 canonical result):**
- per-dispatch enqueue: <1ms (non-blocking xQueueSend to FreeRTOS queue)
- background sender drains queue at priority 1 (below compute at priority 2)
- total compute_task exec: ~14ms (2 local blocks) + <1ms (9 queue posts) ≈ 15ms
- miss drops: 20/20 → 0.12/20 steady-state

---

## DEC-019 — Peer IP propagated via MQTT telemetry for TCP connect
**Date:** 2026-04-27
**Decision:** Add `"ip":"%s"` field to the MQTT telemetry JSON published by
`manager_task`. Parse `"ip"` from incoming peer telemetry in `mqtt.c` and store in
`peer->ip_addr[16]`. When a DELEGATE_ACCEPT arrives, look up `peer->ip_addr` to
open the TCP connection (`work_transport_connect`).
**Alternatives rejected:**
- Include IP in DELEGATE_ACCEPT payload: would require protocol change.
- mDNS/node ID resolution: introduces ESP-IDF mDNS dependency, adds latency.
- Hard-coded IPs: incompatible with DHCP-assigned addresses.
**Reason:** Telemetry already reaches all nodes every 1s before delegation fires.
By the time DELEGATE_ACCEPT arrives, the peer's IP is already known from at least
one telemetry cycle. Zero-cost: reuses existing MQTT control path.

**Bug discovered and fixed:** `parse_ip_field()` in `mqtt.c` had a guard
`if (len > 511) return;` but actual telemetry payloads are 608 bytes. This silently
prevented the IP from ever being stored, so `find_peer_ip()` always returned NULL,
`tcp_fd` stayed -1, every dispatch returned DISPATCH_ERROR. Runs deleg-tcp-run1
through run3 all showed `dispatched=0`. Fix: scan only the first 128 bytes (the
`"ip"` field is always within the first ~50 bytes of the JSON); drop the size guard
entirely. Fixed in deleg-tcp-run4 (dispatched=7228 confirmed working).

---

## DEC-020 — Async TCP sender queue: decouples compute_task exec from WiFi latency
**Date:** 2026-04-27
**Decision:** `compute_task` does not call `send_exact()` directly. Instead,
`work_transport_enqueue_item()` allocates a 7208-byte frame buffer, copies header
and matrices, and posts a pointer to a per-channel FreeRTOS queue
(`WORK_SEND_QUEUE_DEPTH=20`). A background `work_sender_task` (priority 1) dequeues
frames and calls `send_exact()`. `compute_task`'s exec window ends after the queue
post (<1ms per item).

**Problem solved:** In `deleg-tcp-run4` (first working TCP run), TCP dispatch was
synchronous inside `compute_task`'s exec window. `send_exact(7208 bytes)` blocked
waiting for the 2920-byte lwIP send buffer to drain over WiFi. Four dispatches per
cycle × ~25ms each = ~100ms blocking → exec still exceeded 100ms, miss=19.78/20.
Introducing the async queue in `deleg-tcp-run5` increased dispatch throughput
(10500 vs 7228) but didn't fix miss, because `work_sender_task` was at priority 2
(same as `compute_task`). Time-sliced execution caused WiFi ISR preemptions during
`compute_task`'s exec window.

**Priority 1 for sender:** ESP32 WiFi driver tasks run at priority ~22. When
`work_sender_task` (priority 1) is running, WiFi TX processing triggers ISRs and
the lwIP task. These preempt `work_sender_task` but NOT `compute_task` (priority 2),
because `work_sender_task` is lower-priority and is scheduled only during
`compute_task`'s idle window (vTaskDelayUntil sleep). When `compute_task` wakes,
`work_sender_task` immediately yields. WiFi TX preemptions now happen exclusively
during compute's idle phase, not during its exec window.

**Result (`deleg-tcp-run6`):** `compute_task` exec_ticks ≈ 15ms per cycle.
miss avg ss = 0.12/20 (vs 20/20 baseline).

---

## DEC-021 — SO_SNDTIMEO set after connect(), not before
**Date:** 2026-04-27
**Decision:** In `work_transport_connect()`, `setsockopt(SO_SNDTIMEO)` is called
only after `connect()` returns successfully.
**Reason:** ESP-IDF lwIP quirk: `SO_SNDTIMEO` also constrains `connect()` when
set beforehand. On a congested WiFi mesh, the 200ms timeout then causes `connect()`
to fail with EAGAIN. Fix: set the timeout only on an established socket.
**Evidence:** Discovered during initial TCP integration. `connect()` was returning
EAGAIN consistently on a live 4-node mesh. Moving `setsockopt` after `connect()`
resolved all connection failures immediately.

---

## DEC-022 — Queue-full treated as DISPATCH_BUSY, not DISPATCH_ERROR
**Date:** 2026-04-27
**Decision:** When `work_transport_enqueue_item()` fails because the async send
queue is full, `delegation_dispatch_work_item()` returns `DISPATCH_BUSY` (not
`DISPATCH_ERROR`). `DISPATCH_BUSY` → compute_task skips the block (no fallback
local compute). `DISPATCH_ERROR` → compute_task runs `compute_kernel()` locally.

**Problem:** In `deleg-tcp-run5`, the async queue occasionally filled (sender task
lagged on large WiFi transfers). Queue-full returned DISPATCH_ERROR, causing
fallback `compute_kernel()` to run for each failed enqueue. With potentially 5–8
blocks failing per cycle, local work jumped to 7–15 blocks × 7ms = 49–105ms,
pushing exec past the 100ms deadline again.

**Fix rationale:** A queue-full condition is a backpressure signal, not a hard
error. The frame will be retried next cycle when the queue has drained. Running it
locally defeats the purpose of delegation (adds load to the victim) and is worse
than skipping it (the host has capacity, the queue is just momentarily full).
`DISPATCH_BUSY` semantics already exist for the in-flight cap — queue-full is
exactly the same class of transient backpressure.

**Combined effect of DEC-020 + DEC-022:** compute_task's local work is bounded at
`local_blocks = eff_blocks - dispatch_blocks` = 2 blocks regardless of queue
pressure. exec_ticks ≈ 15ms. WiFi TX is isolated to `work_sender_task` (priority 1).
