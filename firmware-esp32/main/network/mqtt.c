// MQTT client setup, telemetry publish, and control message handling.
#include "network/mqtt.h"
#include "network/delegation.h"
#include "config/config.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static bool ends_with(const char *s, int slen, const char *suffix) {
    int n = (int)strlen(suffix);
    return slen >= n && strncmp(s + slen - n, suffix, n) == 0;
}

static const char *MQTT_TAG = "mqtt";
static const char *TELEMETRY_PREFIX = "cluster/";
static const char *TELEMETRY_SUFFIX = "/telemetry";

typedef enum {
    CONTROL_ACTION_NONE = 0,
    CONTROL_ACTION_SET_LOAD,
    CONTROL_ACTION_SET_BLOCKS,
    CONTROL_ACTION_SYNC_TIME,
    CONTROL_ACTION_REBOOT,
    CONTROL_ACTION_POWEROFF,
    CONTROL_ACTION_FAIL_SILENT_ON,
    CONTROL_ACTION_FAIL_SILENT_OFF,
} control_action_t;

static bool parse_stress_level(const char *data, int len, uint8_t *out_stress)
{
    if (data == NULL || out_stress == NULL || len <= 0) {
        return false;
    }

    if (len > 255) {
        len = 255;
    }

    char buf[256];
    memcpy(buf, data, len);
    buf[len] = '\0';

    char *key = strstr(buf, "\"stress_level\"");
    if (key == NULL) {
        return false;
    }
    char *colon = strchr(key, ':');
    if (colon == NULL) {
        return false;
    }
    char *num_start = colon + 1;
    while (*num_start == ' ' || *num_start == '\t') {
        num_start++;
    }

    char *endptr = NULL;
    long level = strtol(num_start, &endptr, 10);
    if (endptr == num_start || level < 0 || level > 2) {
        return false;
    }

    *out_stress = (uint8_t)level;
    return true;
}

/* Parse "ip" string field from telemetry JSON into out (up to out_len-1 chars). */
static void parse_ip_field(const char *data, int len, char *out, size_t out_len)
{
    out[0] = '\0';
    if (data == NULL || len <= 0) return;

    /* "ip" is always within the first ~50 bytes of telemetry JSON — scan only
     * the first 128 bytes so payload size never triggers a false early return. */
    int scan_len = len < 128 ? len : 128;
    char buf[129];
    memcpy(buf, data, scan_len);
    buf[scan_len] = '\0';

    char *key = strstr(buf, "\"ip\"");
    if (key == NULL) return;
    char *colon = strchr(key + 4, ':');
    if (colon == NULL) return;
    char *p = colon + 1;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return;
    p++; /* skip opening quote */
    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
}

static bool parse_telemetry_topic_node(const char *topic, int topic_len, char *out_node_id, size_t out_len)
{
    if (topic == NULL || topic_len <= 0 || out_node_id == NULL || out_len == 0) {
        return false;
    }
    if (topic_len > 127) {
        return false;
    }

    char topic_buf[128];
    memcpy(topic_buf, topic, topic_len);
    topic_buf[topic_len] = '\0';

    size_t prefix_len = strlen(TELEMETRY_PREFIX);
    size_t suffix_len = strlen(TELEMETRY_SUFFIX);
    if (strlen(topic_buf) <= prefix_len + suffix_len) {
        return false;
    }
    if (strncmp(topic_buf, TELEMETRY_PREFIX, prefix_len) != 0) {
        return false;
    }

    char *suffix_pos = strstr(topic_buf + prefix_len, TELEMETRY_SUFFIX);
    if (suffix_pos == NULL) {
        return false;
    }

    size_t node_len = (size_t)(suffix_pos - (topic_buf + prefix_len));
    if (node_len == 0 || node_len >= out_len) {
        return false;
    }

    memcpy(out_node_id, topic_buf + prefix_len, node_len);
    out_node_id[node_len] = '\0';
    return true;
}

static void update_peer_state(system_context_t *ctx, const char *node_id,
                              uint8_t stress_level, const char *ip_addr)
{
    if (ctx == NULL || node_id == NULL || node_id[0] == '\0') {
        return;
    }

    if (xSemaphoreTake(ctx->peers_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    int free_idx = -1;
    int replace_idx = -1;
    uint32_t oldest_seen = UINT32_MAX;

    for (int i = 0; i < MAX_PEERS; i++) {
        peer_state_t *peer = &ctx->peers[i];
        if (peer->valid) {
            if (strncmp(peer->node_id, node_id, sizeof(peer->node_id)) == 0) {
                peer->stress_level = stress_level;
                peer->last_seen_ms = now_ms;
                if (ip_addr && ip_addr[0] != '\0') {
                    strncpy(peer->ip_addr, ip_addr, sizeof(peer->ip_addr) - 1);
                    peer->ip_addr[sizeof(peer->ip_addr) - 1] = '\0';
                }
                xSemaphoreGive(ctx->peers_mutex);
                return;
            }
            if (peer->last_seen_ms < oldest_seen) {
                oldest_seen = peer->last_seen_ms;
                replace_idx = i;
            }
        } else if (free_idx < 0) {
            free_idx = i;
        }
    }

    int idx = (free_idx >= 0) ? free_idx : replace_idx;
    if (idx < 0) {
        xSemaphoreGive(ctx->peers_mutex);
        return;
    }

    peer_state_t *slot = &ctx->peers[idx];
    snprintf(slot->node_id, sizeof(slot->node_id), "%s", node_id);
    slot->stress_level = stress_level;
    slot->last_seen_ms = now_ms;
    slot->valid = 1;
    if (ip_addr && ip_addr[0] != '\0') {
        strncpy(slot->ip_addr, ip_addr, sizeof(slot->ip_addr) - 1);
        slot->ip_addr[sizeof(slot->ip_addr) - 1] = '\0';
    }

    xSemaphoreGive(ctx->peers_mutex);
}

// Replace "{node_id}" in a topic template with the actual node ID.
static void format_topic(char *out, size_t out_len, const char *tpl, const char *node_id)
{
    if (out == NULL || out_len == 0 || tpl == NULL || node_id == NULL) {
        return;
    }

    const char *needle = "{node_id}";
    const char *pos = strstr(tpl, needle);
    if (pos == NULL) {
        snprintf(out, out_len, "%s", tpl);
        return;
    }

    size_t prefix_len = (size_t)(pos - tpl);
    size_t needle_len = strlen(needle);
    const char *suffix = pos + needle_len;

    snprintf(out, out_len, "%.*s%s%s", (int)prefix_len, tpl, node_id, suffix);
}

// Parse control payloads such as:
// {"action":"SET_LOAD","value":123} or {"action":"FAIL_SILENT_ON"}.
static bool parse_control_message(const char *data,
                                  int len,
                                  int64_t *out_value,
                                  control_action_t *out_action,
                                  uint32_t *out_seq)
{
    if (data == NULL || out_value == NULL || out_action == NULL || out_seq == NULL || len <= 0) {
        return false;
    }

    if (len > 255) {
        len = 255;
    }

    char buf[256];
    memcpy(buf, data, len);
    buf[len] = '\0';

    if (strstr(buf, "\"action\"") == NULL) {
        return false;
    }

    bool is_set_load = strstr(buf, "SET_LOAD") != NULL;
    bool is_set_blocks = strstr(buf, "SET_BLOCKS") != NULL;
    bool is_sync_time = strstr(buf, "SYNC_TIME") != NULL;
    bool is_reboot = strstr(buf, "REBOOT") != NULL;
    bool is_poweroff = strstr(buf, "POWEROFF") != NULL;
    bool is_fail_silent_on = strstr(buf, "FAIL_SILENT_ON") != NULL;
    bool is_fail_silent_off = strstr(buf, "FAIL_SILENT_OFF") != NULL;
    if (!is_set_load && !is_set_blocks && !is_sync_time &&
        !is_reboot && !is_poweroff && !is_fail_silent_on && !is_fail_silent_off) {
        return false;
    }

    *out_value = 0;
    if (is_set_blocks) {
        *out_action = CONTROL_ACTION_SET_BLOCKS;
    } else if (is_set_load) {
        *out_action = CONTROL_ACTION_SET_LOAD;
    } else if (is_sync_time) {
        *out_action = CONTROL_ACTION_SYNC_TIME;
    } else if (is_reboot) {
        *out_action = CONTROL_ACTION_REBOOT;
    } else if (is_fail_silent_on) {
        *out_action = CONTROL_ACTION_FAIL_SILENT_ON;
    } else if (is_fail_silent_off) {
        *out_action = CONTROL_ACTION_FAIL_SILENT_OFF;
    } else {
        *out_action = CONTROL_ACTION_POWEROFF;
    }

    if (*out_action == CONTROL_ACTION_SET_LOAD ||
        *out_action == CONTROL_ACTION_SET_BLOCKS ||
        *out_action == CONTROL_ACTION_SYNC_TIME) {
        char *value_key = strstr(buf, "\"value\"");
        if (value_key == NULL) {
            return false;
        }

        char *colon = strchr(value_key, ':');
        if (colon == NULL) {
            return false;
        }

        char *num_start = colon + 1;
        while (*num_start == ' ' || *num_start == '\t') {
            num_start++;
        }

        char *endptr = NULL;
        long long value = strtoll(num_start, &endptr, 10);
        if (endptr == num_start || value < 0) {
            return false;
        }
        *out_value = (int64_t)value;
    }

    // Optional sequence number for control latency tracking.
    *out_seq = 0;
    char *seq_key = strstr(buf, "\"seq\"");
    if (seq_key != NULL) {
        char *seq_colon = strchr(seq_key, ':');
        if (seq_colon != NULL) {
            char *seq_start = seq_colon + 1;
            while (*seq_start == ' ' || *seq_start == '\t') {
                seq_start++;
            }
            char *seq_end = NULL;
            long seq_val = strtol(seq_start, &seq_end, 10);
            if (seq_end != seq_start && seq_val >= 0) {
                *out_seq = (uint32_t)seq_val;
            }
        }
    }
    return true;
}

// MQTT event handler: subscribe on connect, parse control messages on data.
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    system_context_t *ctx = (system_context_t *)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    if (event == NULL) {
        return;
    }

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            // Subscribe to control messages for this node.
            esp_mqtt_client_subscribe(event->client, ctx->control_topic, 0);
            // Subscribe to cluster telemetry to observe peer stress levels.
            esp_mqtt_client_subscribe(event->client, "cluster/+/telemetry", 0);
            {
                char dreq[80], drep[80], witem[80], wres[80];
                snprintf(dreq,  sizeof(dreq),  "cluster/%s/delegate_request", ctx->node_id);
                snprintf(drep,  sizeof(drep),  "cluster/%s/delegate_reply",   ctx->node_id);
                snprintf(witem, sizeof(witem), "cluster/%s/work_item",         ctx->node_id);
                snprintf(wres,  sizeof(wres),  "cluster/%s/work_result",       ctx->node_id);
                esp_mqtt_client_subscribe(event->client, dreq,  0);
                esp_mqtt_client_subscribe(event->client, drep,  0);
                esp_mqtt_client_subscribe(event->client, witem, 0);
                esp_mqtt_client_subscribe(event->client, wres,  0);
            }
            break;
        case MQTT_EVENT_DATA: {
            // Receive peer telemetry and update fixed peer table.
            char peer_node_id[16];
            if (ctx != NULL &&
                parse_telemetry_topic_node(event->topic, event->topic_len, peer_node_id, sizeof(peer_node_id)) &&
                strncmp(peer_node_id, ctx->node_id, sizeof(peer_node_id)) != 0) {
                uint8_t peer_stress = 0;
                if (parse_stress_level(event->data, event->data_len, &peer_stress)) {
                    char peer_ip[16] = {0};
                    parse_ip_field(event->data, event->data_len, peer_ip, sizeof(peer_ip));
                    update_peer_state(ctx, peer_node_id, peer_stress, peer_ip);
                }
            }

            // Delegation handshake topics.
            if (ends_with(event->topic, event->topic_len, "/delegate_request")) {
                delegation_handle_request(ctx, event->data, event->data_len);
            } else if (ends_with(event->topic, event->topic_len, "/delegate_reply")) {
                delegation_handle_reply(ctx, event->data, event->data_len);
            }
            /* work_item and work_result now travel over TCP — no MQTT routing needed. */

            // Only handle messages on this node's control topic.
            if (event->topic_len == (int)strlen(ctx->control_topic) &&
                strncmp(event->topic, ctx->control_topic, event->topic_len) == 0) {
#if DEBUG_LOGS
                ESP_LOGI(MQTT_TAG, "control rx: topic=%.*s payload=%.*s",
                         event->topic_len, event->topic,
                         event->data_len, event->data);
#endif
                int64_t new_value = 0;
                control_action_t action = CONTROL_ACTION_NONE;
                uint32_t seq = 0;
                if (parse_control_message(event->data, event->data_len, &new_value, &action, &seq)) {
                    if (ctx != NULL && action == CONTROL_ACTION_SET_LOAD) {
                        uint32_t clamped = (new_value < 0) ? 0 : (uint32_t)new_value;
                        if (clamped < LOAD_FACTOR_MIN) {
                            clamped = LOAD_FACTOR_MIN;
                        }
                        if (clamped > LOAD_FACTOR_MAX) {
                            clamped = LOAD_FACTOR_MAX;
                        }
                        ctx->load_factor = clamped;
                        ctx->last_ctrl_seq = seq;
#if DEBUG_LOGS
                        ESP_LOGI(MQTT_TAG, "control: SET_LOAD=%u seq=%u", (unsigned)clamped, (unsigned)seq);
#endif
                    }
                    if (ctx != NULL && action == CONTROL_ACTION_SET_BLOCKS) {
                        uint32_t clamped = (new_value < 0) ? 0 : (uint32_t)new_value;
                        if (clamped > PROCESSING_BLOCKS) {
                            clamped = PROCESSING_BLOCKS;
                        }
                        ctx->active_blocks = clamped;
                        ctx->last_ctrl_seq = seq;
#if DEBUG_LOGS
                        ESP_LOGI(MQTT_TAG, "control: SET_BLOCKS=%u seq=%u", (unsigned)clamped, (unsigned)seq);
#endif
                    }
                    if (ctx != NULL && action == CONTROL_ACTION_SYNC_TIME) {
                        // new_value is epoch ms from the dashboard.
                        int64_t now_ms = (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                        ctx->time_offset_ms = new_value - now_ms;
                        ctx->time_sync_ready = 1;
#if DEBUG_LOGS
                        ESP_LOGI(MQTT_TAG, "control: SYNC_TIME epoch_ms=%lld seq=%u",
                                 (long long)new_value, (unsigned)seq);
#endif
                    }
                    if (action == CONTROL_ACTION_REBOOT) {
#if DEBUG_LOGS
                        ESP_LOGI(MQTT_TAG, "control: REBOOT seq=%u", (unsigned)seq);
#endif
                        vTaskDelay(pdMS_TO_TICKS(100));
                        esp_restart();
                    }
                    if (action == CONTROL_ACTION_POWEROFF) {
#if DEBUG_LOGS
                        ESP_LOGI(MQTT_TAG, "control: POWEROFF seq=%u", (unsigned)seq);
#endif
                        vTaskDelay(pdMS_TO_TICKS(100));
                        esp_deep_sleep_start();
                    }
                    if (ctx != NULL && action == CONTROL_ACTION_FAIL_SILENT_ON) {
                        ctx->telemetry_suppressed = 1;
                        ctx->last_ctrl_seq = seq;
#if DEBUG_LOGS
                        ESP_LOGI(MQTT_TAG, "control: FAIL_SILENT_ON seq=%u", (unsigned)seq);
#endif
                    }
                    if (ctx != NULL && action == CONTROL_ACTION_FAIL_SILENT_OFF) {
                        ctx->telemetry_suppressed = 0;
                        ctx->last_ctrl_seq = seq;
#if DEBUG_LOGS
                        ESP_LOGI(MQTT_TAG, "control: FAIL_SILENT_OFF seq=%u", (unsigned)seq);
#endif
                    }
                } else {
#if DEBUG_LOGS
                    ESP_LOGW(MQTT_TAG, "control parse failed: payload=%.*s",
                             event->data_len, event->data);
#endif
                }
            }
            break;
        }
        default:
            break;
    }
}

void mqtt_start(system_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    uint8_t mac[6];

    // Use the last 3 bytes of the STA MAC to derive a stable node ID.
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(ctx->node_id, sizeof(ctx->node_id), "node-%02X%02X%02X", mac[3], mac[4], mac[5]);

    // Fill topics from config templates (supports "{node_id}" placeholders).
    format_topic(ctx->telemetry_topic,
                 sizeof(ctx->telemetry_topic),
                 MQTT_TELEMETRY_TOPIC,
                 ctx->node_id);
    format_topic(ctx->control_topic,
                 sizeof(ctx->control_topic),
                 MQTT_CONTROL_TOPIC,
                 ctx->node_id);

    esp_mqtt_client_config_t config = {0};
    config.host = MQTT_BROKER_HOST;
    config.port = MQTT_BROKER_PORT;
    config.client_id = ctx->node_id;
    config.user_context = ctx;
    config.disable_auto_reconnect = false;
    /* Raise MQTT buffer to accommodate full matrix payloads (~16–20 KB per message). */
    config.buffer_size = 32768;

    ctx->mqtt_client = esp_mqtt_client_init(&config);
    if (ctx->mqtt_client == NULL) {
        return;
    }

    esp_mqtt_client_register_event(ctx->mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, ctx);
    esp_mqtt_client_start(ctx->mqtt_client);
}

void mqtt_publish_telemetry(system_context_t *ctx, const char *payload)
{
    if (ctx == NULL || ctx->mqtt_client == NULL || payload == NULL) {
        return;
    }
    if (ctx->telemetry_suppressed) {
#if DEBUG_LOGS
        ESP_LOGI(MQTT_TAG, "telemetry suppressed by FAIL_SILENT_ON");
#endif
        return;
    }

    ESP_LOGI(MQTT_TAG, "telemetry publish: topic=%s len=%u", ctx->telemetry_topic,
             (unsigned)strlen(payload));
    esp_mqtt_client_publish(ctx->mqtt_client, ctx->telemetry_topic, payload, 0, 0, 0);
}
