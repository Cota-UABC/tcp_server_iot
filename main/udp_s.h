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

#include "tcp_server.h"

#include <string.h>
#include <inttypes.h>

#define PORT 8250

#define QUEUE_MS_WAIT 5000

void udp_server_task(void *pvParameters);

#endif