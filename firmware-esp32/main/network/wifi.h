#ifndef WIFI_H
#define WIFI_H

#include "core/system_context.h"

// Event group bit indicating Wi-Fi STA is connected.
#define WIFI_CONNECTED_BIT (1 << 0)

// Bring up Wi-Fi STA and connect to configured SSID.
void wifi_init_and_connect(system_context_t *ctx);

#endif
