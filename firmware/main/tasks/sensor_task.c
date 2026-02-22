#include "tasks/sensor_task.h"
#include "config/config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void sensor_task(void *param)
{
    system_context_t *ctx = (system_context_t *)param;
    TickType_t last_wake = xTaskGetTickCount();
    int counter = 0;

    while (1) {
        counter++;
        if (ctx != NULL && ctx->data_queue != NULL) {
            xQueueSend(ctx->data_queue, &counter, 0);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}
