#ifndef WIFI_H
#define WIFI_H

#include "core/system_context.h"

#define WIFI_CONNECTED_BIT (1 << 0)

void wifi_init_and_connect(system_context_t *ctx);

#endif
