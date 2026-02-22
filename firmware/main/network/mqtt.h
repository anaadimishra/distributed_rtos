#ifndef MQTT_H
#define MQTT_H

#include "core/system_context.h"

void mqtt_start(system_context_t *ctx);
void mqtt_publish_telemetry(system_context_t *ctx, const char *payload);

#endif
