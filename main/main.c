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

static const char *TAG = "MAIN_TCP";

#define SSID "cota_mobil"
#define PASS "123456780"

#define QUEUE_LENGTH 20      
#define STRING_SIZE 128  


void app_main(void)
{
    //SemaphoreHandle_t command_semaphore = xSemaphoreCreateBinary();
    QueueHandle_t queue_command_handler;

    //char command[128] = "\0";

    if(wifi_connect(SSID, PASS) == ESP_FAIL)
    {
        ESP_LOGE(TAG_W, "Could not connect to wifi, restarting...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    queue_command_handler = xQueueCreate(QUEUE_LENGTH, STRING_SIZE);
    if(queue_command_handler == NULL) {
        ESP_LOGE(TAG, "Error creating queue");
        return;
    }

    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void *)queue_command_handler, 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(1000));
    xTaskCreate(udp_server_task, "udp_server_task", 4096, (void *)queue_command_handler, 5, NULL);

    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
