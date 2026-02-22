#include "tasks/compute_task.h"
#include "config/config.h"
#include "core/metrics.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void compute_task(void *param)
{
    system_context_t *ctx = (system_context_t *)param;
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        uint32_t local_load = 0;
        if (ctx != NULL) {
            local_load = ctx->load_factor;
        }
        volatile uint32_t sink = 0;
        for (uint32_t i = 0; i < local_load; i++) {
            sink += i;
        }
        (void)sink;
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(COMPUTE_PERIOD_MS));
    }
}
