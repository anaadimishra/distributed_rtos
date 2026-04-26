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
matrices over MQTT; host executes and returns full result matrix. Both directions
carry real data.
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
**Decision:** Full 30×30 matrices transmitted in work_item (≈16–20 KB JSON).
MQTT client buffer raised to 32768 bytes via `config.buffer_size = 32768`.
**Rejected:** Reducing MATRIX_SIZE (e.g. to 8) to fit default 1024-byte buffer.
**Reason:** All Phase 1/2 load characterisation data (SAT=700, OVL=800 thresholds,
exec timing, deadline miss profiles) was collected at MATRIX_SIZE=30. Changing it
would make the delegation phase data incomparable to the baseline.

---

## DEC-007 — Host does not inflate active_blocks on delegation accept
**Date:** Day 08
**Decision:** `delegation_handle_request()` no longer increases `ctx->active_blocks`
when accepting a delegation request.
**Reason:** With true work-item dispatch, the host's extra compute work arrives via
`work_item` MQTT messages and executes in the MQTT callback. Inflating
`active_blocks` would cause the local compute loop to also run extra blocks —
double-counting the delegated work. The host's load comes from executing received
work items, not from its own scheduling loop.

---

## DEC-008 — Transport protocol: MQTT for both control and data planes
**Date:** Day 08
**Decision:** Use MQTT for work_item and work_result dispatch (data plane) as
well as for telemetry and delegation handshake (control plane).
**Alternatives evaluated:**
- **ESP-NOW:** ~1ms latency, no broker, peer-to-peer at WiFi MAC. Rejected —
  250-byte payload limit cannot carry full 30×30 matrices.
- **UDP:** Low latency, binary encoding, no broker. Rejected — no delivery
  guarantee. Silent packet loss leaves `pending_work` slots in_flight
  indefinitely. Would require application-layer retransmission (reimplementing TCP).
- **TCP direct:** Reliable, ordered, ACK built in. Binary encoding removes JSON
  overhead. Persistent per-peer connections map cleanly to the peer table.
  Valid architecture but adds socket lifecycle, peer IP resolution, connection
  management before any delegation evidence exists.
- **Dual-protocol (MQTT control + TCP data):** Architecturally clean, mirrors
  production distributed systems (e.g. Kubernetes: etcd for control, direct pod
  networking for data). Documented as the natural production evolution.
**Reason for staying with MQTT:** Delegation validation evidence is not yet
captured. Introducing a new transport before the first successful run risks
new failure modes with no baseline. MQTT provides full observability at no
extra cost. TCP/direct peer communication is documented as future work.

---

## DEC-009 — ADAPT_LOW_WINDOWS_TO_INCREASE = 9999 during delegation validation
**Date:** Day 07
**Decision:** Disable the auto-scale-up policy during delegation validation runs.
**Reason:** With `ADAPT_LOW_WINDOWS_TO_INCREASE = 3`, bystander nodes were
climbing their own load every ~6 seconds during the warmup phase, corrupting
the controlled asymmetry needed for delegation testing. Setting to 9999
effectively disables the increase path while preserving the decrease path
(stress-triggered load reduction still fires).
**Note:** Re-enable (set back to 3 or similar) after delegation evidence is captured.

---

## DEC-010 — Round-robin peer selection for work-item dispatch
**Date:** Day 08
**Decision:** Static `rr_index` cycles across `STRESS_LOW` peers in the peer
table on each `delegation_dispatch_work_item()` call.
**Reason:** Even distribution across available peers without a separate scheduler
or priority queue. Simple, deterministic, O(n) in peer count. Sufficient for
a 2–5 node cluster.

---

## DEC-011 — Victim reduces local blocks when DELEGATION_ACTIVE
**Date:** Day 09
**Decision:** When `delegation_state == DELEGATION_ACTIVE`, `compute_task` runs
`blocks - delegation_blocks` locally and dispatches `delegation_blocks` to the host.
Previously it ran all blocks locally AND dispatched on top — delegation had zero
effect on the victim's CPU.
**Reason:** The dissertation argument is that delegation provides load relief. Without
this fix, the victim stays at 100% CPU and OVERLOADED throughout delegation — the
mechanism is demonstrated but the benefit is not. With this fix, the victim's local
compute drops by the delegated fraction, producing an observable state transition
(OVERLOADED → SATURATED or SCHEDULABLE) that is the core evaluation claim.

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

---

## DEC-014 — Per-channel in-flight cap and pending timeout reclaim
**Date:** Day 09
**Decision:** Cap in-flight work per active channel at
`DELEGATION_MAX_INFLIGHT_PER_CHANNEL=4` and reclaim pending slots older than
`DELEGATION_PENDING_TIMEOUT_MS=2000`.
**Reason:** MQTT work results can be delayed or lost. Without timeout reclaim,
fixed pending slots can leak permanently. A cap of 4 prevents one peer from
monopolising the pending table and keeps `MAX_PENDING_WORK=20` usable across
multiple peers.
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

---

## DEC-017 — ADAPT_DECREASE_ENABLED=0 for delegation-only evaluation
**Date:** 2026-04-26
**Decision:** Add `#define ADAPT_DECREASE_ENABLED 0` to `config.h` and guard the
Phase 3 load-decrease block in `manager_task.c` with `#if ADAPT_DECREASE_ENABLED`.
**Reason:** The Phase 3 DECREASE path (`stress == STRESS_HIGH && has_low_peer →
load_factor -= ADAPT_LOAD_STEP`) fires under the same condition as delegation
(`STRESS_HIGH` with reachable low-stress peers). During `deleg-load800-run1`,
`load_factor` stepped `800 → 700 → ... → 100` every ~2s throughout the hold period,
making it impossible to attribute any CPU or miss reduction to delegation. The
confound was discovered by inspecting the `load` field in the telemetry JSONL.
`ADAPT_LOW_WINDOWS_TO_INCREASE=9999` only disabled the INCREASE path; the DECREASE
path required a separate flag.
**Trade-off:** Disabling decrease means bystanders cannot adapt their own load down
if they become stressed from hosting. This is acceptable for controlled delegation
experiments where bystander loads are externally pinned via `--low-load`.
**Note:** Set `ADAPT_DECREASE_ENABLED 1` to restore Phase 3 adaptive behaviour
for any future combined Phase 3+4 evaluation.

---

## DEC-018 — MQTT+JSON chosen as data plane; TCP/binary deferred as future work
**Date:** 2026-04-26
**Decision:** Confirmed MQTT+JSON as the work-item data plane for all Phase 4
evaluation runs. Binary transport (TCP direct or CoAP) documented as future work.
**Reason (empirical):** `deleg-load800-run2` measured dispatch overhead of >9ms per
work item (9 dispatches per 100ms cycle, all budget consumed by `pvPortMalloc` +
`snprintf` + `esp_mqtt_client_publish`). This prevents the victim from reaching
SCHEDULABLE during ACTIVE delegation at load=800.
**Projection:** Binary encoding (7200 bytes vs ~16–20KB JSON) with TCP direct
reduces per-dispatch overhead to ~2–4ms, cutting 9-dispatch overhead from >83ms to
~18–36ms. At 17ms local compute + 27ms dispatch = 44ms total cycle time, the victim
would be SATURATED (cpu~44%) rather than OVERLOADED during ACTIVE delegation.
**Dissertation framing:** The delegation algorithm is correct and transport-agnostic.
The miss persistence in evaluation is an infrastructure constraint arising from
DEC-005/DEC-006 (real matrix data, MATRIX_SIZE=30 preserved) combined with MQTT's
per-message serialisation overhead. Quantified in `docs/threats-to-validity.md §5`.

---

## DEC-016 — Stack budgets updated after delegation became real I/O
**Date:** Day 09
**Decision:** Increase stack budgets for tasks that now carry delegation work:
`MANAGER_TASK_STACK_SIZE=6144` and `COMPUTE_TASK_STACK_SIZE=8192`.
**Reason:** Full matrix JSON dispatch, telemetry formatting, multi-peer request
generation, and work-item parsing increased stack pressure beyond the original
pre-delegation task budgets.
**Trade-off:** Higher static RAM use. Accepted because the alternative is
non-deterministic resets during the evaluation workload.
