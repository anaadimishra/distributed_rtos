#include "network/wifi.h"
#include "config/config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event_legacy.h"
#include "esp_system.h"
#include "tcpip_adapter.h"
#include "nvs_flash.h"

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    system_context_t *sys = (system_context_t *)ctx;

    if (event == NULL || sys == NULL) {
        return ESP_OK;
    }

    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(sys->wifi_event_group, WIFI_CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            xEventGroupClearBits(sys->wifi_event_group, WIFI_CONNECTED_BIT);
            break;
        default:
            break;
    }

    return ESP_OK;
}

void wifi_init_and_connect(system_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    nvs_flash_init();
    tcpip_adapter_init();

    // NOTE: Verify esp_event_loop_init usage for your ESP8266 RTOS SDK version.
    esp_event_loop_init(wifi_event_handler, ctx);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", WIFI_PASSWORD);

    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();
}
