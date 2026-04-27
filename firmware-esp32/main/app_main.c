// Firmware entry point: bring up system services and RTOS tasks.
#include "config/config.h"
#include "core/system_context.h"
#include "core/metrics.h"
#include "network/wifi.h"
#include "network/mqtt.h"
#include "network/work_transport.h"
#include "tasks/sensor_task.h"
#include "tasks/control_task.h"
#include "tasks/compute_task.h"
#include "tasks/manager_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"

#include <stdlib.h>
#include <stdio.h>

/*
 * Contributions (dissertation-aligned):
 * 1) Empirical schedulability characterization:
 *    Reproducible load-sweep harness for periodic FreeRTOS tasks, yielding
 *    WCET-proxy and deadline-miss evidence comparable with RM/RTA analysis.
 * 2) Fault-observable telemetry design:
 *    Windowed telemetry schema mapping timing/omission/crash classes to
 *    explicit signals (miss counter, heartbeat gap, telemetry silence).
 * 3) Reproducible evaluation infrastructure:
 *    Session-based experiment pipeline (load_sweep + analyze_logs) with
 *    repeatability controls (warmup, skip-seconds, window-ready gating).
 */
void app_main(void)
{
    // Allocate shared context used by all tasks and network layers.
    system_context_t *ctx = (system_context_t *)calloc(1, sizeof(system_context_t));
    if (ctx == NULL) {
        return;
    }

    // Core RTOS primitives and default state.
    ctx->data_queue = xQueueCreate(SENSOR_QUEUE_LENGTH, sizeof(int));
    ctx->wifi_event_group = xEventGroupCreate();
    ctx->load_factor = DEFAULT_LOAD_START;
    ctx->cpu_usage = 0;
    ctx->queue_depth = 0;
    ctx->active_blocks = ACTIVE_BLOCKS;
    // Randomized per-boot identifier to correlate telemetry timestamps across reboots.
    ctx->boot_id = (uint32_t)esp_random();
    ctx->time_offset_ms = 0;
    ctx->time_sync_ready = 0;
    ctx->telemetry_suppressed = 0;

    /* Phase 4 — delegation init (channels[] zeroed by calloc → all CHAN_IDLE).
     * tcp_fd must be -1 (not 0 which is stdin) for each channel. */
    ctx->peers_mutex = xSemaphoreCreateMutex();
    configASSERT(ctx->peers_mutex != NULL);
    for (int i = 0; i < MAX_DELEGATION_CHANNELS; i++) {
        ctx->channels[i].tcp_fd          = -1;
        ctx->channels[i].tcp_send_queue  = NULL;
        ctx->channels[i].tcp_sender_task = NULL;
    }

    // NVS is required by the Wi-Fi stack.
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_ret = nvs_flash_init();
    }
    if (nvs_ret != ESP_OK) {
        return;
    }

    if (esp_netif_init() != ESP_OK) {
        return;
    }

    if (esp_event_loop_create_default() != ESP_OK) {
        return;
    }

    // Wi-Fi connect and wait briefly for link.
    wifi_init_and_connect(ctx);

    if (ctx->wifi_event_group != NULL) {
        xEventGroupWaitBits(ctx->wifi_event_group,
                            WIFI_CONNECTED_BIT,
                            pdFALSE,
                            pdTRUE,
                            pdMS_TO_TICKS(10000));
    }

    /* Capture this node's IP address for peer discovery. */
    {
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            snprintf(ctx->node_ip, sizeof(ctx->node_ip), IPSTR,
                     IP2STR(&ip_info.ip));
        }
    }

    // Metrics hook + MQTT connectivity.
#if ENABLE_METRICS
    metrics_init();
#else
    // Allow compute task to run without waiting for baseline samples.
    ctx->cpu_baseline_ready = 1;
#endif
    mqtt_start(ctx);

    /* Start TCP work transport server (data plane for delegation). */
    work_transport_server_start(ctx);

    // Start periodic RTOS tasks.
    xTaskCreate(sensor_task, "sensor_task", SENSOR_TASK_STACK_SIZE, ctx, SENSOR_TASK_PRIORITY, NULL);
    xTaskCreate(control_task, "control_task", CONTROL_TASK_STACK_SIZE, ctx, CONTROL_TASK_PRIORITY, NULL);
    xTaskCreate(compute_task, "compute_task", COMPUTE_TASK_STACK_SIZE, ctx, COMPUTE_TASK_PRIORITY, NULL);
#if ENABLE_MANAGER_TASK
    xTaskCreate(manager_task, "manager_task", MANAGER_TASK_STACK_SIZE, ctx, MANAGER_TASK_PRIORITY, NULL);
#endif
}
