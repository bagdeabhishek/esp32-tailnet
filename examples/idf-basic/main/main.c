#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#include "esp32_tailnet.h"

static const char *TAG = "tailnet_example";
static EventGroupHandle_t wifi_events;
static const EventBits_t WIFI_READY = BIT0;
static esp32_tailnet_t *tailnet;

static void http_server(void *context) {
    (void)context;
    const int server = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server < 0) {
        ESP_LOGE(TAG, "Unable to create HTTP socket");
        vTaskDelete(NULL);
        return;
    }

    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(8080),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(server, 2) != 0) {
        ESP_LOGE(TAG, "Unable to listen on TCP port 8080");
        close(server);
        vTaskDelete(NULL);
        return;
    }

    static const char response[] =
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
        "Hello from ESP32 Tailnet\n";
    while (true) {
        const int client = accept(server, NULL, NULL);
        if (client < 0) continue;
        send(client, response, sizeof(response) - 1, 0);
        shutdown(client, SHUT_RDWR);
        close(client);
    }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, WIFI_READY);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, WIFI_READY);
        if (tailnet != NULL) {
            esp32_tailnet_rebind(tailnet);
        }
    }
}

static void connect_wifi(void) {
    wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event, NULL));

    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, CONFIG_EXAMPLE_WIFI_SSID,
            sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, CONFIG_EXAMPLE_WIFI_PASSWORD,
            sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(wifi_events, WIFI_READY, pdFALSE, pdTRUE, portMAX_DELAY);
}

static void tailnet_event(esp32_tailnet_t *instance,
                          esp32_tailnet_event_t event,
                          const esp32_tailnet_status_t *status,
                          void *context) {
    (void)instance;
    (void)context;
    if (event != ESP32_TAILNET_EVENT_CONNECTED) return;

    char ip[16];
    if (esp32_tailnet_format_ipv4(status->vpn_ip, ip, sizeof(ip))) {
        ESP_LOGI(TAG, "Tailnet ready: http://%s:8080", ip);
    }
    if (status->node_key_expiry > 0) {
        ESP_LOGW(TAG, "Disable key expiry for this unattended node in the admin console");
    }
}

void app_main(void) {
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    connect_wifi();

    esp32_tailnet_config_t config;
    esp32_tailnet_config_defaults(&config);
    config.auth_key = CONFIG_EXAMPLE_TAILNET_AUTH_KEY;
    config.hostname = CONFIG_EXAMPLE_TAILNET_HOSTNAME;
    config.event_callback = tailnet_event;

    ESP_ERROR_CHECK(esp32_tailnet_create(&config, &tailnet));
    ESP_ERROR_CHECK(esp32_tailnet_start(tailnet));
    xTaskCreate(http_server, "tailnet_http", 4096, NULL, 5, NULL);
}
