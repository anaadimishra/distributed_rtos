# Day 09 Engineering Log (2026-04-26)

## Focus

- Stabilise multi-peer delegation under slot pressure.
- Convert intermittent reboot failures into concrete serial evidence.
- Harden the experiment harness so runs are reproducible and diagnosable.

## Problem

The first multi-peer delegation runs showed two different failure modes:

- Pending work slots filled and delegation collapsed into local compute fallback, hiding the intended CPU relief.
- Nodes rebooted during delegation tests, but dashboard telemetry alone could only show a `boot_id` change, not the cause.

The engineering objective became: make delegation bounded and observable, then use serial logs to classify crashes precisely.

## Implementation Decisions

### Bounded in-flight delegation pipeline

`delegation_dispatch_work_item()` now returns an explicit result:

- `DISPATCH_OK`
- `DISPATCH_BUSY`
- `DISPATCH_ERROR`

`DISPATCH_BUSY` means the active channels are already at cap or no pending slot is available. The compute task now skips that delegated block for the current cycle instead of running local fallback. This preserves the load-shedding semantics: delegation pressure should reduce work attempted locally, not silently recreate the same CPU load.

Per-channel in-flight work is capped with:

- `DELEGATION_MAX_INFLIGHT_PER_CHANNEL=4`
- `DELEGATION_PENDING_TIMEOUT_MS=2000`

Each pending entry records the owning channel and send timestamp. `delegation_tick()` reclaims timed-out pending entries in a deterministic fixed-array scan.

Telemetry now includes:

- `deleg_inflight_total`
- `deleg_busy_skip`
- `deleg_timeout_reclaim`
- `deleg_dispatch_err`

### Harness fixes

`run-lab.sh` and `experiments/delegation_test.py` were updated after false-negative runs:

- `--hold-seconds` now applies to delegation runs correctly.
- `--min-nodes`, `--nodes-timeout`, and `--nodes-settle-seconds` prevent starting before all nodes appear.
- stale `HOSTING blocks=0` is logged as `stale_hosting_ignored`, not counted as successful hosting.
- victim stress precheck reports when the victim never reaches stress.
- `--serial-monitor` starts detached `screen` monitors for all serial ports and stops them after the run.

Serial logs are now stored under `serial_logs/session_*` and are linked to run output by timestamp.

## Run Evidence

### `multi-peer-run8`

Command:

```bash
./run-lab.sh delegation --label multi-peer-run8 --hold-seconds 60 --low-load 200 --victim node-34A9F0 --mqtt-verbose --min-nodes 4 --nodes-timeout 180 --nodes-settle-seconds 1
```

Result:

- delegation did not fire
- victim max: `cpu=27`, `stress=0`, `eff_blocks=4`, `miss=0`
- telemetry showed victim rebooted and did not sustain `load=950`
- serial log identified the true cause:

```text
mqtt: control: SET_LOAD=950
deleg: offload request -> node-717AC4 blocks=3 slot=0
***ERROR*** A stack overflow in task manager_task has been detected.
```

Fix:

- `MANAGER_TASK_STACK_SIZE` increased from `3072` to `6144`.

### `multi-peer-run9`

Command:

```bash
./run-lab.sh delegation --serial-monitor --label multi-peer-run9 --hold-seconds 60 --low-load 200 --victim node-34A9F0 --mqtt-verbose --min-nodes 4 --nodes-timeout 180 --nodes-settle-seconds 1
```

Result:

- delegation fired
- victim reached `ACTIVE`
- handshake latency: `1006ms`
- time to delegate: `3029ms`
- victim max: `cpu=98`, `stress=2`, `eff_blocks=19`, `miss=20`
- victim dispatched `465` work items and received `397` results
- victim `deleg_inflight_total` maxed at `12`, exactly `3 active channels * 4`
- `deleg_busy_skip` reached `2901`
- `deleg_timeout_reclaim` reached `58`
- `deleg_dispatch_err` remained `0`

This confirms the bounded busy-skip pipeline worked before the next resource limit appeared.

Serial logs then identified a new failure:

```text
***ERROR*** A stack overflow in task compute_task has been detected.
```

Fix:

- `COMPUTE_TASK_STACK_SIZE` increased from `2048` to `8192`.

## Dissertation-Relevant Interpretation

This sequence supports several strong claims:

- The system is not only a best-effort demo; it exposes internal state through telemetry counters that can validate bounded behavior.
- Failure diagnosis combines dashboard telemetry with serial crash logs, which is important for embedded distributed evaluation.
- The busy-skip path is measurable: when peers are saturated, work is skipped and counted rather than hidden as local fallback.
- The pending timeout path is measurable and prevents permanent slot leakage.
- Stack sizing is part of real-time systems engineering; adding distributed payload parsing and JSON dispatch changed task resource needs.

## Next Validation

Build and flash the firmware containing:

- `MANAGER_TASK_STACK_SIZE=6144`
- `COMPUTE_TASK_STACK_SIZE=8192`

Then run:

```bash
./run-lab.sh delegation --serial-monitor --label multi-peer-run10 --hold-seconds 60 --low-load 200 --victim node-34A9F0 --mqtt-verbose --min-nodes 4 --nodes-timeout 180 --nodes-settle-seconds 1
```

Success criteria:

- no stack overflow or reboot in serial logs
- victim remains active through the hold period
- `deleg_dispatched` and `deleg_returned` progress continuously
- `deleg_inflight_total <= active_channels * 4`
- `deleg_dispatch_err == 0`
