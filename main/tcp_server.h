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

#include "freertos/semphr.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#define PORT_TCP                    8250
#define KEEPALIVE_IDLE              20
#define KEEPALIVE_INTERVAL          5
#define KEEPALIVE_COUNT             3

#define ID_BIT_LEN 16
#define USER_BIT_LEN 5
#define DEV_BIT_LEN 1
#define OPERATION_BIT_LEN 1
#define ELEMENT_BIT_LEN 3
#define VALUE_BIT_LEN 3


#define USER_MAIN "a1264598"

#define TERMINATION_DELIMITER_CHR '\r'
#define TERMINATION_DELIMITER_STR "\r"

#define MAX_SOCKETS 5

#define MAX_RETRY_RECV 5

#define KEEP_ALIVE_TIMEOUT_MS 25000
#define NEXT_SOCKET_WAIT_MS 1000

#define AVAILABLE 0
#define UNAVAILABLE 1

#define STR_LEN 128

extern char received_command[STR_LEN];
extern uint8_t active_sock_f_array[MAX_SOCKETS];

extern QueueHandle_t received_cmmd_queue_array[MAX_SOCKETS];
extern QueueHandle_t response_cmmd_queue_array[MAX_SOCKETS];

extern SemaphoreHandle_t active_sock_mutex;

typedef struct 
{
    int sock;
    uint8_t *active_sock_f;
    QueueHandle_t received_cmmd_queue;
    QueueHandle_t response_cmmd_queue;
} task_tcp_params_t;

void tcp_server_main_task(void *pvParameters);

void manage_socket_task(void *pvParameters);

void keep_alive_timer_task(void *pvParameters);

void transmit_receive(char *tx_buffer, char *rx_buffer, int *sock_ptr);

void decode_string(char *str);

void code_string(char *str);

#endif
