// Manager task: aggregates metrics and publishes telemetry.
#include "tasks/manager_task.h"
#include "config/config.h"
#include "core/metrics.h"
#include "network/mqtt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include <stdio.h>

void manager_task(void *param)
{
    system_context_t *ctx = (system_context_t *)param;
    char payload[768];
    uint32_t log_tick = 0;
    int64_t expected_publish_ms = -1;
    static const char *TAG = "mgr";

    while (1) {
        // Refresh CPU load and queue depth.
#if ENABLE_METRICS
        metrics_update_cpu_load(ctx);
#else
        if (ctx != NULL) {
            ctx->cpu_usage = 0;
        }
#endif

        if (ctx != NULL && ctx->data_queue != NULL) {
            ctx->queue_depth = uxQueueMessagesWaiting(ctx->data_queue);
        } else if (ctx != NULL) {
            ctx->queue_depth = 0;
        }

        if (ctx != NULL) {
            // Gate windowed stats: only publish when the compute task marks them ready.
            uint32_t exec_avg = 0;
            uint32_t exec_max = 0;
            uint32_t exec_miss = 0;
            uint32_t ready = ctx->processing_window_ready;

            if (ready) {
                exec_avg = ctx->processing_window_avg;
                exec_max = ctx->processing_window_max;
                exec_miss = ctx->processing_window_miss;
            }

            uint32_t t_pub_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            uint64_t t_pub_epoch_ms = 0;
            if (ctx->time_sync_ready) {
                t_pub_epoch_ms = (uint64_t)((int64_t)t_pub_ms + ctx->time_offset_ms);
            }
            // Publish-time drift tracking:
            // expected publish time = previous actual publish + MANAGER_PERIOD_MS.
            int64_t t_actual_publish_ms =
                (ctx->time_sync_ready && t_pub_epoch_ms > 0) ? (int64_t)t_pub_epoch_ms : (int64_t)t_pub_ms;
            if (expected_publish_ms < 0) {
                expected_publish_ms = t_actual_publish_ms;
            }
            int64_t drift_ms = t_actual_publish_ms - expected_publish_ms;
            expected_publish_ms = t_actual_publish_ms + MANAGER_PERIOD_MS;

            // Runtime state model used by dashboard/log analysis.
            const char *state = "SCHEDULABLE";
            if (exec_miss > 0) {
                state = "OVERLOADED";
            } else if (ctx->cpu_usage >= 90) {
                state = "SATURATED";
            }

            int len = snprintf(payload, sizeof(payload),
                               "{\"fw\":\"%s\",\"boot_id\":%u,\"t_pub_ms\":%u,\"t_pub_epoch_ms\":%llu,\"t_actual_publish_ms\":%lld,\"t_expected_publish_ms\":%lld,\"drift_ms\":%lld,\"state\":\"%s\",\"cpu\":%u,\"queue\":%u,\"load\":%u,\"blocks\":%u,\"eff_blocks\":%u,\"last_ctrl_seq\":%u,\"exec_avg\":%u,\"exec_max\":%u,\"miss\":%u,\"window_ready\":%u}",
                               FIRMWARE_VERSION,
                               (unsigned)ctx->boot_id,
                               // Milliseconds since boot; used for telemetry latency measurement.
                               (unsigned)t_pub_ms,
                               (unsigned long long)t_pub_epoch_ms,
                               (long long)t_actual_publish_ms,
                               (long long)(t_actual_publish_ms - drift_ms),
                               (long long)drift_ms,
                               state,
                               (unsigned)ctx->cpu_usage,
                               (unsigned)ctx->queue_depth,
                               (unsigned)ctx->load_factor,
                               (unsigned)ctx->active_blocks,
                               (unsigned)ctx->effective_blocks,
                               (unsigned)ctx->last_ctrl_seq,
                               (unsigned)exec_avg,
                               (unsigned)exec_max,
                               (unsigned)exec_miss,
                               (unsigned)ready);

            if (len > 0 && len < (int)sizeof(payload)) {
                mqtt_publish_telemetry(ctx, payload);
            }

            // Clear the ready flag after publishing.
            ctx->processing_window_ready = 0;
        }

#if DEBUG_LOGS
        // Compact periodic log every 5 seconds.
        log_tick++;
        if (log_tick >= 5) {
            log_tick = 0;
            if (ctx != NULL) {
                ESP_LOGI(TAG, "cpu=%u q=%u load=%u cap=%u eff=%u win=%u",
                         (unsigned)ctx->cpu_usage,
                         (unsigned)ctx->queue_depth,
                         (unsigned)ctx->load_factor,
                         (unsigned)ctx->active_blocks,
                         (unsigned)ctx->effective_blocks,
                         (unsigned)ctx->processing_window_ready);
            }
        }
#endif

        // Intentionally use vTaskDelay (not vTaskDelayUntil):
        // manager_task is observability infrastructure, not a hard real-time task.
        // We measure/publish drift_ms and accept small drift relative to 1000ms.
        vTaskDelay(pdMS_TO_TICKS(MANAGER_PERIOD_MS));
    }
}
