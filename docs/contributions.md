# Contributions

Use this section directly in dissertation introduction as a numbered list.

1. Empirical schedulability characterisation  
   A reproducible load-sweep harness for FreeRTOS periodic tasks that generates RM/RTA-comparable evidence (CPU, execution-window, queue, and deadline-miss behavior) across single and multi-node clusters.

2. Fault-observable telemetry design  
   A telemetry schema and windowed-stats protocol aligned with fault classes: timing faults (`miss`), omission faults (heartbeat timeout / `last_seen` gap), and crash/failure behavior (telemetry silence or commanded fail-silent), enabling post-hoc fault analysis from JSONL logs.

3. Reproducible evaluation infrastructure  
   A session-based experiment pipeline (`run-lab.sh`, `load_sweep.py`, `analyze_logs.py`) with window-ready gating, skip-seconds control, repeatability summaries, baseline/overhead comparison, and analysis outputs structured for dissertation figures/tables.
