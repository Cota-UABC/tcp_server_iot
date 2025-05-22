#include "tcp_server.h"

static const char *TAG_T = "TCP";

int sock;


void keep_alive_timer_task(void *pvParameters)
{
    SemaphoreHandle_t keep_alive_semaphore = (SemaphoreHandle_t)pvParameters;

    ESP_LOGI(TAG_T, "Timeout started");
    vTaskDelay(pdMS_TO_TICKS(KEEP_ALIVE_MS_WAIT));

    xSemaphoreGive(keep_alive_semaphore);

    while(1)
        vTaskDelay(pdMS_TO_TICKS(100));
}

void do_retransmit(const int sock, task_tcp_params_t *params)
{
    int len, err;
    uint8_t login_f = 0, partial_rx_f = 0;
    uint16_t retry_cnt;
    char rx_buffer[STR_LEN], login[STR_LEN], keep_alive[STR_LEN], command[STR_LEN] = "\0", local_buffer[STR_LEN*2];
    const char *msg_nack = "NACK", *msg_ack = "ACK"; 

    TaskHandle_t keep_alive_handle = NULL;
    SemaphoreHandle_t keep_alive_semaphore = xSemaphoreCreateBinary();


    //crear comandos
    sprintf(login, "UABC:%s:L", USER_MAIN);
    sprintf(keep_alive, "UABC:%s:K", USER_MAIN);

    //CONNECTION TIMEOUT
    xTaskCreate(keep_alive_timer_task, "keep_alive_timer_task", 4096, keep_alive_semaphore, 5, &keep_alive_handle);

    ESP_LOGW(TAG_T, "Waiting for login...");

    while(1)
    {
        rx_buffer[0] = '\0';

        //keep alive timeout
        if(xSemaphoreTake(keep_alive_semaphore, 10) == pdTRUE)
        {
            ESP_LOGE(TAG_T, "Timeout reached, closing socket...");
            break;
        }

        //check if command received
        if(login_f && xQueueReceive(params->queue_command_handler, command, pdMS_TO_TICKS(10)))
        {
            ESP_LOGW(TAG_T, "Received command");
            transmit_receive(command, rx_buffer, &sock);
            
            //if different than ack
            if(strncmp(rx_buffer, "ACK", 3) != 0)
                strcpy(rx_buffer, "NACK");

            xQueueSend(params->queue_anwserback_handler, rx_buffer, portMAX_DELAY);
            ESP_LOGI(TAG_T, "Responce send to udp: %s", rx_buffer);
            
            command[0] = '\0';
        }

        //receive data
        rx_buffer[0] = '\0';
        local_buffer[0] = '\0';

        retry_cnt = 0;
        partial_rx_f = 0;
        do{
            len = recv(sock, local_buffer, sizeof(local_buffer) - 1, 0);
            if(len > 1)
            {
                local_buffer[len] = 0; 
                strcat(rx_buffer, local_buffer);

                len = strlen(rx_buffer);

                //check terminator delimiter
                if(rx_buffer[len-1] != TERMINATION_DELIMITER_CHR)
                {
                    ESP_LOGW(TAG_T, "Received partial message: %s", local_buffer);
                    partial_rx_f = 1;
                    continue;
                }
                partial_rx_f = 0;

                //remove termination delimiter
                rx_buffer[len-1] = '\0';
                ESP_LOGI(TAG_T, "RX: %s", rx_buffer);


                //login not received
                if(!login_f)
                {
                    //if login correct
                    if(strncmp(rx_buffer, login, strlen(login)) == 0 )
                    {
                        sprintf(local_buffer, "%s", msg_ack);

                        ESP_LOGI(TAG_T, "Login Acknowledge");
                        if(keep_alive_handle != NULL)
                        {
                            vTaskDelete(keep_alive_handle);
                            keep_alive_handle = NULL;
                        }
                        xTaskCreate(keep_alive_timer_task, "keep_alive_timer_task", 4096, keep_alive_semaphore, 5, &keep_alive_handle);
                        
                        login_f = 1;
                    }
                    else
                    {
                        ESP_LOGE(TAG_T, "Login not received...");
                        goto close_socket;
                    }
                }
                //login received
                else
                {
                    //KEEP ALIVE
                    if(strncmp(rx_buffer, keep_alive, strlen(keep_alive)) == 0 )
                    {
                        sprintf(local_buffer, "%s", msg_ack);
                        
                        ESP_LOGI(TAG_T, "Keep alive Acknowledge.");

                        if(keep_alive_handle != NULL)
                        {
                            vTaskDelete(keep_alive_handle);
                            keep_alive_handle = NULL;
                        }
                        xTaskCreate(keep_alive_timer_task, "keep_alive_timer_task", 4096, keep_alive_semaphore, 5, &keep_alive_handle);
                        
                    }
                    //ACK OR NACK
                    else if((strncmp(rx_buffer, "ACK", 3) == 0 || strncmp(rx_buffer, "NACK", 4) == 0))
                    {   
                        local_buffer[0] = '\0';
                        ESP_LOGW(TAG_T, "ACK or NACK ignored");
                    }
                    //ANYTHING ELSE
                    else
                    {
                        sprintf(local_buffer, "%s", msg_nack);
                        ESP_LOGW(TAG_T, "RX Unknown, NACK send");
                    }
                }

                //send response
                if(local_buffer[0] != '\0')
                {
                    ESP_LOGI(TAG_T, "TX: %s", local_buffer);
                    strcat(local_buffer, TERMINATION_DELIMITER_STR);
                    send(sock, local_buffer, strlen(local_buffer), 0);
                }
            }
            else if(len == 0)
            {
                ESP_LOGE(TAG_T, "Connection closed by peer.");
                goto close_socket;
            }
            else if(partial_rx_f)
            {
                err = errno;

                if(err == EAGAIN || err == EWOULDBLOCK)
                {
                    ESP_LOGW(TAG_T, "recv() timeout, attempt... (%d/%d)", retry_cnt + 1, MAX_RETRY_RECV);
                    retry_cnt++;
                }
            }
        } while(partial_rx_f && retry_cnt < MAX_RETRY_RECV);

    }

    close_socket:
    if(keep_alive_handle != NULL)
        vTaskDelete(keep_alive_handle);
    vSemaphoreDelete(keep_alive_semaphore);
}

void transmit_receive(char *tx_buffer, char *rx_buffer, int *sock_ptr)
{
    int len, err;
    uint8_t retry_cnt = 0;
    char local_rx_buffer[STR_LEN/2];

    ESP_LOGI(TAG_T, "TX: %s", tx_buffer);
    strcat(tx_buffer, TERMINATION_DELIMITER_STR);
    send(*sock_ptr, tx_buffer, strlen(tx_buffer), 0);

    len = strlen(tx_buffer);
    tx_buffer[len-1] = '\0';


    rx_buffer[0] = '\0';

    while(retry_cnt < MAX_RETRY_RECV)
    {
        len = recv(*sock_ptr, local_rx_buffer, sizeof(local_rx_buffer) - 1, 0);
        
        if(len > 0) 
        {
            local_rx_buffer[len] = '\0';
            strcat(rx_buffer, local_rx_buffer);
            
            len = strlen(rx_buffer);

            //check termination delimiter
            if(rx_buffer[len-1] != TERMINATION_DELIMITER_CHR)
            {
                ESP_LOGW(TAG_T, "Received partial message: %s", local_rx_buffer);
                continue;
            }

            rx_buffer[len-1] = '\0';
            ESP_LOGI(TAG_T, "RX: %s", rx_buffer);
            break;
        }
        else
        {
            err = errno;

            if(err == EAGAIN || err == EWOULDBLOCK)
                ESP_LOGW(TAG_T, "recv() timeout, attempt... (%d/%d)", retry_cnt + 1, MAX_RETRY_RECV);
            else
            {
                ESP_LOGE(TAG_T, "recv() error: %d (%s)", err, strerror(err));
                break;
            }
        }
        retry_cnt++;
    }

    return;
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

    int listen_sock;
    while(1)
    {
        if (addr_family == AF_INET) {
            struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
            dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
            dest_addr_ip4->sin_family = AF_INET;
            dest_addr_ip4->sin_port = htons(PORT_TCP);
            ip_protocol = IPPROTO_IP;
        }

        listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
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
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }

        ESP_LOGI(TAG_T, "Socket accepted ip address: %s", addr_str);

        do_retransmit(sock, params);

        ESP_LOGE(TAG_T, "Closing socket...");

        shutdown(sock, 0);
        close(sock);
    

        CLEAN_UP:
        close(listen_sock);
    }

    vTaskDelete(NULL);
}