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

## Dissertation Outline Hook

Add a dedicated subsection in evaluation/discussion:
- `Threats to Validity`
  - Internal validity
  - External validity
  - Construct validity
  - Mitigations and planned follow-up experiments
