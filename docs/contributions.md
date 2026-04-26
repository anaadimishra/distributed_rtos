# Contributions

Use this section directly in dissertation introduction as a numbered list.

1. Empirical schedulability characterisation  
   A reproducible load-sweep harness for FreeRTOS periodic tasks that generates RM/RTA-comparable evidence (CPU, execution-window, queue, and deadline-miss behavior) across single and multi-node clusters.

2. Fault-observable telemetry design  
   A telemetry schema and windowed-stats protocol aligned with fault classes: timing faults (`miss`), omission faults (heartbeat timeout / `last_seen` gap), and crash/failure behavior (telemetry silence or commanded fail-silent), enabling post-hoc fault analysis from JSONL logs.

3. Reproducible evaluation infrastructure  
   A session-based experiment pipeline (`run-lab.sh`, `load_sweep.py`, `analyze_logs.py`) with window-ready gating, skip-seconds control, repeatability summaries, baseline/overhead comparison, and analysis outputs structured for dissertation figures/tables.

4. Bounded multi-peer delegation with observable backpressure  
   A fixed-channel delegation mechanism that dispatches real matrix inputs over MQTT, bounds per-peer in-flight work, skips busy delegated blocks instead of collapsing into local fallback, reclaims timed-out pending slots, and exposes all relevant counters in telemetry.

5. Embedded fault diagnosis integrated into the experiment harness  
   Automated serial capture (`run-lab.sh --serial-monitor`) records one log per attached ESP32 during experiments, allowing dashboard-level symptoms such as reboot or telemetry loss to be mapped to concrete firmware faults such as task stack overflow and reset reason.
