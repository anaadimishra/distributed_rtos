// Manager task: aggregates metrics and publishes telemetry.
#include "tasks/manager_task.h"
#include "config/config.h"
#include "network/delegation.h"
#include "core/metrics.h"
#include "network/mqtt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static stress_level_t get_stress_level(uint32_t miss, uint32_t exec_max, uint32_t cpu_usage)
{
    if (miss > 0) {
        return STRESS_HIGH;
    }
    if (exec_max > STRESS_EXEC_THRESHOLD_TICKS) {
        return STRESS_MEDIUM;
    }
    if (cpu_usage > STRESS_CPU_THRESHOLD_PCT) {
        return STRESS_MEDIUM;
    }
    return STRESS_LOW;
}

static const char *stress_to_string(stress_level_t stress)
{
    switch (stress) {
        case STRESS_HIGH:
            return "HIGH";
        case STRESS_MEDIUM:
            return "MEDIUM";
        default:
            return "LOW";
    }
}

void manager_task(void *param)
{
    system_context_t *ctx = (system_context_t *)param;
    char payload[768];
    uint32_t log_tick = 0;
    int64_t expected_publish_ms = -1;
    static const char *TAG = "mgr";
    stress_level_t prev_stress = STRESS_LOW;
    uint32_t low_windows_streak = 0;
    uint32_t last_adjust_window_ms = 0;
    uint32_t last_window_exec_max = 0;
    uint32_t last_window_miss = 0;

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
                last_window_exec_max = exec_max;
                last_window_miss = exec_miss;
            } else {
                exec_max = last_window_exec_max;
                exec_miss = last_window_miss;
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

            stress_level_t stress = get_stress_level(exec_miss, exec_max, ctx->cpu_usage);
            ctx->self_stress_level = (uint8_t)stress;

            /* Phase 4: delegation tick — runs every manager cycle */
            delegation_tick(ctx);

            /* Phase 4: attempt offload when MEDIUM or HIGH stress (cpu > 85%).
             * Triggering at MEDIUM fires on the first manager tick above the CPU
             * threshold — before any deadline misses — so delegation completes while
             * the system is still schedulable rather than after it has already crashed
             * into a zero-idle-time overload spiral.
             * delegation_try_offload() skips peers that already have a channel open. */
            if (ctx->self_stress_level >= STRESS_MEDIUM) {
                delegation_try_offload(ctx);
            }

            if (stress != prev_stress) {
#if DEBUG_LOGS
                ESP_LOGI(TAG, "stress transition: %s -> %s",
                         stress_to_string(prev_stress), stress_to_string(stress));
#endif
                prev_stress = stress;
            }

            // Keep peer table fresh in manager task context; callback only updates entries.
            uint32_t now_ms = (uint32_t)t_pub_ms;
#if ADAPT_DECREASE_ENABLED
            int has_low_peer = 0;
#endif
            int has_high_peer = 0;
            for (int i = 0; i < MAX_PEERS; i++) {
                peer_state_t *peer = &ctx->peers[i];
                if (!peer->valid) {
                    continue;
                }
                if ((now_ms - peer->last_seen_ms) > PEER_TIMEOUT_MS) {
                    peer->valid = 0;
                    continue;
                }
#if ADAPT_DECREASE_ENABLED
                if (peer->stress_level == STRESS_LOW) {
                    has_low_peer = 1;
                }
#endif
                if (peer->stress_level == STRESS_HIGH) {
                    has_high_peer = 1;
                }
            }

            // Adaptation policy:
            // - reduce quickly when self is high and any peer is low
            // - increase slowly only after several LOW windows and no HIGH peer
            // - at most one change per compute window (~2s)
            if (ready && (now_ms - last_adjust_window_ms) >= (PROCESSING_WINDOW_CYCLES * COMPUTE_PERIOD_MS)) {
                uint32_t old_load = ctx->load_factor;

#if ADAPT_DECREASE_ENABLED
                if (stress == STRESS_HIGH && has_low_peer &&
                    delegation_active_channel_count(ctx) == 0) {
                    if (ctx->load_factor > LOAD_FACTOR_MIN) {
                        uint32_t next = ctx->load_factor;
                        next = (next > ADAPT_LOAD_STEP) ? (next - ADAPT_LOAD_STEP) : LOAD_FACTOR_MIN;
                        if (next < LOAD_FACTOR_MIN) {
                            next = LOAD_FACTOR_MIN;
                        }
                        ctx->load_factor = next;
                        last_adjust_window_ms = now_ms;
                        low_windows_streak = 0;
#if DEBUG_LOGS
                        ESP_LOGI(TAG, "adapt: peer-assisted reduce load %u -> %u", (unsigned)old_load, (unsigned)next);
#endif
                    }
                } else
#endif
                if (stress == STRESS_LOW && !has_high_peer) {
                    low_windows_streak++;
                    if (low_windows_streak >= ADAPT_LOW_WINDOWS_TO_INCREASE && ctx->load_factor < LOAD_FACTOR_MAX) {
                        uint32_t next = ctx->load_factor + ADAPT_LOAD_STEP;
                        if (next > LOAD_FACTOR_MAX) {
                            next = LOAD_FACTOR_MAX;
                        }
                        ctx->load_factor = next;
                        last_adjust_window_ms = now_ms;
                        low_windows_streak = 0;
#if DEBUG_LOGS
                        ESP_LOGI(TAG, "adapt: cautious increase load %u -> %u", (unsigned)old_load, (unsigned)next);
#endif
                    }
                } else {
                    low_windows_streak = 0;
                }
            }

            // Runtime state model used by dashboard/log analysis.
            const char *state = "SCHEDULABLE";
            if (exec_miss > 0) {
                state = "OVERLOADED";
            } else if (ctx->cpu_usage >= 90) {
                state = "SATURATED";
            }

            /* Phase 4: delegation visibility */
            const char *deleg_state = delegation_node_role_str(ctx);
            int deleg_blocks = delegation_total_delegated_blocks(ctx);
            if (strcmp(deleg_state, "HOSTING") == 0) {
                deleg_blocks = delegation_total_hosted_blocks(ctx);
            }

            int len = snprintf(payload, sizeof(payload),
                               "{\"fw\":\"%s\",\"ip\":\"%s\",\"boot_id\":%u,\"t_pub_ms\":%u,\"t_pub_epoch_ms\":%llu,\"t_actual_publish_ms\":%lld,\"t_expected_publish_ms\":%lld,\"drift_ms\":%lld,\"state\":\"%s\",\"stress_level\":%u,\"cpu\":%u,\"queue\":%u,\"load\":%u,\"blocks\":%u,\"eff_blocks\":%u,\"last_ctrl_seq\":%u,\"exec_avg\":%u,\"exec_max\":%u,\"miss\":%u,\"window_ready\":%u,\"deleg_state\":\"%s\",\"deleg_peer\":\"%s\",\"deleg_blocks\":%d,\"deleg_dispatched\":%u,\"deleg_returned\":%u,\"deleg_inflight_total\":%u,\"deleg_busy_skip\":%u,\"deleg_timeout_reclaim\":%u,\"deleg_dispatch_err\":%u,\"deleg_failover_count\":%u}",
                               FIRMWARE_VERSION,
                               ctx->node_ip,
                               (unsigned)ctx->boot_id,
                               // Milliseconds since boot; used for telemetry latency measurement.
                               (unsigned)t_pub_ms,
                               (unsigned long long)t_pub_epoch_ms,
                               (long long)t_actual_publish_ms,
                               (long long)(t_actual_publish_ms - drift_ms),
                               (long long)drift_ms,
                               state,
                               (unsigned)stress,
                               (unsigned)ctx->cpu_usage,
                               (unsigned)ctx->queue_depth,
                               (unsigned)ctx->load_factor,
                               (unsigned)ctx->active_blocks,
                               (unsigned)ctx->effective_blocks,
                               (unsigned)ctx->last_ctrl_seq,
                               (unsigned)exec_avg,
                               (unsigned)exec_max,
                               (unsigned)exec_miss,
                               (unsigned)ready,
                               deleg_state,
                               delegation_primary_peer(ctx),
                               deleg_blocks,
                               (unsigned)ctx->deleg_blocks_dispatched,
                               (unsigned)ctx->deleg_blocks_returned,
                               (unsigned)ctx->deleg_inflight_total,
                               (unsigned)ctx->deleg_busy_skip,
                               (unsigned)ctx->deleg_timeout_reclaim,
                               (unsigned)ctx->deleg_dispatch_err,
                               (unsigned)ctx->deleg_failover_count);

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
