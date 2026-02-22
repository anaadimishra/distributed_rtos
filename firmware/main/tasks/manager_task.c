#include "tasks/manager_task.h"
#include "config/config.h"
#include "core/metrics.h"
#include "network/mqtt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <stdio.h>

void manager_task(void *param)
{
    system_context_t *ctx = (system_context_t *)param;
    TickType_t last_wake = xTaskGetTickCount();
    char payload[128];

    while (1) {
        metrics_update_cpu_load(ctx);

        if (ctx != NULL && ctx->data_queue != NULL) {
            ctx->queue_depth = uxQueueMessagesWaiting(ctx->data_queue);
        } else if (ctx != NULL) {
            ctx->queue_depth = 0;
        }

        if (ctx != NULL) {
            int len = snprintf(payload, sizeof(payload),
                               "{\"cpu\":%u,\"queue\":%u,\"load\":%u}",
                               (unsigned)ctx->cpu_usage,
                               (unsigned)ctx->queue_depth,
                               (unsigned)ctx->load_factor);

            if (len > 0) {
                mqtt_publish_telemetry(ctx, payload);
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MANAGER_PERIOD_MS));
    }
}
