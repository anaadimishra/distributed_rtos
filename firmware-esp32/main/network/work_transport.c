/* TCP binary work transport — data plane for delegation work items/results. */
#include "network/work_transport.h"
#include "network/delegation.h"
#include "config/config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

static const char *TAG = "wtrans";

/* ------------------------------------------------------------------ helpers */

/* Receive exactly n bytes; returns n on success, <=0 on EOF/error. */
static int recv_exact(int fd, void *buf, int n)
{
    int done = 0;
    while (done < n) {
        int r = recv(fd, (char *)buf + done, n - done, 0);
        if (r <= 0) return r;
        done += r;
    }
    return done;
}

/* Send exactly n bytes; returns n on success, -1 on error. */
static int send_exact(int fd, const void *buf, int n)
{
    int done = 0;
    while (done < n) {
        int r = send(fd, (const char *)buf + done, n - done, 0);
        if (r <= 0) return -1;
        done += r;
    }
    return done;
}

/* ------------------------------------------------------------------ hosting */

typedef struct {
    system_context_t *ctx;
    int               fd;
} work_hosting_args_t;

static void matrix_multiply_local(const int32_t *a, const int32_t *b,
                                   int32_t *c, int sz)
{
    for (int i = 0; i < sz; i++) {
        for (int j = 0; j < sz; j++) {
            int32_t sum = 0;
            for (int k = 0; k < sz; k++) {
                sum += a[i * sz + k] * b[k * sz + j];
            }
            c[i * sz + j] = sum;
        }
    }
}

static void work_hosting_task(void *arg)
{
    work_hosting_args_t *args = (work_hosting_args_t *)arg;
    int fd = args->fd;
    vPortFree(args);

    const int n = WORK_FRAME_MATRIX_INTS;
    int32_t *mat_a  = (int32_t *)pvPortMalloc(n * sizeof(int32_t));
    int32_t *mat_b  = (int32_t *)pvPortMalloc(n * sizeof(int32_t));
    int32_t *result = (int32_t *)pvPortMalloc(n * sizeof(int32_t));

    if (!mat_a || !mat_b || !result) {
        ESP_LOGE(TAG, "hosting: malloc failed");
        goto done;
    }

    while (1) {
        work_frame_hdr_t hdr;
        if (recv_exact(fd, &hdr, sizeof(hdr)) <= 0) break;
        if (hdr.magic != WORK_TRANSPORT_MAGIC || hdr.type != FRAME_WORK_ITEM) break;
        if (recv_exact(fd, mat_a, n * (int)sizeof(int32_t)) <= 0) break;
        if (recv_exact(fd, mat_b, n * (int)sizeof(int32_t)) <= 0) break;

        matrix_multiply_local(mat_a, mat_b, result, MATRIX_SIZE);

        work_frame_hdr_t res_hdr;
        res_hdr.magic    = WORK_TRANSPORT_MAGIC;
        res_hdr.type     = FRAME_WORK_RESULT;
        res_hdr.block_id = hdr.block_id;
        res_hdr.cycle_id = hdr.cycle_id;

        if (send_exact(fd, &res_hdr, sizeof(res_hdr)) < 0) break;
        if (send_exact(fd, result, n * (int)sizeof(int32_t)) < 0) break;
    }

done:
    vPortFree(mat_a);
    vPortFree(mat_b);
    vPortFree(result);
    close(fd);
    vTaskDelete(NULL);
}

/* --------------------------------------------------------------- TCP server */

static void work_transport_server_task(void *arg)
{
    system_context_t *ctx = (system_context_t *)arg;

    int server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0) {
        ESP_LOGE(TAG, "server socket failed: %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(WORK_TRANSPORT_PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind failed: %d", errno);
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }

    if (listen(server_fd, 4) < 0) {
        ESP_LOGE(TAG, "listen failed: %d", errno);
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "listening on port %d", WORK_TRANSPORT_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        ESP_LOGI(TAG, "accepted connection from %s",
                 inet_ntoa(client_addr.sin_addr));

        work_hosting_args_t *args = (work_hosting_args_t *)pvPortMalloc(sizeof(*args));
        if (!args) {
            close(client_fd);
            continue;
        }
        args->ctx = ctx;
        args->fd  = client_fd;
        xTaskCreate(work_hosting_task, "whost", WORK_HOSTING_TASK_STACK,
                    args, 3, NULL);
    }
}

esp_err_t work_transport_server_start(system_context_t *ctx)
{
    BaseType_t r = xTaskCreate(work_transport_server_task, "wtrans_srv",
                               WORK_SERVER_TASK_STACK, ctx, 3, NULL);
    return (r == pdPASS) ? ESP_OK : ESP_FAIL;
}

/* ------------------------------------------------ async send queue + sender */

/* Item posted to the per-channel send queue. frame_buf is heap-allocated by
 * work_transport_enqueue_item and freed by work_sender_task after sending. */
typedef struct {
    uint8_t *frame_buf;
    int      frame_len;
} work_send_item_t;

typedef struct {
    system_context_t *ctx;
    QueueHandle_t queue;
    int           fd;
    int           channel_idx;
} work_sender_args_t;

static void work_sender_task(void *arg)
{
    work_sender_args_t *args = (work_sender_args_t *)arg;
    system_context_t *ctx = args->ctx;
    QueueHandle_t q = args->queue;
    int fd          = args->fd;
    int ch_idx      = args->channel_idx;
    vPortFree(args);

    work_send_item_t item;
    while (xQueueReceive(q, &item, portMAX_DELAY) == pdTRUE) {
        if (item.frame_buf == NULL) break; /* sentinel: teardown requested */
        if (send_exact(fd, item.frame_buf, item.frame_len) < 0) {
            ESP_LOGW(TAG, "sender failed ch=%d fd=%d errno=%d", ch_idx, fd, errno);
            vPortFree(item.frame_buf);
            delegation_handle_tcp_channel_lost(ctx, ch_idx, fd);
            break;
        }
        vPortFree(item.frame_buf);
    }

    if (ctx != NULL && ch_idx >= 0 && ch_idx < MAX_DELEGATION_CHANNELS &&
        ctx->channels[ch_idx].tcp_sender_task == xTaskGetCurrentTaskHandle()) {
        ctx->channels[ch_idx].tcp_sender_task = NULL;
    }
    vTaskDelete(NULL);
}

int work_transport_enqueue_item(QueueHandle_t q, uint32_t cycle_id, uint8_t block_id,
                                const int32_t *matrix_a, const int32_t *matrix_b)
{
    if (q == NULL) return -1;

    const int n             = WORK_FRAME_MATRIX_INTS;
    const int payload_bytes = (int)(sizeof(work_frame_hdr_t) + n * 4 * 2);
    uint8_t *buf = (uint8_t *)pvPortMalloc(payload_bytes);
    if (!buf) return -1;

    work_frame_hdr_t *hdr = (work_frame_hdr_t *)buf;
    hdr->magic    = WORK_TRANSPORT_MAGIC;
    hdr->type     = FRAME_WORK_ITEM;
    hdr->block_id = block_id;
    hdr->cycle_id = cycle_id;
    memcpy(buf + sizeof(*hdr),         matrix_a, n * 4);
    memcpy(buf + sizeof(*hdr) + n * 4, matrix_b, n * 4);

    work_send_item_t item = { .frame_buf = buf, .frame_len = payload_bytes };
    if (xQueueSend(q, &item, 0) != pdTRUE) {
        /* Queue full — drop this frame rather than blocking compute_task. */
        vPortFree(buf);
        return -1;
    }
    return 0;
}

void work_transport_channel_teardown(TaskHandle_t *sender_task,
                                     QueueHandle_t *send_queue,
                                     int *fd)
{
    if (*sender_task) {
        if (*sender_task != xTaskGetCurrentTaskHandle()) {
            vTaskDelete(*sender_task);
        }
        *sender_task = NULL;
    }
    if (*send_queue) {
        /* Drain any frames that were never sent and free their buffers. */
        work_send_item_t item;
        while (xQueueReceive(*send_queue, &item, 0) == pdTRUE) {
            if (item.frame_buf) vPortFree(item.frame_buf);
        }
        vQueueDelete(*send_queue);
        *send_queue = NULL;
    }
    work_transport_disconnect(*fd);
    *fd = -1;
}

/* --------------------------------------------------------------- TCP client */

typedef struct {
    system_context_t *ctx;
    int               fd;
    int               channel_idx;
} work_recv_args_t;

static void work_recv_task(void *arg)
{
    work_recv_args_t *args = (work_recv_args_t *)arg;
    system_context_t *ctx = args->ctx;
    int fd                = args->fd;
    int ch_idx            = args->channel_idx;
    vPortFree(args);

    const int n = WORK_FRAME_MATRIX_INTS;
    int32_t *result = (int32_t *)pvPortMalloc(n * sizeof(int32_t));
    if (!result) {
        ESP_LOGE(TAG, "recv_task: malloc failed");
        delegation_handle_tcp_channel_lost(ctx, ch_idx, fd);
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        work_frame_hdr_t hdr;
        if (recv_exact(fd, &hdr, sizeof(hdr)) <= 0) break;
        if (hdr.magic != WORK_TRANSPORT_MAGIC || hdr.type != FRAME_WORK_RESULT) break;
        if (recv_exact(fd, result, n * (int)sizeof(int32_t)) <= 0) break;

        delegation_handle_work_result_tcp(ctx, hdr.cycle_id, hdr.block_id,
                                          ch_idx, result);
    }

    delegation_handle_tcp_channel_lost(ctx, ch_idx, fd);

    vPortFree(result);
    vTaskDelete(NULL);
}

int work_transport_connect(system_context_t *ctx, const char *peer_ip,
                           int channel_idx)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        ESP_LOGE(TAG, "client socket failed: %d", errno);
        delegation_handle_tcp_channel_lost(ctx, channel_idx, -1);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(WORK_TRANSPORT_PORT);
    if (inet_pton(AF_INET, peer_ip, &addr.sin_addr) <= 0) {
        ESP_LOGE(TAG, "invalid peer IP: %s", peer_ip);
        close(fd);
        return -1;
    }

    /* connect() must complete before setting SO_SNDTIMEO — on ESP-IDF lwIP,
     * SO_SNDTIMEO also constrains connect(), causing it to fail with EAGAIN
     * on congested WiFi if set beforehand. */
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "connect to %s failed: %d", peer_ip, errno);
        close(fd);
        delegation_handle_tcp_channel_lost(ctx, channel_idx, -1);
        return -1;
    }

    /* Apply send timeout only after connection is established. */
    struct timeval tv;
    tv.tv_sec  = WORK_TRANSPORT_SEND_TIMEOUT_MS / 1000;
    tv.tv_usec = (WORK_TRANSPORT_SEND_TIMEOUT_MS % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    /* SO_RCVTIMEO deliberately not set: work_recv_task blocks waiting for
     * results and exits only when the peer closes the connection. */

    /* Create async send queue and spawn sender task. */
    QueueHandle_t send_q = xQueueCreate(WORK_SEND_QUEUE_DEPTH, sizeof(work_send_item_t));
    if (!send_q) {
        ESP_LOGE(TAG, "send queue alloc failed");
        close(fd);
        delegation_handle_tcp_channel_lost(ctx, channel_idx, -1);
        return -1;
    }
    ctx->channels[channel_idx].tcp_send_queue = send_q;

    work_sender_args_t *snd_args = (work_sender_args_t *)pvPortMalloc(sizeof(*snd_args));
    if (!snd_args) {
        vQueueDelete(send_q);
        ctx->channels[channel_idx].tcp_send_queue = NULL;
        close(fd);
        delegation_handle_tcp_channel_lost(ctx, channel_idx, -1);
        return -1;
    }
    snd_args->queue = send_q;
    snd_args->fd    = fd;
    snd_args->ctx   = ctx;
    snd_args->channel_idx = channel_idx;
    TaskHandle_t sender_handle = NULL;
    /* Priority 1 (below compute at 2): sender only runs during compute_task's
     * idle window, so WiFi TX processing does not inflate compute's exec_ticks. */
    if (xTaskCreate(work_sender_task, "wsend", WORK_SENDER_TASK_STACK,
                    snd_args, 1, &sender_handle) != pdPASS) {
        ESP_LOGE(TAG, "sender task create failed");
        vPortFree(snd_args);
        vQueueDelete(send_q);
        ctx->channels[channel_idx].tcp_send_queue = NULL;
        close(fd);
        delegation_handle_tcp_channel_lost(ctx, channel_idx, -1);
        return -1;
    }
    ctx->channels[channel_idx].tcp_sender_task = sender_handle;

    /* Spawn result receiver task. */
    work_recv_args_t *recv_args = (work_recv_args_t *)pvPortMalloc(sizeof(*recv_args));
    if (!recv_args) {
        /* sender task will exit when queue is deleted in teardown */
        work_transport_channel_teardown(&ctx->channels[channel_idx].tcp_sender_task,
                                        &ctx->channels[channel_idx].tcp_send_queue,
                                        &fd);
        delegation_handle_tcp_channel_lost(ctx, channel_idx, -1);
        return -1;
    }
    recv_args->ctx         = ctx;
    recv_args->fd          = fd;
    recv_args->channel_idx = channel_idx;
    if (xTaskCreate(work_recv_task, "wrecv", WORK_RECV_TASK_STACK,
                    recv_args, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "recv task create failed");
        vPortFree(recv_args);
        work_transport_channel_teardown(&ctx->channels[channel_idx].tcp_sender_task,
                                        &ctx->channels[channel_idx].tcp_send_queue,
                                        &fd);
        delegation_handle_tcp_channel_lost(ctx, channel_idx, -1);
        return -1;
    }

    ESP_LOGI(TAG, "connected to %s ch=%d fd=%d", peer_ip, channel_idx, fd);
    return fd;
}

void work_transport_disconnect(int fd)
{
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
}
