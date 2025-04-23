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

//#include "gpio.h"
//#include "adc.h"
//#include "nvs_esp.h"
//#include "host_name.h"
//#include "pwm.h"

#define PORT 4080

//string
#define STRING_LENGHT 128

#define COMMAND_NUM_U 5

//command parts
#define ID 0
#define USER 1
#define OPERATION 2
#define ELEMENT 3
#define VALUE 4
#define COMMENT 5

#define STR_LEN 128

extern int com_f;
extern char command[STR_LEN];
extern TaskHandle_t udp_server_handle;

extern QueueHandle_t queueHandler;

void udp_server_task(void *pvParameters);

#endif