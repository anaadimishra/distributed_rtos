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

#include <stdlib.h>

void app_main(void)
{
    system_context_t *ctx = (system_context_t *)calloc(1, sizeof(system_context_t));
    if (ctx == NULL) {
        return;
    }

    ctx->data_queue = xQueueCreate(SENSOR_QUEUE_LENGTH, sizeof(int));
    ctx->wifi_event_group = xEventGroupCreate();
    ctx->load_factor = DEFAULT_LOAD_FACTOR;
    ctx->cpu_usage = 0;
    ctx->queue_depth = 0;

    wifi_init_and_connect(ctx);

    if (ctx->wifi_event_group != NULL) {
        xEventGroupWaitBits(ctx->wifi_event_group,
                            WIFI_CONNECTED_BIT,
                            pdFALSE,
                            pdTRUE,
                            pdMS_TO_TICKS(10000));
    }

    mqtt_start(ctx);

    xTaskCreate(sensor_task, "sensor_task", SENSOR_TASK_STACK_SIZE, ctx, SENSOR_TASK_PRIORITY, NULL);
    xTaskCreate(control_task, "control_task", CONTROL_TASK_STACK_SIZE, ctx, CONTROL_TASK_PRIORITY, NULL);
    xTaskCreate(compute_task, "compute_task", COMPUTE_TASK_STACK_SIZE, ctx, COMPUTE_TASK_PRIORITY, NULL);
    xTaskCreate(manager_task, "manager_task", MANAGER_TASK_STACK_SIZE, ctx, MANAGER_TASK_PRIORITY, NULL);
}
