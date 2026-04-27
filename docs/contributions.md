# Contributions

Use this section directly in dissertation introduction as a numbered list.

1. **Empirical schedulability characterisation**
   A reproducible load-sweep harness for FreeRTOS periodic tasks that generates
   RM/RTA-comparable evidence (CPU, execution-window, queue, and deadline-miss
   behaviour) across single, dual, and five-node clusters. Consistent SAT=700
   (cpu≥90%, miss=0) and OVL=800 (miss>0) thresholds across all topologies,
   confirming the unicore FreeRTOS scheduler is deterministic at cluster scale.

2. **Fault-observable telemetry design**
   A telemetry schema and windowed-stats protocol aligned with fault classes:
   timing faults (`miss`, `exec_ticks`), omission faults (heartbeat timeout /
   `last_seen` gap), and crash/failure behaviour (telemetry silence or commanded
   fail-silent). Enables post-hoc fault analysis from JSONL logs, including
   delegation-specific counters (`deleg_dispatched`, `deleg_returned`,
   `deleg_busy_skip`, `deleg_timeout_reclaim`, `deleg_dispatch_err`).

3. **Reproducible evaluation infrastructure**
   A session-based experiment pipeline (`run-lab.sh`, `load_sweep.py`,
   `delegation_test.py`, `analyze_logs.py`, `analyze_delegation.py`) with
   window-ready gating, skip-seconds control, asymmetric stress orchestration
   (victim/bystander roles), drain phase, repeatability summaries, and analysis
   outputs structured for dissertation figures and tables. Serial log capture
   integrated into the harness (`--serial-monitor`) for firmware fault diagnosis.

4. **Bounded multi-peer delegation with TCP binary transport**
   A fixed-channel delegation mechanism that:
   - dispatches real 30×30 matrix inputs (not synthetic seeds or signals) across
     nodes using a direct TCP binary protocol — 8-byte frame header + 7200-byte
     binary payload, eliminating the MQTT+JSON serialisation bottleneck
   - bounds per-peer in-flight work at `DELEGATION_MAX_INFLIGHT_PER_CHANNEL=4`
     with a timeout-reclaim policy for slow results
   - skips busy/queue-full delegated blocks (DISPATCH_BUSY semantics) instead of
     falling back to local compute, preserving the victim's CPU budget
   - decouples TCP send latency from `compute_task`'s exec window via an async
     FreeRTOS send queue and a priority-1 background sender task, ensuring TCP
     WiFi processing never inflates the victim's measured exec_ticks
   - re-enables load adaptation (ADAPT_DECREASE) when no delegation channels are
     active, giving correct priority ordering: delegation first, load shedding fallback

   **Empirical result:** At load=800 (OVERLOADED, miss=20/20, cpu=100%), TCP
   delegation reduces victim deadline misses to **0.12/20 steady-state** (99%
   reduction, 4-node) and **1.20/20 steady-state** (94% reduction, 5-node).
   CPU drops to 79–85%. Both the MQTT-phase mechanism and TCP-phase outcome are
   documented with full session logs and reproducible run scripts.

5. **Embedded fault diagnosis integrated into the experiment harness**
   Automated serial capture (`run-lab.sh --serial-monitor`) records one log per
   attached ESP32 during experiments, allowing dashboard-level symptoms (node
   reboot, telemetry loss, zero dispatches) to be mapped to concrete firmware
   faults. Evidence collected: task stack overflows in `manager_task` and
   `compute_task` (resolved by stack budget increases), TCP port-5002 server
   confirmed live independently of MQTT (nc test), IP-parse silent-return bug
   identified by correlating payload byte lengths against source guards,
   SO_SNDTIMEO-before-connect ESP-IDF lwIP quirk diagnosed from EAGAIN errno logs.

6. **Design decision and threat-to-validity documentation trail**
   A running log of 22 architecture decisions (`docs/decisions.md`) and a
   structured threats-to-validity analysis (`docs/threats-to-validity.md`)
   covering internal, external, and construct validity, plus six Phase 4
   engineering constraints with root causes, mitigations, and their experimental
   evidence. This documentation trail allows examiners to follow the reasoning
   from first principles to final empirical result without gap.
