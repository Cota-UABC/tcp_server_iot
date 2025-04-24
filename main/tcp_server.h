#ifndef TCP_S
#define TCP_S

#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#define PORT_TCP                    8250
#define KEEPALIVE_IDLE              20
#define KEEPALIVE_INTERVAL          CONFIG_EXAMPLE_KEEPALIVE_INTERVAL
#define KEEPALIVE_COUNT             CONFIG_EXAMPLE_KEEPALIVE_COUNT

#define ID_BIT_LEN 16
#define USER_BIT_LEN 5
#define DEV_BIT_LEN 1
#define OPERATION_BIT_LEN 1
#define ELEMENT_BIT_LEN 3
#define VALUE_BIT_LEN 3


#define USER_MAIN "a1264598"

void do_retransmit(const int sock, QueueHandle_t queue_command_handler);

void tcp_server_task(void *pvParameters);

#endif
