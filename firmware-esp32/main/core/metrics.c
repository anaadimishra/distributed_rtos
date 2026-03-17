// CPU load estimation using FreeRTOS idle hook counts.
#include "core/metrics.h"
#include "config/config.h"
#include "esp_freertos_hooks.h"

volatile uint32_t idle_counter = 0;
volatile uint32_t idle_prev = 0;
volatile uint32_t idle_baseline = 0;
static uint32_t idle_baseline_samples = 0;
static uint64_t idle_baseline_sum = 0;

// Idle hook increments a counter each time the idle task runs.
static bool idle_hook_cb(void)
{
    idle_counter++;
    return false;
}

void metrics_init(void)
{
    // Register the idle hook to estimate CPU load.
    esp_register_freertos_idle_hook(idle_hook_cb);
}

void metrics_update_cpu_load(system_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    uint32_t idle_now = idle_counter;
    uint32_t delta = idle_now - idle_prev;

    idle_prev = idle_now;

    // Capture a stable idle baseline once during the initial samples.
    if (idle_baseline == 0 && idle_baseline_samples < CPU_BASELINE_SAMPLES) {
        idle_baseline_sum += delta;
        idle_baseline_samples++;
        if (idle_baseline_samples >= CPU_BASELINE_SAMPLES) {
            idle_baseline = (uint32_t)(idle_baseline_sum / idle_baseline_samples);
            ctx->cpu_baseline_ready = 1;
        }
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
