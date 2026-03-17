# Session Analysis: session_20260318-024639

## Summary (from CSV)

```
node_id,load,cpu_mean,cpu_p95,cpu_std,volatility_score,queue_mean,queue_p95,eff_blocks_mean,exec_max_p95,miss_p95,ctrl_latency_p95_ms,telemetry_latency_p95_ms,warning
node-717AC4,100,2.67,5,2.4,2.4,0.56,1,1.11,1,0,0,259,queue
node-717AC4,200,14.89,16,1.59,1.59,0,0,4,2,0,0,164,ok
node-717AC4,300,28.67,31,1.76,1.76,0,0,7,4,0,0,97,ok
node-717AC4,400,40,42,1.94,1.94,0,0,9,5,0,0,70,ok
node-717AC4,500,54.67,59,4.29,4.29,0,0,12,7,0,0,81,ok
node-717AC4,600,60.75,69,5.29,5.29,0,0,14,7,0,0,104,ok
node-717AC4,700,73.22,79,4.92,4.92,0,0,16,8,0,0,151,ok
node-717AC4,800,78.89,83,3.98,3.98,0,0,19,10,0,0,135,ok
node-717AC4,900,29.38,61,31.87,31.87,0,0,21,11,20,0,121,miss
node-717AC4,1000,9.43,16,12.98,12.98,0,0,24,13,20,0,92,miss
```

## Interpretation
- 700–800 load: CPU mean high and stable, no deadline misses. System still schedulable.
- 900–1000 load: miss_p95 jumps to 20 (all cycles missed in a 20-cycle window). CPU becomes highly volatile with low mean but high p95, indicating overload and missed deadlines.
- Queue remains near 0, so misses are compute-bound rather than queue backlog driven.

## Raw CPU (window_ready=1, load >= 700)
- File: cpu_raw_window_ready_ge700.txt