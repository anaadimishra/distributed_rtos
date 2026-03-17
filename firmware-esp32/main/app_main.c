// Firmware entry point: bring up system services and RTOS tasks.
#include "config/config.h"
#include "core/system_context.h"
#include "core/metrics.h"
#include "network/wifi.h"
#include "network/mqtt.h"
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
    ctx->boot_id = (uint32_t)esp_random();

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

    // Metrics hook + MQTT connectivity.
#if ENABLE_METRICS
    metrics_init();
#else
    // Allow compute task to run without waiting for baseline samples.
    ctx->cpu_baseline_ready = 1;
#endif
    mqtt_start(ctx);

    // Start periodic RTOS tasks.
    xTaskCreate(sensor_task, "sensor_task", SENSOR_TASK_STACK_SIZE, ctx, SENSOR_TASK_PRIORITY, NULL);
    xTaskCreate(control_task, "control_task", CONTROL_TASK_STACK_SIZE, ctx, CONTROL_TASK_PRIORITY, NULL);
    xTaskCreate(compute_task, "compute_task", COMPUTE_TASK_STACK_SIZE, ctx, COMPUTE_TASK_PRIORITY, NULL);
#if ENABLE_MANAGER_TASK
    xTaskCreate(manager_task, "manager_task", MANAGER_TASK_STACK_SIZE, ctx, MANAGER_TASK_PRIORITY, NULL);
#endif
}
