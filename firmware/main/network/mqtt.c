#include "network/mqtt.h"
#include "config/config.h"

#include <string.h>
#include <stdlib.h>

#include "esp_event.h"
#include "mqtt_client.h"

static bool parse_control_message(const char *data, int len, uint32_t *out_value)
{
    if (data == NULL || out_value == NULL || len <= 0) {
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

    if (strstr(buf, "SET_LOAD") == NULL) {
        return false;
    }

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
    long value = strtol(num_start, &endptr, 10);
    if (endptr == num_start || value < 0) {
        return false;
    }

    *out_value = (uint32_t)value;
    return true;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    system_context_t *ctx = (system_context_t *)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    if (event == NULL) {
        return;
    }

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            esp_mqtt_client_subscribe(event->client, MQTT_CONTROL_TOPIC, 0);
            break;
        case MQTT_EVENT_DATA: {
            if (event->topic_len == (int)strlen(MQTT_CONTROL_TOPIC) &&
                strncmp(event->topic, MQTT_CONTROL_TOPIC, event->topic_len) == 0) {
                uint32_t new_value = 0;
                if (parse_control_message(event->data, event->data_len, &new_value)) {
                    if (ctx != NULL) {
                        ctx->load_factor = new_value;
                    }
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

    esp_mqtt_client_config_t config = {0};
    config.host = MQTT_BROKER_HOST;
    config.port = MQTT_BROKER_PORT;
    config.client_id = MQTT_CLIENT_ID;
    config.user_context = ctx;

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

    esp_mqtt_client_publish(ctx->mqtt_client, MQTT_TELEMETRY_TOPIC, payload, 0, 0, 0);
}
