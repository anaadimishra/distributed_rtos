#ifndef WORK_TRANSPORT_H
#define WORK_TRANSPORT_H

#include "core/system_context.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_err.h"
#include <stdint.h>

/*
 * TCP binary work transport — data plane for delegation work items and results.
 * MQTT remains the control plane (telemetry, handshake).
 *
 * Frame layout (packed):
 *   work_frame_hdr_t  (8 bytes)
 *   payload           (7200 bytes for work_item, 3600 bytes for work_result)
 *
 * All integers are native-endian (ESP32 is little-endian; peers are also ESP32).
 *
 * Send path is async: compute_task posts to a per-channel FreeRTOS queue via
 * work_transport_enqueue_item (non-blocking), which decouples compute_task's
 * exec window from TCP send latency. A background work_sender_task drains the
 * queue and performs the blocking send_exact calls.
 */

#define WORK_FRAME_MATRIX_INTS  (MATRIX_SIZE * MATRIX_SIZE)   /* 900 */
#define WORK_FRAME_ITEM_BYTES   (8 + WORK_FRAME_MATRIX_INTS * 4 * 2)  /* 7208 */
#define WORK_FRAME_RESULT_BYTES (8 + WORK_FRAME_MATRIX_INTS * 4)      /* 3608 */

typedef enum __attribute__((packed)) {
    FRAME_WORK_ITEM   = 0x01,
    FRAME_WORK_RESULT = 0x02,
} work_frame_type_t;

typedef struct __attribute__((packed)) {
    uint16_t magic;    /* WORK_TRANSPORT_MAGIC */
    uint8_t  type;     /* work_frame_type_t    */
    uint8_t  block_id;
    uint32_t cycle_id;
} work_frame_hdr_t;   /* 8 bytes */

/* Start the TCP listen server. Call once from app_main after WiFi is up. */
esp_err_t work_transport_server_start(system_context_t *ctx);

/*
 * Open a TCP client connection to peer_ip:WORK_TRANSPORT_PORT.
 * Creates a per-channel send queue and spawns a work_sender_task plus
 * work_recv_task. Stores tcp_send_queue and tcp_sender_task into
 * ctx->channels[channel_idx] directly.
 * Returns socket fd >= 0 on success, -1 on failure.
 */
int work_transport_connect(system_context_t *ctx, const char *peer_ip, int channel_idx);

/* Close a transport connection; the associated recv task exits naturally. */
void work_transport_disconnect(int fd);

/*
 * Enqueue a work item for async sending (called from compute_task context).
 * Allocates a 7208-byte frame buffer, copies header + matrices, and posts
 * to the channel's send queue with a zero-tick timeout (non-blocking).
 * Returns 0 on success, -1 if queue is full or allocation fails.
 * Does NOT block — compute_task exec window is unaffected by TCP latency.
 */
int work_transport_enqueue_item(QueueHandle_t q, uint32_t cycle_id, uint8_t block_id,
                                const int32_t *matrix_a, const int32_t *matrix_b);

/*
 * Tear down a channel's sender task, send queue, and TCP fd.
 * Deletes the sender task, drains and frees any queued frames, deletes the
 * queue, then calls work_transport_disconnect on the fd.
 * Zeroes *sender_task, *send_queue, and sets *fd to -1 on return.
 */
void work_transport_channel_teardown(TaskHandle_t *sender_task,
                                     QueueHandle_t *send_queue,
                                     int *fd);

#endif /* WORK_TRANSPORT_H */
