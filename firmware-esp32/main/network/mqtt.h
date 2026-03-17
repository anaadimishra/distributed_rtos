#ifndef MQTT_H
#define MQTT_H

#include "core/system_context.h"

// Initialize MQTT client and subscribe to control topic.
void mqtt_start(system_context_t *ctx);

// Publish telemetry payload to the node's telemetry topic.
void mqtt_publish_telemetry(system_context_t *ctx, const char *payload);

#endif
