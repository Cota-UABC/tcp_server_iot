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

//#define SSID "Totalplay-2.4G-b518"
//#define PASS "Qxm2EAzh99Ce7Lfk"
//#define SSID "COTA_PC"
//#define PASS "0402{V8z"
//#define SSID "IoT_AP"
//#define PASS "12345678"
#define SSID "IoT_AP"
#define PASS "12345678"

#define QUEUE_LENGTH 20      
#define STRING_SIZE 128  

typedef struct 
{
    QueueHandle_t queue_command_handler;
    QueueHandle_t queue_anwserback_handler;
} task_server_params_t;


void app_main(void)
{
    task_server_params_t server_params;

    server_params.queue_command_handler = xQueueCreate(QUEUE_LENGTH, STRING_SIZE);
    server_params.queue_anwserback_handler = xQueueCreate(QUEUE_LENGTH, STRING_SIZE);

    if(wifi_connect(SSID, PASS) == ESP_FAIL)
    {
        ESP_LOGE(TAG, "Could not connect to wifi, restarting...");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    }

    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void *)&server_params, 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(1000));

    xTaskCreate(udp_server_task, "udp_server_task", 4096, (void *)&server_params, 5, NULL);

    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
