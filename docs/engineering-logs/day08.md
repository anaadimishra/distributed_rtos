# Day 08 Engineering Log (2026-04-19 – 2026-04-25)

## Focus
- Phase 4: True compute task delegation with actual I/O exchange.
- Fix a fundamental design gap: the existing delegation engine negotiated load
  shedding (active_blocks counts) but never moved any data between nodes.
- Implement, validate, and document genuine work-item dispatch.

---

## Problem Identified

The delegation engine built in the previous session (`delegation.c`) only coordinated
which node reduced its `active_blocks` count and which increased it. No work items,
no inputs, no outputs traveled between nodes. The dissertation requires actual I/O
exchange: a stressed node dispatches compute work with its inputs to a peer, the peer
executes it, and the result comes back.

This was caught during review. The delegation state machine (IDLE → REQUESTING →
ACTIVE/HOSTING) and handshake protocol (DELEGATE_REQUEST / ACCEPT / REJECT) were
sound and retained unchanged. Only the data-plane was missing.

---

## Design Decisions

### 1. Work item = full inputs, not a seed or partial data

Early consideration: use a seed value to avoid transmitting large matrix payloads
(seed → peer regenerates same matrices locally). Rejected. This is circular — if the
peer can regenerate the inputs from a seed, no real data exchange is happening. The
point of the system is that inputs travel from the delegating node to the executing
node.

Decision: send both `matrix_a` and `matrix_b` in full with every work item. The peer
has no assumed knowledge of the inputs. This reflects how real task delegation works.

### 2. MATRIX_SIZE kept at 30 — no reduction

Reducing MATRIX_SIZE (e.g. to 8) to fit within MQTT's 1024-byte buffer would
invalidate all Phase 1/2 evaluation data (load sweep thresholds, SAT/OVL boundaries,
exec timing) which were collected at MATRIX_SIZE=30. Data integrity takes priority.

Decision: raise `CONFIG_MQTT_BUFFER_SIZE` to 32768 in the MQTT client config
(`config.buffer_size = 32768` in `mqtt_start()`). Full 30×30 matrices travel in
both directions. work_item payload ≈ 16–20 KB; work_result ≈ 8 KB.

### 3. Host does NOT inflate active_blocks on accept

Previous implementation: `delegation_handle_request()` increased `ctx->active_blocks`
on the host when accepting. With true work-item dispatch, the host's extra work
arrives via `work_item` MQTT messages and is executed in the MQTT callback — not
through the local compute loop. Inflating `active_blocks` would double-count the work.

Decision: remove `active_blocks` modification from `delegation_handle_request()`.
The host's `delegation_tick()` HOSTING path also no longer decreases `active_blocks`
on peer disappearance (since it was never changed on accept).

### 4. Round-robin peer selection for multi-block dispatch

When `delegation_state == DELEGATION_ACTIVE`, the compute task dispatches
`delegation_blocks` work items per cycle (one per 100ms cycle). Peer selection
uses a static `rr_index` that cycles across all `STRESS_LOW` peers in the peer
table. Provides even distribution without requiring a separate scheduler.

### 5. Transport protocol: stay with MQTT

Discussion on alternatives:
- **ESP-NOW**: Espressif proprietary, ~1ms latency, peer-to-peer at WiFi MAC layer,
  no broker. Payload limited to 250 bytes — cannot carry full matrices.
- **UDP**: Low latency, no broker, binary encoding possible. No delivery guarantee,
  no ACK. Silent packet loss means work items go unacknowledged — `pending_work`
  slots stay `in_flight` indefinitely.
- **TCP**: Reliable, ordered, ACK built in. Persistent connection per peer maps
  cleanly to the existing peer table. Binary encoding removes JSON overhead.
  Would require `network/tcp_work.c`, peer IP resolution, connection lifecycle.
- **Dual-protocol**: MQTT for control plane (telemetry, handshake, visibility) +
  TCP/UDP for data plane (work_item, work_result). Architecturally clean — mirrors
  how production distributed systems separate control and data planes.

Decision: **stay with MQTT** for this dissertation phase. Rationale:
1. Delegation validation evidence is not yet captured. Introducing a new transport
   protocol risks new failure modes before any result exists.
2. MQTT provides full observability at no extra cost — every work_item and
   work_result is logged by the broker and visible in the dashboard.
3. The 32KB buffer change is already in place.
4. Dual-protocol (MQTT + TCP direct) is documented as the natural production
   evolution and will appear in the dissertation as future work.

---

## Implementation Summary

### New: `delegation_dispatch_work_item(ctx, block_id, matrix_a, matrix_b)`
- Called by `compute_task` each cycle when `delegation_state == DELEGATION_ACTIVE`
- Round-robin selects a `STRESS_LOW` peer from the peer table
- Heap-allocates a ~32KB JSON buffer, serialises both matrices (row-major flat arrays)
- Publishes to `cluster/{peer}/work_item`
- Records entry in `ctx->pending_work[]`, increments `ctx->deleg_blocks_dispatched`

### New: `delegation_handle_work_item(ctx, data, data_len)`
- Called by MQTT event handler on `cluster/{node_id}/work_item`
- Only executes if `delegation_state == HOSTING`
- Heap-allocates, parses `matrix_a` (900 ints) and `matrix_b` (900 ints)
- Computes C = A × B (O(n³), n=30)
- Serialises result (900 ints), publishes to `cluster/{from}/work_result`

### New: `delegation_handle_work_result(ctx, data, data_len)`
- Called by MQTT event handler on `cluster/{node_id}/work_result`
- Parses `cycle_id`, `block_id`, full result array (900 ints — confirms complete receipt)
- Matches to `pending_work[]` entry, clears `in_flight`, increments `deleg_blocks_returned`

### Modified: `compute_task.c`
- Increments `ctx->compute_cycle_id` each tick
- After the local block loop: if `DELEGATION_ACTIVE`, dispatches `delegation_blocks`
  work items via `delegation_dispatch_work_item()`
- Passes `matrix_a` and `matrix_b` as flat row-major pointers

### Modified: `mqtt.c`
- Subscribes to `cluster/{node_id}/work_item` and `cluster/{node_id}/work_result`
  on connect
- Dispatches to new handlers in `MQTT_EVENT_DATA`
- Sets `config.buffer_size = 32768`

### Modified: `manager_task.c`
- Adds `deleg_dispatched` and `deleg_returned` to telemetry JSON

### Modified: `system_context.h`
- Now includes `config/config.h` (needed for `MATRIX_SIZE` in `pending_work_t`)
- Adds `pending_work_t` struct (cycle_id, block_id, in_flight)
- Adds `compute_cycle_id`, `pending_work[MAX_PENDING_WORK]`,
  `deleg_blocks_dispatched`, `deleg_blocks_returned` to `system_context_t`

### Modified: `config/config.h`
- Adds `MQTT_WORK_ITEM_TOPIC`, `MQTT_WORK_RESULT_TOPIC`, `MAX_PENDING_WORK 20`

### Modified: dashboard (`app.py`, `app.js`, `index.html`)
- Tracks and displays `deleg_dispatched` and `deleg_returned` per node

### Modified: `experiments/analyze_delegation.py`
- Extracts `deleg_dispatched` and `deleg_returned` into timeline CSV and summary CSV
- Adds `max_deleg_dispatched` and `max_deleg_returned` to per-node summary stats

---

## What Stays the Same
- Delegation handshake (IDLE → REQUESTING → ACTIVE/HOSTING) — unchanged
- `delegation_tick()`, `delegation_try_offload()` — unchanged
- `ADAPT_LOW_WINDOWS_TO_INCREASE = 9999` — auto-scale disabled for validation
- All Phase 1/2 evaluation data — unaffected (different code path)
- FIRMWARE_VERSION: `fw-0.3.0-deleg`

---

## Pending
- Flash all nodes with rebuilt firmware (full clean build from scratch)
- Run `./run-lab.sh delegation` and confirm:
  - `deleg_dispatched` > 0 on stressed node
  - `deleg_returned` > 0 and close to dispatched
  - MQTT monitor shows `work_item` messages with `matrix_a`/`matrix_b` arrays
  - `work_result` messages with `result` arrays returning
- Capture `delegation_summary.csv` for dissertation evidence
- Update `docs/formal-grounding.md` with empirical C_i values
