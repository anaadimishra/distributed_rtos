#include "core/metrics.h"

volatile uint32_t idle_counter = 0;
volatile uint32_t idle_prev = 0;
volatile uint32_t idle_baseline = 0;

void vApplicationIdleHook(void)
{
    idle_counter++;
}

void metrics_update_cpu_load(system_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    uint32_t idle_now = idle_counter;
    uint32_t delta = idle_now - idle_prev;

    idle_prev = idle_now;

    if (idle_baseline == 0 || delta > idle_baseline) {
        idle_baseline = delta;
    }

    if (idle_baseline > 0) {
        uint32_t idle_pct = (delta * 100U) / idle_baseline;
        if (idle_pct > 100U) {
            idle_pct = 100U;
        }
        ctx->cpu_usage = 100U - idle_pct;
    } else {
        ctx->cpu_usage = 0;
    }
}
