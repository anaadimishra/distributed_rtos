#ifndef METRICS_H
#define METRICS_H

#include <stdint.h>
#include "core/system_context.h"

extern volatile uint32_t idle_counter;
extern volatile uint32_t idle_prev;
extern volatile uint32_t idle_baseline;

void metrics_update_cpu_load(system_context_t *ctx);

#endif
