// Wi-Fi STA bring-up with auto-reconnect.
#include "network/wifi.h"
#include "config/config.h"
#include "config/wifi_secrets.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include <string.h>

// Event handler for Wi-Fi and IP events.
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    system_context_t *sys = (system_context_t *)arg;
    (void)event_data;

    if (sys == NULL) {
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Auto-reconnect on disconnect.
        esp_wifi_connect();
        xEventGroupClearBits(sys->wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // Signal that we have an IP address.
        xEventGroupSetBits(sys->wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }
}

void wifi_init_and_connect(system_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    // Create default Wi-Fi STA interface.
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // Register for Wi-Fi and IP events.
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, ctx);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, ctx);

    // Configure credentials from config.h.
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();
}
