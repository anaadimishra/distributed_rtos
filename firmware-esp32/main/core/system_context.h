#ifndef SYSTEM_CONTEXT_H
#define SYSTEM_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>

#include "config/config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "mqtt_client.h"

/* MAX_PEERS and MATRIX_SIZE come from config/config.h (included above). */

#define HEARTBEAT_TOPIC         "cluster/heartbeat"
#define DELEGATION_TIMEOUT_MS   3000
#define DELEGATION_MIN_HEADROOM 85    /* refuse requests if cpu >= saturation threshold */

typedef enum {
    STRESS_LOW = 0,
    STRESS_MEDIUM = 1,
    STRESS_HIGH = 2,
} stress_level_t;

typedef struct {
    char node_id[16];
    uint8_t stress_level;
    uint32_t last_seen_ms;
    uint8_t valid;
    char ip_addr[16];   /* IPv4 dotted-decimal from peer telemetry */
} peer_state_t;

typedef enum {
    CHAN_IDLE = 0,
    CHAN_REQUESTING,   /* sent request, waiting for reply */
    CHAN_ACTIVE,       /* we offloaded blocks to this peer; dispatching work items */
    CHAN_HOSTING,      /* we accepted work items from this peer */
} chan_state_t;

typedef struct {
    chan_state_t  state;
    char          peer_id[16];
    int           blocks;
    uint8_t       in_flight_count;
    int64_t       start_ms;
    int           tcp_fd;           /* TCP socket when ACTIVE, -1 otherwise */
    QueueHandle_t tcp_send_queue;   /* async send queue; NULL when not ACTIVE */
    TaskHandle_t  tcp_sender_task;  /* background sender task handle; NULL when idle */
} delegation_channel_t;

/* Tracks a single in-flight dispatched compute block. */
typedef struct {
    uint32_t cycle_id;
    uint8_t  block_id;
    uint8_t  channel_idx;  /* owner channel index */
    char     peer_id[16];   /* which host this was sent to */
    uint32_t sent_ms;
    bool     in_flight;
} pending_work_t;

// Shared runtime context passed to tasks and network code.
typedef struct {
    // Node identity and MQTT topics.
    char node_id[16];
    char node_ip[16];       /* own IPv4 address, set after WiFi up */
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
    // Fault injection: when set, manager telemetry publish is suppressed.
    volatile uint32_t telemetry_suppressed;

    // Distributed adaptation: peer stress tracking and local stress.
    volatile uint8_t self_stress_level;
    peer_state_t peers[MAX_PEERS];

    /* Phase 4 — delegation handshake */
    SemaphoreHandle_t    peers_mutex;          /* protects peers[] writes */
    delegation_channel_t channels[MAX_DELEGATION_CHANNELS];

    /* Phase 4 — work item dispatch */
    uint32_t           compute_cycle_id;
    pending_work_t     pending_work[MAX_PENDING_WORK];
    volatile uint32_t  deleg_blocks_dispatched;
    volatile uint32_t  deleg_blocks_returned;
    volatile uint32_t  deleg_inflight_total;
    volatile uint32_t  deleg_busy_skip;
    volatile uint32_t  deleg_timeout_reclaim;
    volatile uint32_t  deleg_dispatch_err;
    volatile uint32_t  deleg_failover_count;
} system_context_t;

#endif
