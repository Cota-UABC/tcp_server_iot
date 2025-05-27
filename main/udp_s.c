#include "udp_s.h"

static const char *TAG_U = "UDP_SOCKET";

void udp_server_task(void *pvParameters)
{

    char rx_buffer[128], tx_buffer[128];
    char addr_str[128];
    int addr_family = AF_INET, ip_protocol = 0;
    struct sockaddr_in6 dest_addr;

    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(PORT);
    ip_protocol = IPPROTO_IP;

    int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
    if (sock < 0)
    {
        ESP_LOGE(TAG_U, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG_U, "Socket created");

    // Set timeout
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0)
    {
        ESP_LOGE(TAG_U, "Socket unable to bind: errno %d", errno);
    }
    else
        ESP_LOGI(TAG_U, "Socket bound, port %d", PORT);

    struct sockaddr_storage source_addr;
    socklen_t socklen = sizeof(source_addr);


    while (1)
    {
        rx_buffer[0] = '\0';
        tx_buffer[0] = '\0';

        //wating for data
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

        if(len > 0)
        {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
            ESP_LOGI(TAG_U, "Received %d bytes from %s:", len, addr_str);

            rx_buffer[len] = '\0'; // terminator
            ESP_LOGI(TAG_U, "RX: %s", rx_buffer);

            //send to active sockets tasks
            for(int i=0; i<MAX_SOCKETS; i++)
            {
                if(xSemaphoreTake(active_sock_mutex, portMAX_DELAY)) {
                    if(active_sock_f_array[i])
                    {
                        //send queue to socket task
                        if(!xQueueSend(received_cmmd_queue_array[i], rx_buffer, pdMS_TO_TICKS(QUEUE_MS_WAIT)))
                            ESP_LOGE(TAG_U, "Could not send queue, previous message not received");

                        //receive queue from socket task
                        if(!xQueueReceive(response_cmmd_queue_array[i], tx_buffer, pdMS_TO_TICKS(QUEUE_MS_WAIT)))
                        {   
                            ESP_LOGE(TAG_U, "No response from socket task");
                            sprintf(tx_buffer, "No response from client");
                        }
                        //send back responce
                        sendto(sock, tx_buffer, strlen(tx_buffer), 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                        ESP_LOGI(TAG_U, "TX: %s", tx_buffer);
                    }
                    else
                        ESP_LOGW(TAG_U, "Socket %d not active", i);
                xSemaphoreGive(active_sock_mutex); }
            }
        }
    }

    ESP_LOGE(TAG_U, "Shutting down socket...");
    shutdown(sock, 0);
    close(sock);

    vTaskDelete(NULL);
}
