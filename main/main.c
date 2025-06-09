#include <string.h>

#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include "wifi.h"
#include "udp_s.h"
#include "tcp_server.h"

static const char *TAG = "MAIN";

//#define SSID "COTA_PC"
//#define PASS "0402{V8z"
//#define SSID "IoT_AP"
//#define PASS "12345678"
#define SSID "Totalplay-2.4G-b518"
#define PASS "Qxm2EAzh99Ce7Lfk"

#define SHOW_IP_S 120

void app_main(void)
{
    char ip_address[128] = {0};

    if(wifi_connect(SSID, PASS, ip_address) == ESP_FAIL)
    {
        ESP_LOGE(TAG, "Could not connect to wifi, restarting...");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    }

    xTaskCreate(tcp_server_main_task, "tcp_server_main_task", 4096, NULL, 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(1000));

    xTaskCreate(udp_server_task, "udp_server_task", 4096, NULL, 5, NULL);

    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(SHOW_IP_S*1000));
        ESP_LOGW(TAG, "Server ip: %s", ip_address);
    }
}
