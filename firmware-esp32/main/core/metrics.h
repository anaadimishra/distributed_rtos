#ifndef METRICS_H
#define METRICS_H

#include <stdint.h>
#include "core/system_context.h"

// Idle counters are updated via a FreeRTOS idle hook.
extern volatile uint32_t idle_counter;
extern volatile uint32_t idle_prev;
extern volatile uint32_t idle_baseline;

// Register idle hook for CPU load tracking.
void metrics_init(void);

// Update ctx->cpu_usage using idle time over the last interval.
void metrics_update_cpu_load(system_context_t *ctx);

#endif
