#include "tasks/control_task.h"
#include "config/config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void control_task(void *param)
{
    system_context_t *ctx = (system_context_t *)param;
    TickType_t last_wake = xTaskGetTickCount();
    int value = 0;

    while (1) {
        if (ctx != NULL && ctx->data_queue != NULL) {
            if (xQueueReceive(ctx->data_queue, &value, 0) == pdTRUE) {
                volatile uint32_t spin = 0;
                for (uint32_t i = 0; i < 2000; i++) {
                    spin += i;
                }
                (void)spin;
            }
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}
