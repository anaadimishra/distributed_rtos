#ifndef SYSTEM_CONTEXT_H
#define SYSTEM_CONTEXT_H

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"

// Shared runtime context passed to tasks and network code.
typedef struct {
    // Node identity and MQTT topics.
    char node_id[16];
    char telemetry_topic[64];
    char control_topic[64];

    // RTOS primitives.
    QueueHandle_t data_queue;
    esp_mqtt_client_handle_t mqtt_client;
    EventGroupHandle_t wifi_event_group;

    // Telemetry fields (updated by tasks).
    volatile uint32_t cpu_usage;
    volatile uint32_t cpu_baseline_ready;
    volatile uint32_t queue_depth;
    volatile uint32_t load_factor;

    // Compute timing and deadline tracking.
    volatile uint32_t deadline_miss_processing;
    volatile uint32_t processing_exec_ticks;
    volatile uint32_t processing_exec_max;
    volatile uint64_t processing_exec_sum;
    volatile uint32_t processing_exec_count;

    // Windowed stats for periodic telemetry.
    volatile uint32_t processing_window_avg;
    volatile uint32_t processing_window_max;
    volatile uint32_t processing_window_miss;
    volatile uint32_t processing_window_ready;

    // Runtime compute scaling.
    volatile uint32_t active_blocks;
    volatile uint32_t effective_blocks;
    volatile uint32_t last_ctrl_seq;
    // Per-boot identifier used to disambiguate telemetry timestamps after reboot.
    volatile uint32_t boot_id;
    // Time sync: offset between node ticks and wall-clock epoch ms.
    volatile int64_t time_offset_ms;
    volatile uint32_t time_sync_ready;
} system_context_t;

#endif
