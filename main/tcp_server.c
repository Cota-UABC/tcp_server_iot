#include "tcp_server.h"

uint8_t active_sock_f_array[MAX_SOCKETS] = {0};

QueueHandle_t received_cmmd_queue_array[MAX_SOCKETS];
QueueHandle_t response_cmmd_queue_array[MAX_SOCKETS];

SemaphoreHandle_t active_sock_mutex = 0;

static const char *TAG_T = "TCP";

void tcp_server_main_task(void *pvParameters)
{
    int index_array;

    //create mutex
    active_sock_mutex = xSemaphoreCreateMutex();

    //create queues
    for(int i = 0; i < MAX_SOCKETS; i++) 
    {
        received_cmmd_queue_array[i] = xQueueCreate(1, STR_LEN);
        response_cmmd_queue_array[i] = xQueueCreate(1, STR_LEN);
    }

    char addr_str[STR_LEN];
    int addr_family = AF_INET;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    int listen_sock;
    int sock;

    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(PORT_TCP);
    ip_protocol = IPPROTO_IP;
    

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
    
    //loop for creating sockets
    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(NEXT_SOCKET_WAIT_MS));

        ESP_LOGI(TAG_T, "Socket listening");

        struct sockaddr_storage source_addr; 
        socklen_t addr_len = sizeof(source_addr);
        sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG_T, "Unable to accept connection: errno %d", errno);
            continue;
        }

        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        //Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }

        ESP_LOGI(TAG_T, "Socket accepted ip address: %s", addr_str);

        //reserve space for task params
        task_tcp_params_t *socket_task_params = malloc(sizeof(task_tcp_params_t));

        socket_task_params->sock = sock;
        
        //get available sock index
        index_array = -1;
        if(xSemaphoreTake(active_sock_mutex, portMAX_DELAY)) 
        {
            for(int i=0; i<MAX_SOCKETS; i++)
            {
                if(active_sock_f_array[i] == AVAILABLE)
                {   
                    ESP_LOGI(TAG_T, "Sock num %d available", i);
                    active_sock_f_array[i] = UNAVAILABLE;
                    index_array = i;
                    break;
                }
            }
            xSemaphoreGive(active_sock_mutex); 
        }
        if(index_array == -1)
        {
            ESP_LOGE(TAG_T, "Max sockets created, closing socket...");
            shutdown(sock, 0);
            close(sock);
            continue;
        }

        //store data in task parameters
        socket_task_params->active_sock_f = &active_sock_f_array[index_array];
        socket_task_params->received_cmmd_queue = received_cmmd_queue_array[index_array];
        socket_task_params->response_cmmd_queue = response_cmmd_queue_array[index_array];

        xTaskCreate(manage_socket_task, "manage_socket_task", 4096, (void *)socket_task_params, 5, NULL);
    }

CLEAN_UP:
    close(listen_sock);

    //delete mutex
    vSemaphoreDelete(active_sock_mutex);

    //delete queues
    for(int i = 0; i < MAX_SOCKETS; i++) 
    {
        vQueueDelete(received_cmmd_queue_array[i]);
        vQueueDelete(response_cmmd_queue_array[i]);
    }

    vTaskDelete(NULL);
}

void manage_socket_task(void *pvParameters)
{
    task_tcp_params_t *params = (task_tcp_params_t *) pvParameters;

    char sock_task_tag[STR_LEN];
    sprintf(sock_task_tag, "TCP SOCK(%d)", params->sock);

    uint8_t login_f = 0, return_f;
    char rx_buffer[STR_LEN], tx_buffer[STR_LEN], login[STR_LEN], keep_alive[STR_LEN], command[STR_LEN] = "\0";
    const char *msg_nack = "NACK", *msg_ack = "ACK"; 

    TaskHandle_t keep_alive_handle = NULL;
    SemaphoreHandle_t keep_alive_semaphore = xSemaphoreCreateBinary();


    //crear comandos
    sprintf(login, "UABC:%s:L", USER_MAIN);
    sprintf(keep_alive, "UABC:%s:K", USER_MAIN);

    //timeout task
    xTaskCreate(keep_alive_timer_task, "keep_alive_timer_task", 4096, keep_alive_semaphore, 5, &keep_alive_handle);

    ESP_LOGW(sock_task_tag, "Waiting for login...");

    while(1)
    {
        //keep alive timeout check
        if(xSemaphoreTake(keep_alive_semaphore, 10) == pdTRUE)
        {
            ESP_LOGE(sock_task_tag, "Timeout reached, closing socket...");
            break;
        }

        //rx data
        return_f = receive_data(sock_task_tag, params->sock, rx_buffer, sizeof(rx_buffer));

        //check return value
        if(return_f == COMMUNICATION_OK)
        {
             //login not received
                if(!login_f)
                {
                    //if login correct
                    if(strncmp(rx_buffer, login, strlen(login)) == 0 )
                    {
                        sprintf(tx_buffer, "%s", msg_ack);

                        ESP_LOGI(sock_task_tag, "Login Acknowledge");
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
                        ESP_LOGE(sock_task_tag, "Login not received...");
                        goto close_socket;
                    }
                }
                //login received
                else
                {
                    //KEEP ALIVE
                    if(strncmp(rx_buffer, keep_alive, strlen(keep_alive)) == 0 )
                    {
                        sprintf(tx_buffer, "%s", msg_ack);
                        
                        ESP_LOGI(sock_task_tag, "Keep alive Acknowledge.");

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
                        tx_buffer[0] = '\0';
                        ESP_LOGW(sock_task_tag, "ACK or NACK ignored");
                    }
                    //ANYTHING ELSE
                    else
                    {
                        sprintf(tx_buffer, "%s", msg_nack);
                        ESP_LOGW(sock_task_tag, "RX Unknown, NACK send");
                    }
                }

                //send response
                if(tx_buffer[0] != '\0')
                    transmit_data(sock_task_tag, params->sock, tx_buffer);
        }
        else if(return_f == CONNECTION_CLOSED)
           goto close_socket;
        

        //command received check
        if(login_f && xQueueReceive(params->received_cmmd_queue, command, pdMS_TO_TICKS(10)))
        {
            ESP_LOGW(sock_task_tag, "Received command");
            transmit_data(sock_task_tag, params->sock, command);
            receive_data(sock_task_tag, params->sock, rx_buffer, strlen(rx_buffer));
            
            //if different than ack
            if(strncmp(rx_buffer, msg_ack, 3) != 0)
                strcpy(rx_buffer, msg_nack);

            xQueueSend(params->response_cmmd_queue, rx_buffer, portMAX_DELAY);
            ESP_LOGI(sock_task_tag, "Responce send to udp: %s", rx_buffer);
            
            command[0] = '\0';
        }

    }

close_socket:
    if(keep_alive_handle != NULL)
        vTaskDelete(keep_alive_handle);
    vSemaphoreDelete(keep_alive_semaphore);

    if(xSemaphoreTake(active_sock_mutex, portMAX_DELAY)) {
        *(params->active_sock_f) = AVAILABLE;
    xSemaphoreGive(active_sock_mutex); }


    ESP_LOGE(sock_task_tag, "Closing socket...");

    shutdown(params->sock, 0);
    close(params->sock);

    free(params);
    
    vTaskDelete(NULL);
}

void keep_alive_timer_task(void *pvParameters)
{
    SemaphoreHandle_t keep_alive_semaphore = (SemaphoreHandle_t)pvParameters;

    ESP_LOGI(TAG_T, "Timeout started");
    vTaskDelay(pdMS_TO_TICKS(KEEP_ALIVE_TIMEOUT_MS));

    xSemaphoreGive(keep_alive_semaphore);

    while(1)
        vTaskDelay(pdMS_TO_TICKS(100));
}

uint8_t receive_data(const char *LOCAL_FUNCTION_TAG, int sock, char *rx_buffer, size_t buffer_size)
{
    char local_buffer[STR_LEN] = {0};
    int len, err = 0;
    uint8_t retry_cnt = 0, partial_rx_f = 0,return_f = UNDEFINED;
    
    rx_buffer[0] = '\0';

    //do while it has not received termination delimiter and retries are not max
    do{
        len = recv(sock, local_buffer, buffer_size - 1, 0);
        if(len > 0) 
        {
            local_buffer[len] = '\0';

            //decode_string(local_buffer);

            strcat(rx_buffer, local_buffer);

            len = strlen(rx_buffer);
                        
            //check terminator delimiter
            if(rx_buffer[len-1] != TERMINATION_DELIMITER_CHR)
            {
                ESP_LOGW(LOCAL_FUNCTION_TAG, "Received partial message: %s", local_buffer);
                partial_rx_f = 1;
                continue;
            }
            partial_rx_f = 0;
            
            /*
            ESP_LOGI(LOCAL_FUNCTION_TAG, "RX LEN: %d", strlen(rx_buffer));
            for (int i = 0; i<strlen(rx_buffer); i++) {
                printf("%02X ", (unsigned char)rx_buffer[i]);
            }
            printf("\n");
            */
        
            //remove termination delimiter
            rx_buffer[len-1] = '\0';
            ESP_LOGI(LOCAL_FUNCTION_TAG, "RX: %s", rx_buffer);


            return_f = COMMUNICATION_OK;
            break;
        }
        else if(len == 0)
        {
            ESP_LOGE(LOCAL_FUNCTION_TAG, "Connection closed by peer.");
            return_f = CONNECTION_CLOSED;
            break;
        }
        else if(partial_rx_f)
        {
            err = errno;

            if(err == EAGAIN || err == EWOULDBLOCK)
            {
                ESP_LOGW(LOCAL_FUNCTION_TAG, "recv() timeout, attempt... (%d/%d)", retry_cnt + 1, MAX_RETRY_RECV);
                retry_cnt++;
            }
        }
    } while(partial_rx_f && retry_cnt <= MAX_RETRY_RECV);

    if(retry_cnt >= MAX_RETRY_RECV)
    return_f = CONNECTION_TIMEOUT;

    return return_f;
}

void transmit_data(const char *LOCAL_FUNCTION_TAG, int sock, char *tx_buffer)
{
    ESP_LOGI(LOCAL_FUNCTION_TAG, "TX: %s", tx_buffer);
    strcat(tx_buffer, TERMINATION_DELIMITER_STR);

    //encode_string(tx_buffer);

    /*
    ESP_LOGI(LOCAL_FUNCTION_TAG, "TX LEN: %d", strlen(tx_buffer));
    for (int i = 0; i<strlen(tx_buffer); i++) {
        printf("%02X ", (unsigned char)tx_buffer[i]);
    }
    printf("\n");
    */

    send(sock, tx_buffer, strlen(tx_buffer), 0);
}

void decode_string(char *str)
{
    char buffer_decoded[STR_LEN] = {0};

    for(int i=0; i<strlen(str); i=i+2)
    {
        int len = strlen(buffer_decoded);  

        buffer_decoded[len] = (((uint8_t)str[i] << 1) & 0xAA) | (((uint8_t)str[i+1] >> 1) & 0x55); 

        buffer_decoded[len + 1] = '\0';
    }

    //printf("Original: %s\n", str);
    //printf("Decoded: %s\n", buffer_decoded);

    strcpy(str, buffer_decoded);
}

void code_string(char *str)
{

    char buffer_coded[STR_LEN] = {0};

    for(int i=0; i<strlen(str); i++)
    {
        int len = strlen(buffer_coded);  
    
        buffer_coded[len] = (((uint8_t)str[i] & 0xAA) >> 1) | ((~((uint8_t)str[i] & 0xAA)) & 0xAA);
        buffer_coded[len + 1] = (((uint8_t)str[i] & 0x55) << 1) | ((~((uint8_t)str[i] & 0x55)) & 0x55);

        buffer_coded[len + 2] = '\0';
    }
    
    //printf("Original: %s\n", str);
    //printf("Coded: %s\n", buffer_coded);

    strcpy(str, buffer_coded);
}