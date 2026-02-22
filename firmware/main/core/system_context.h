#ifndef SYSTEM_CONTEXT_H
#define SYSTEM_CONTEXT_H

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"

typedef struct {
    QueueHandle_t data_queue;
    esp_mqtt_client_handle_t mqtt_client;
    EventGroupHandle_t wifi_event_group;
    volatile uint32_t cpu_usage;
    volatile uint32_t queue_depth;
    volatile uint32_t load_factor;
} system_context_t;

#endif
