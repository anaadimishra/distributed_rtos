// Delegation state machine: offload, hosting, and work-item dispatch/execution.
#include "network/delegation.h"
#include "config/config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "deleg";

/* ------------------------------------------------------------------ helpers */

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void clear_pending_entry(pending_work_t *pw)
{
    if (pw == NULL) return;
    pw->cycle_id = 0;
    pw->block_id = 0;
    pw->channel_idx = 0xFF;
    pw->peer_id[0] = '\0';
    pw->sent_ms = 0;
    pw->in_flight = false;
}

static void reset_channel(delegation_channel_t *ch)
{
    ch->state    = CHAN_IDLE;
    ch->peer_id[0] = '\0';
    ch->blocks   = 0;
    ch->in_flight_count = 0;
    ch->start_ms = 0;
}

static uint32_t channel_reclaim_pending(system_context_t *ctx, int ch_idx)
{
    uint32_t reclaimed = 0;
    if (ctx == NULL || ch_idx < 0 || ch_idx >= MAX_DELEGATION_CHANNELS) {
        return 0;
    }

    delegation_channel_t *ch = &ctx->channels[ch_idx];
    for (int i = 0; i < MAX_PENDING_WORK; i++) {
        pending_work_t *pw = &ctx->pending_work[i];
        if (!pw->in_flight) continue;

        bool owned_by_channel = ((int)pw->channel_idx == ch_idx);
        bool peer_fallback = ((int)pw->channel_idx >= MAX_DELEGATION_CHANNELS) &&
            (ch->peer_id[0] != '\0') &&
            (strncmp(pw->peer_id, ch->peer_id, sizeof(pw->peer_id)) == 0);
        if (!owned_by_channel && !peer_fallback) continue;

        clear_pending_entry(pw);
        reclaimed++;
    }

    if (reclaimed > 0) {
        if (ctx->deleg_inflight_total >= reclaimed) {
            ctx->deleg_inflight_total -= reclaimed;
        } else {
            ctx->deleg_inflight_total = 0;
        }
    }
    return reclaimed;
}

static bool should_log_now(uint32_t *last_ms, uint32_t min_interval_ms)
{
    uint32_t now = now_ms();
    if ((uint32_t)(now - *last_ms) >= min_interval_ms) {
        *last_ms = now;
        return true;
    }
    return false;
}

static void publish_to(system_context_t *ctx,
                       const char *topic, const char *payload)
{
    if (ctx->mqtt_client == NULL) {
        return;
    }
    esp_mqtt_client_publish(ctx->mqtt_client, topic, payload, 0, 0, 0);
}

static void build_topic(char *out, size_t out_len,
                        const char *node_id, const char *suffix)
{
    snprintf(out, out_len, "cluster/%s/%s", node_id, suffix);
}

/* Minimal JSON string-field extraction — no heap, no cJSON dependency. */
static bool parse_str_field(const char *buf, const char *key,
                            char *out, size_t out_len)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(buf, search);
    if (!pos) return false;
    const char *colon = strchr(pos, ':');
    if (!colon) return false;
    const char *q1 = strchr(colon, '"');
    if (!q1) return false;
    q1++;
    const char *q2 = strchr(q1, '"');
    if (!q2) return false;
    size_t len = (size_t)(q2 - q1);
    if (len >= out_len) len = out_len - 1;
    memcpy(out, q1, len);
    out[len] = '\0';
    return true;
}

static bool parse_int_field(const char *buf, const char *key, int *out)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(buf, search);
    if (!pos) return false;
    const char *colon = strchr(pos, ':');
    if (!colon) return false;
    const char *num = colon + 1;
    while (*num == ' ' || *num == '\t') num++;
    char *end = NULL;
    long val = strtol(num, &end, 10);
    if (end == num) return false;
    *out = (int)val;
    return true;
}

/* Parse a JSON integer array field into out[], returning count of values read.
 * buf must be null-terminated. */
static int parse_int_array(const char *buf, const char *key,
                            int *out, int out_max)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(buf, search);
    if (!pos) return 0;
    const char *colon = strchr(pos + strlen(search), ':');
    if (!colon) return 0;
    const char *p = colon + 1;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '[') return 0;
    p++; /* skip '[' */

    int count = 0;
    while (*p && *p != ']' && count < out_max) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') p++;
        if (*p == ']' || *p == '\0') break;
        char *endptr = NULL;
        long val = strtol(p, &endptr, 10);
        if (endptr == p) break;
        out[count++] = (int)val;
        p = endptr;
    }
    return count;
}

/* Returns 1 if peer_id is in the peer table and has not timed out. */
static int peer_is_alive(system_context_t *ctx, const char *peer_id)
{
    uint32_t t = now_ms();
    for (int i = 0; i < MAX_PEERS; i++) {
        peer_state_t *p = &ctx->peers[i];
        if (!p->valid) continue;
        if (strncmp(p->node_id, peer_id, sizeof(p->node_id)) != 0) continue;
        return ((t - p->last_seen_ms) <= PEER_TIMEOUT_MS) ? 1 : 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ query API */

int delegation_active_channel_count(const system_context_t *ctx)
{
    if (ctx == NULL) return 0;
    int count = 0;
    for (int i = 0; i < MAX_DELEGATION_CHANNELS; i++) {
        if (ctx->channels[i].state == CHAN_ACTIVE) count++;
    }
    return count;
}

int delegation_total_delegated_blocks(const system_context_t *ctx)
{
    if (ctx == NULL) return 0;
    int total = 0;
    for (int i = 0; i < MAX_DELEGATION_CHANNELS; i++) {
        if (ctx->channels[i].state == CHAN_ACTIVE) {
            total += ctx->channels[i].blocks;
        }
    }
    return total;
}

const char *delegation_node_role_str(const system_context_t *ctx)
{
    if (ctx == NULL) return "IDLE";
    bool has_active     = false;
    bool has_requesting = false;
    bool has_hosting    = false;
    for (int i = 0; i < MAX_DELEGATION_CHANNELS; i++) {
        chan_state_t s = ctx->channels[i].state;
        if (s == CHAN_ACTIVE)     has_active     = true;
        if (s == CHAN_REQUESTING) has_requesting = true;
        if (s == CHAN_HOSTING)    has_hosting    = true;
    }
    if (has_active)     return "ACTIVE";
    if (has_requesting) return "REQUESTING";
    if (has_hosting)    return "HOSTING";
    return "IDLE";
}

const char *delegation_primary_peer(const system_context_t *ctx)
{
    if (ctx == NULL) return "";
    for (int i = 0; i < MAX_DELEGATION_CHANNELS; i++) {
        if (ctx->channels[i].state != CHAN_IDLE && ctx->channels[i].peer_id[0] != '\0') {
            return ctx->channels[i].peer_id;
        }
    }
    return "";
}

/* ------------------------------------------------------------------ public */

void delegation_tick(system_context_t *ctx)
{
    if (ctx == NULL) return;

    uint32_t t = now_ms();
    static uint32_t last_timeout_log_ms = 0;
    static uint32_t last_reclaim_log_ms = 0;

    /* Deterministic pending timeout reclaim (single linear scan). */
    for (int i = 0; i < MAX_PENDING_WORK; i++) {
        pending_work_t *pw = &ctx->pending_work[i];
        if (!pw->in_flight) continue;

        if ((uint32_t)(t - pw->sent_ms) <= DELEGATION_PENDING_TIMEOUT_MS) {
            continue;
        }

        if ((int)pw->channel_idx < MAX_DELEGATION_CHANNELS) {
            delegation_channel_t *ch = &ctx->channels[pw->channel_idx];
            if (ch->in_flight_count > 0) {
                ch->in_flight_count--;
            }
        }
        if (ctx->deleg_inflight_total > 0) {
            ctx->deleg_inflight_total--;
        }
        ctx->deleg_timeout_reclaim++;
        clear_pending_entry(pw);

#if DEBUG_LOGS
        if (should_log_now(&last_timeout_log_ms, 1000)) {
            ESP_LOGI(TAG, "pending timeout reclaim total=%u",
                     (unsigned)ctx->deleg_timeout_reclaim);
        }
#endif
    }

    for (int i = 0; i < MAX_DELEGATION_CHANNELS; i++) {
        delegation_channel_t *ch = &ctx->channels[i];

        switch (ch->state) {

            case CHAN_REQUESTING:
                if ((t - (uint32_t)ch->start_ms) > DELEGATION_TIMEOUT_MS) {
#if DEBUG_LOGS
                    ESP_LOGI(TAG, "request timed out peer=%s", ch->peer_id);
#endif
                    reset_channel(ch);
                }
                break;

            case CHAN_ACTIVE:
                if (!peer_is_alive(ctx, ch->peer_id)) {
                    uint32_t reclaimed = channel_reclaim_pending(ctx, i);
#if DEBUG_LOGS
                    ESP_LOGI(TAG, "host lost, restoring blocks=%d peer=%s",
                             ch->blocks, ch->peer_id);
                    if (reclaimed > 0 && should_log_now(&last_reclaim_log_ms, 1000)) {
                        ESP_LOGI(TAG, "channel reset reclaim peer=%s reclaimed=%u",
                                 ch->peer_id, (unsigned)reclaimed);
                    }
#endif
                    ctx->active_blocks += (uint32_t)ch->blocks;
                    if (ctx->active_blocks > PROCESSING_BLOCKS) {
                        ctx->active_blocks = PROCESSING_BLOCKS;
                    }
                    reset_channel(ch);
                }
                break;

            case CHAN_HOSTING:
                if (!peer_is_alive(ctx, ch->peer_id)) {
                    uint32_t reclaimed = channel_reclaim_pending(ctx, i);
#if DEBUG_LOGS
                    ESP_LOGI(TAG, "delegator lost, stopping hosting peer=%s", ch->peer_id);
                    if (reclaimed > 0 && should_log_now(&last_reclaim_log_ms, 1000)) {
                        ESP_LOGI(TAG, "channel reset reclaim peer=%s reclaimed=%u",
                                 ch->peer_id, (unsigned)reclaimed);
                    }
#endif
                    reset_channel(ch);
                }
                break;

            default:
                break;
        }
    }
}

void delegation_try_offload(system_context_t *ctx)
{
    if (ctx == NULL || ctx->mqtt_client == NULL) return;

    /* A node that is currently HOSTING for another must not re-delegate its own
     * load — that would cascade hosting CPU pressure to other nodes and create
     * an unbounded delegation loop across the cluster. */
    for (int i = 0; i < MAX_DELEGATION_CHANNELS; i++) {
        if (ctx->channels[i].state == CHAN_HOSTING) {
            return;
        }
    }

    /* Open a channel to EVERY reachable STRESS_LOW peer that does not already
     * have a channel open. We do NOT break after the first — multi-peer is the
     * entire point of this function. */

    if (xSemaphoreTake(ctx->peers_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }

    uint32_t t = now_ms();

    /* Collect eligible peers (STRESS_LOW, alive, no channel open yet). */
    char candidates[MAX_PEERS][16];
    int  n_candidates = 0;

    for (int i = 0; i < MAX_PEERS && n_candidates < MAX_PEERS; i++) {
        peer_state_t *p = &ctx->peers[i];
        if (!p->valid) continue;
        if ((t - p->last_seen_ms) > PEER_TIMEOUT_MS) continue;
        if (p->stress_level != STRESS_LOW) continue;

        /* Skip if we already have a channel open to this peer. */
        bool already_open = false;
        for (int j = 0; j < MAX_DELEGATION_CHANNELS; j++) {
            delegation_channel_t *ch = &ctx->channels[j];
            if (ch->state != CHAN_IDLE &&
                strncmp(ch->peer_id, p->node_id, sizeof(ch->peer_id)) == 0) {
                already_open = true;
                break;
            }
        }
        if (already_open) continue;

        snprintf(candidates[n_candidates], sizeof(candidates[0]), "%s", p->node_id);
        n_candidates++;
    }

    xSemaphoreGive(ctx->peers_mutex);

    if (n_candidates == 0) return;

    /* Compute per-channel block count: split active_blocks equally, minimum 1. */
    int blocks = (int)(ctx->active_blocks / 2);
    if (blocks < 1) blocks = 1;
    /* Distribute evenly across all candidates we can open channels for. */
    int per_peer = blocks / n_candidates;
    if (per_peer < 1) per_peer = 1;

    for (int c = 0; c < n_candidates; c++) {
        /* Find a free IDLE channel slot. */
        int slot = -1;
        for (int j = 0; j < MAX_DELEGATION_CHANNELS; j++) {
            if (ctx->channels[j].state == CHAN_IDLE) { slot = j; break; }
        }
        if (slot < 0) break; /* no more channel slots available */

        delegation_channel_t *ch = &ctx->channels[slot];
        ch->state    = CHAN_REQUESTING;
        ch->blocks   = per_peer;
        ch->start_ms = (int64_t)t;
        snprintf(ch->peer_id, sizeof(ch->peer_id), "%s", candidates[c]);

        char topic[80];
        build_topic(topic, sizeof(topic), candidates[c], "delegate_request");

        char payload[128];
        snprintf(payload, sizeof(payload),
                 "{\"action\":\"DELEGATE_REQUEST\",\"from\":\"%s\",\"blocks\":%d}",
                 ctx->node_id, per_peer);

        publish_to(ctx, topic, payload);
#if DEBUG_LOGS
        ESP_LOGI(TAG, "offload request -> %s blocks=%d slot=%d",
                 candidates[c], per_peer, slot);
#endif
    }
}

void delegation_handle_request(system_context_t *ctx,
                               const char *data, int data_len)
{
    if (ctx == NULL || data == NULL || data_len <= 0) return;

    if (data_len > 255) data_len = 255;
    char buf[256];
    memcpy(buf, data, data_len);
    buf[data_len] = '\0';

    char from[16] = {0};
    int  blocks   = 0;
    if (!parse_str_field(buf, "from", from, sizeof(from))) return;
    if (!parse_int_field(buf, "blocks", &blocks) || blocks <= 0) return;

    char reply_topic[80];
    build_topic(reply_topic, sizeof(reply_topic), from, "delegate_reply");

    /* Reject if CPU headroom is too low. */
    if (ctx->cpu_usage >= DELEGATION_MIN_HEADROOM) {
        char reject[128];
        snprintf(reject, sizeof(reject),
                 "{\"action\":\"DELEGATE_REJECT\",\"from\":\"%s\"}",
                 ctx->node_id);
        publish_to(ctx, reply_topic, reject);
#if DEBUG_LOGS
        ESP_LOGI(TAG, "reject request from=%s cpu=%u (headroom low)",
                 from, (unsigned)ctx->cpu_usage);
#endif
        return;
    }

    /* Find a free IDLE channel slot to host this peer. */
    int slot = -1;
    for (int i = 0; i < MAX_DELEGATION_CHANNELS; i++) {
        if (ctx->channels[i].state == CHAN_IDLE) { slot = i; break; }
    }
    if (slot < 0) {
        /* All channel slots busy — reject. */
        char reject[128];
        snprintf(reject, sizeof(reject),
                 "{\"action\":\"DELEGATE_REJECT\",\"from\":\"%s\"}",
                 ctx->node_id);
        publish_to(ctx, reply_topic, reject);
#if DEBUG_LOGS
        ESP_LOGI(TAG, "reject request from=%s (no free slot)", from);
#endif
        return;
    }

    /* Accept: claim the slot as HOSTING. */
    delegation_channel_t *ch = &ctx->channels[slot];
    ch->state    = CHAN_HOSTING;
    ch->blocks   = blocks;
    ch->start_ms = (int64_t)now_ms();
    snprintf(ch->peer_id, sizeof(ch->peer_id), "%s", from);

    char accept[128];
    snprintf(accept, sizeof(accept),
             "{\"action\":\"DELEGATE_ACCEPT\",\"from\":\"%s\",\"blocks\":%d}",
             ctx->node_id, blocks);
    publish_to(ctx, reply_topic, accept);
#if DEBUG_LOGS
    ESP_LOGI(TAG, "accept request from=%s blocks=%d slot=%d cpu=%u",
             from, blocks, slot, (unsigned)ctx->cpu_usage);
#endif
}

/* ---------------------------------------------------- work item dispatch */

delegation_dispatch_result_t delegation_dispatch_work_item(system_context_t *ctx,
                                                           int block_id,
                                                           const int *matrix_a,
                                                           const int *matrix_b)
{
    if (ctx == NULL || ctx->mqtt_client == NULL ||
        matrix_a == NULL || matrix_b == NULL) {
        return DISPATCH_ERROR;
    }

    /* Find a free pending slot first. If none available, this cycle is BUSY. */
    int slot = -1;
    for (int i = 0; i < MAX_PENDING_WORK; i++) {
        if (!ctx->pending_work[i].in_flight) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return DISPATCH_BUSY;
    }

    /* Round-robin across ACTIVE channels under per-channel in-flight cap. */
    static int rr_index = 0;
    static uint32_t last_cap_log_ms = 0;
    char target[16] = {0};
    int eligible_slots[MAX_DELEGATION_CHANNELS];
    int eligible_count = 0;
    bool had_active_channels = false;

    for (int i = 0; i < MAX_DELEGATION_CHANNELS; i++) {
        if (ctx->channels[i].state != CHAN_ACTIVE) continue;
        had_active_channels = true;
        if (ctx->channels[i].in_flight_count < DELEGATION_MAX_INFLIGHT_PER_CHANNEL) {
            eligible_slots[eligible_count++] = i;
        }
    }
    if (eligible_count == 0) {
#if DEBUG_LOGS
        if (had_active_channels && should_log_now(&last_cap_log_ms, 1000)) {
            ESP_LOGI(TAG, "channel cap-hit: active=%d cap=%d inflight_total=%u",
                     delegation_active_channel_count(ctx),
                     DELEGATION_MAX_INFLIGHT_PER_CHANNEL,
                     (unsigned)ctx->deleg_inflight_total);
        }
#endif
        return DISPATCH_BUSY;
    }

    rr_index = rr_index % eligible_count;
    int ch_idx = eligible_slots[rr_index];
    snprintf(target, sizeof(target), "%s", ctx->channels[ch_idx].peer_id);
    rr_index = (rr_index + 1) % eligible_count;
    if (target[0] == '\0') {
        return DISPATCH_ERROR;
    }

    /* Build JSON payload with both full matrices.
     * Allocate on heap — payload is ~16 KB, far too large for any task stack. */
    const int BUF = 32768;
    char *payload = (char *)pvPortMalloc(BUF);
    if (payload == NULL) return DISPATCH_ERROR;

    int n = MATRIX_SIZE * MATRIX_SIZE;
    int pos = 0;
    pos += snprintf(payload + pos, BUF - pos,
                    "{\"from\":\"%s\",\"cycle_id\":%u,\"block_id\":%d,\"matrix_a\":[",
                    ctx->node_id, (unsigned)ctx->compute_cycle_id, block_id);

    for (int i = 0; i < n && pos < BUF - 16; i++) {
        pos += snprintf(payload + pos, BUF - pos,
                        i == 0 ? "%d" : ",%d", matrix_a[i]);
    }
    pos += snprintf(payload + pos, BUF - pos, "],\"matrix_b\":[");
    for (int i = 0; i < n && pos < BUF - 16; i++) {
        pos += snprintf(payload + pos, BUF - pos,
                        i == 0 ? "%d" : ",%d", matrix_b[i]);
    }
    pos += snprintf(payload + pos, BUF - pos, "]}");

    char topic[80];
    build_topic(topic, sizeof(topic), target, "work_item");
    int pub_id = esp_mqtt_client_publish(ctx->mqtt_client, topic, payload, pos, 0, 0);
    vPortFree(payload);
    if (pub_id < 0) {
        return DISPATCH_ERROR;
    }

    /* Record pending entry. */
    ctx->pending_work[slot].cycle_id  = ctx->compute_cycle_id;
    ctx->pending_work[slot].block_id  = (uint8_t)block_id;
    ctx->pending_work[slot].channel_idx = (uint8_t)ch_idx;
    ctx->pending_work[slot].in_flight = true;
    ctx->pending_work[slot].sent_ms = now_ms();
    snprintf(ctx->pending_work[slot].peer_id,
             sizeof(ctx->pending_work[slot].peer_id), "%s", target);
    ctx->channels[ch_idx].in_flight_count++;
    ctx->deleg_inflight_total++;
    ctx->deleg_blocks_dispatched++;

#if DEBUG_LOGS
    ESP_LOGI(TAG, "dispatch block=%d cycle=%u -> %s", block_id,
             (unsigned)ctx->compute_cycle_id, target);
#endif
    return DISPATCH_OK;
}

void delegation_handle_work_item(system_context_t *ctx,
                                 const char *data, int data_len)
{
    if (ctx == NULL || data == NULL || data_len <= 0) return;

    /* Null-terminate a heap copy of the message. */
    char *buf = (char *)pvPortMalloc(data_len + 1);
    if (buf == NULL) return;
    memcpy(buf, data, data_len);
    buf[data_len] = '\0';

    char from[16] = {0};
    int  cycle_id = 0, block_id = 0;
    if (!parse_str_field(buf, "from", from, sizeof(from)) ||
        !parse_int_field(buf, "cycle_id", &cycle_id) ||
        !parse_int_field(buf, "block_id", &block_id)) {
        vPortFree(buf);
        return;
    }

    /* Only execute if we have a HOSTING channel for this sender. */
    bool hosting = false;
    for (int i = 0; i < MAX_DELEGATION_CHANNELS; i++) {
        delegation_channel_t *ch = &ctx->channels[i];
        if (ch->state == CHAN_HOSTING &&
            strncmp(ch->peer_id, from, sizeof(ch->peer_id)) == 0) {
            hosting = true;
            break;
        }
    }
    if (!hosting) {
        vPortFree(buf);
        return;
    }

    const int n = MATRIX_SIZE * MATRIX_SIZE;
    int *mat_a  = (int *)pvPortMalloc(n * sizeof(int));
    int *mat_b  = (int *)pvPortMalloc(n * sizeof(int));
    int *result = (int *)pvPortMalloc(n * sizeof(int));

    if (!mat_a || !mat_b || !result) {
        vPortFree(mat_a); vPortFree(mat_b); vPortFree(result);
        vPortFree(buf);
        return;
    }

    int a_cnt = parse_int_array(buf, "matrix_a", mat_a, n);
    int b_cnt = parse_int_array(buf, "matrix_b", mat_b, n);
    vPortFree(buf);

    if (a_cnt != n || b_cnt != n) {
        vPortFree(mat_a); vPortFree(mat_b); vPortFree(result);
        return;
    }

    /* Compute C = A × B (row-major flat storage). */
    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            int sum = 0;
            for (int k = 0; k < MATRIX_SIZE; k++) {
                sum += mat_a[i * MATRIX_SIZE + k] * mat_b[k * MATRIX_SIZE + j];
            }
            result[i * MATRIX_SIZE + j] = sum;
        }
    }
    vPortFree(mat_a);
    vPortFree(mat_b);

    /* Build and publish result JSON. */
    const int BUF = 16384;
    char *payload = (char *)pvPortMalloc(BUF);
    if (payload == NULL) { vPortFree(result); return; }

    int pos = 0;
    pos += snprintf(payload + pos, BUF - pos,
                    "{\"cycle_id\":%d,\"block_id\":%d,\"result\":[",
                    cycle_id, block_id);
    for (int i = 0; i < n && pos < BUF - 16; i++) {
        pos += snprintf(payload + pos, BUF - pos,
                        i == 0 ? "%d" : ",%d", result[i]);
    }
    pos += snprintf(payload + pos, BUF - pos, "]}");
    vPortFree(result);

    char topic[80];
    build_topic(topic, sizeof(topic), from, "work_result");
    esp_mqtt_client_publish(ctx->mqtt_client, topic, payload, pos, 0, 0);
    vPortFree(payload);

#if DEBUG_LOGS
    ESP_LOGI(TAG, "work_item done cycle=%d block=%d -> %s", cycle_id, block_id, from);
#endif
}

void delegation_handle_work_result(system_context_t *ctx,
                                   const char *data, int data_len)
{
    if (ctx == NULL || data == NULL || data_len <= 0) return;

    char *buf = (char *)pvPortMalloc(data_len + 1);
    if (buf == NULL) return;
    memcpy(buf, data, data_len);
    buf[data_len] = '\0';

    int cycle_id = 0, block_id = 0;
    bool ok = parse_int_field(buf, "cycle_id", &cycle_id) &&
              parse_int_field(buf, "block_id", &block_id);

    /* Parse and verify result count (confirms full matrix was received). */
    const int n = MATRIX_SIZE * MATRIX_SIZE;
    int *result = NULL;
    if (ok) {
        result = (int *)pvPortMalloc(n * sizeof(int));
        if (result) {
            int r_cnt = parse_int_array(buf, "result", result, n);
            if (r_cnt != n) ok = false;
        } else {
            ok = false;
        }
    }
    vPortFree(buf);

    if (!ok) { vPortFree(result); return; }

    /* Find matching pending slot by cycle_id + block_id. */
    for (int i = 0; i < MAX_PENDING_WORK; i++) {
        pending_work_t *pw = &ctx->pending_work[i];
        if (!pw->in_flight) continue;
        if ((int)pw->cycle_id == cycle_id && (int)pw->block_id == block_id) {
            if ((int)pw->channel_idx < MAX_DELEGATION_CHANNELS) {
                delegation_channel_t *ch = &ctx->channels[pw->channel_idx];
                if (ch->in_flight_count > 0) {
                    ch->in_flight_count--;
                }
            }
            if (ctx->deleg_inflight_total > 0) {
                ctx->deleg_inflight_total--;
            }
            clear_pending_entry(pw);
            ctx->deleg_blocks_returned++;
#if DEBUG_LOGS
            ESP_LOGI(TAG, "result rx cycle=%d block=%d returned=%u",
                     cycle_id, block_id, (unsigned)ctx->deleg_blocks_returned);
#endif
            break;
        }
    }
    vPortFree(result);
}

/* ----------------------------------------------------------- reply handler */

void delegation_handle_reply(system_context_t *ctx,
                             const char *data, int data_len)
{
    if (ctx == NULL || data == NULL || data_len <= 0) return;

    if (data_len > 255) data_len = 255;
    char buf[256];
    memcpy(buf, data, data_len);
    buf[data_len] = '\0';

    char action[32] = {0};
    char from[16]   = {0};
    if (!parse_str_field(buf, "action", action, sizeof(action))) return;
    parse_str_field(buf, "from", from, sizeof(from)); /* best-effort */

    /* Find the REQUESTING channel for this peer. */
    int slot = -1;
    for (int i = 0; i < MAX_DELEGATION_CHANNELS; i++) {
        delegation_channel_t *ch = &ctx->channels[i];
        if (ch->state == CHAN_REQUESTING &&
            (from[0] == '\0' ||
             strncmp(ch->peer_id, from, sizeof(ch->peer_id)) == 0)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return; /* no matching REQUESTING channel */

    delegation_channel_t *ch = &ctx->channels[slot];

    if (strncmp(action, "DELEGATE_ACCEPT", sizeof(action)) == 0) {
        /* Reduce our own blocks now the host is carrying the offloaded portion. */
        if (ctx->active_blocks >= (uint32_t)ch->blocks) {
            ctx->active_blocks -= (uint32_t)ch->blocks;
        } else {
            ctx->active_blocks = 0;
        }
        ch->state = CHAN_ACTIVE;
#if DEBUG_LOGS
        ESP_LOGI(TAG, "offload accepted by=%s blocks=%d active_blocks=%u",
                 from, ch->blocks, (unsigned)ctx->active_blocks);
#endif
    } else {
        /* DELEGATE_REJECT or unrecognised — return channel to idle cleanly. */
#if DEBUG_LOGS
        ESP_LOGI(TAG, "offload rejected action=%s from=%s", action, from);
#endif
        reset_channel(ch);
    }
}
