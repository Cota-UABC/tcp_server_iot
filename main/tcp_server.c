#include "tcp_server.h"

static const char *TAG_T = "TCP";

int sock;


void keep_alive_timer_task(void *pvParameters)
{
    SemaphoreHandle_t keep_alive_semaphore = (SemaphoreHandle_t)pvParameters;

    ESP_LOGI(TAG_T, "Timeout started");
    vTaskDelay(pdMS_TO_TICKS(15000));

    xSemaphoreGive(keep_alive_semaphore);

    while(1)
        vTaskDelay(pdMS_TO_TICKS(100));
}

void do_retransmit(const int sock, task_tcp_params_t *params)
{
    int len;
    char rx_buffer[128], login[128], keep_alive[128], command[128] = "\0";
    const char *msg_nack = "NACK", *msg_ack = "ACK"; 

    TaskHandle_t keep_alive_handle = NULL;
    SemaphoreHandle_t keep_alive_semaphore = xSemaphoreCreateBinary();

    
    //crear comandos
    sprintf(login, "UABC:%s:L", USER_MAIN);
    sprintf(keep_alive, "UABC:%s:K", USER_MAIN);

    //CONEXION TIMEOUT
    xTaskCreate(keep_alive_timer_task, "keep_alive_timer_task", 4096, keep_alive_semaphore, 5, &keep_alive_handle);

    while(1){
        rx_buffer[0] = '\0';

        //keep alive timeout
        if(xSemaphoreTake(keep_alive_semaphore, 10) == pdTRUE)
        {
            ESP_LOGE(TAG_T, "Timeout reached, closing socket...");
            break;
        }

        //check if command received
        if(xQueueReceive(params->queue_command_handler, command, pdMS_TO_TICKS(10)))
        {
            ESP_LOGW(TAG_T, "Received command");
            int written = send(sock, command, strlen(command), 0);
            if (written < 0) {
                ESP_LOGE(TAG_T, "Error occurred on sending command: errno %d", errno);
            }
            else 
            {
                ESP_LOGI(TAG_T, "Command send: %s", command);
                len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
                if(len > 1)
                {
                    rx_buffer[len] = 0; 
                    ESP_LOGI(TAG_T, "Received %d bytes: %s", len, rx_buffer);
                    
                    xQueueSend(params->queue_anwserback_handler, rx_buffer, portMAX_DELAY);
                    ESP_LOGI(TAG_T, "Responce send to udp: %s", rx_buffer);
                }
            }
            
            command[0] = '\0';
        }

        //receive data
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if(len > 1)
        {
            rx_buffer[len] = 0; 
            ESP_LOGI(TAG_T, "Received %d bytes: %s", len, rx_buffer);

            //LOGIN
            if(strncmp(rx_buffer, login, strlen(login)) == 0)
            {
                int written = send(sock, msg_ack, strlen(msg_ack), 0);
                if (written < 0) {
                    ESP_LOGE(TAG_T, "Error occurred on sending ACK - Login: errno %d", errno);
                    break;
                }
                else 
                {
                    ESP_LOGI(TAG_T, "Login Acknowledge");
                    if(keep_alive_handle != NULL)
                    {
                        vTaskDelete(keep_alive_handle);
                        keep_alive_handle = NULL;
                    }
                    xTaskCreate(keep_alive_timer_task, "keep_alive_timer_task", 4096, keep_alive_semaphore, 5, &keep_alive_handle);
                }
            }
            //KEEP ALIVE
            else if(strncmp(rx_buffer, keep_alive, strlen(keep_alive)) == 0 )
            {
                int written = send(sock, msg_ack, strlen(msg_ack), 0);
                if (written < 0) {
                    ESP_LOGE(TAG_T, "Error occurred on sending ACK - Keep Alive: errno %d", errno);
                    break;
                }
                else 
                {
                    //keep_alive_handle
                    ESP_LOGI(TAG_T, "Keep alive Acknowledge" );

                    if(keep_alive_handle != NULL)
                    {
                        vTaskDelete(keep_alive_handle);
                        keep_alive_handle = NULL;
                    }
                    xTaskCreate(keep_alive_timer_task, "keep_alive_timer_task", 4096, keep_alive_semaphore, 5, &keep_alive_handle);
                }
            }
            //ACK OR NACK
            else if((strncmp(rx_buffer, "ACK", 3) == 0 || strncmp(rx_buffer, "NACK", 4) == 0))
                    ESP_LOGW(TAG_T, "ACK or NACK ignored");
            //ANYTHING ELSE
            else
            {
                int written = send(sock, msg_nack, strlen(msg_nack), 0);
                if (written < 0) {
                    ESP_LOGE(TAG_T, "Error occurred on sending NACK: errno %d", errno);
                    break;
                }
                else 
                    ESP_LOGW(TAG_T, "Unknown mesage, NACK send");
            }
        }

    }

    if(keep_alive_handle != NULL)
        vTaskDelete(keep_alive_handle);
    vSemaphoreDelete(keep_alive_semaphore);
}

void tcp_server_task(void *pvParameters)
{
    task_tcp_params_t *params = (task_tcp_params_t *) pvParameters;

    char addr_str[128];
    int addr_family = AF_INET;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT_TCP);
        ip_protocol = IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG_T, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG_T, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG_T, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG_T, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG_T, "Socket bound, port %d", PORT_TCP);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG_T, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG_T, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG_T, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }

        ESP_LOGI(TAG_T, "Socket accepted ip address: %s", addr_str);

        do_retransmit(sock, params);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}