#ifndef UDP_S
#define UDP_S


#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/ip_addr.h"

#include <string.h>
#include <inttypes.h>

#define PORT 4080

//string
#define STRING_LENGHT 128


#define STR_LEN 128

//extern int com_f;
//extern char command[STR_LEN];

void udp_server_task(void *pvParameters);

#endif