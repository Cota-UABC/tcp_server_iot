#include "udp_s.h"
#include "wifi.h"

static const char *TAG_U = "UDP_SOCKET";

//int com_f = 0;
//char command[STR_LEN] = "\0";

void udp_server_task(void *pvParameters)
{
    QueueHandle_t queue_command_handler = (QueueHandle_t)pvParameters;

    char rx_buffer[STRING_LENGHT], tx_buffer[STRING_LENGHT];
    char addr_str[STRING_LENGHT];
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
        //ESP_LOGI(TAG_U, "Waiting for data");
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

        if(len > 0)
        {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
            rx_buffer[len] = '\0'; // terminator
            ESP_LOGI(TAG_U, "Received %d bytes from %s:", len, addr_str);
            ESP_LOGI(TAG_U, "%s", rx_buffer);

            if( !xQueueSend(queue_command_handler, rx_buffer, portMAX_DELAY) )
                ESP_LOGE(TAG_U, "Error sending queue: %s", rx_buffer);
            
            //retrace back 
            sprintf(tx_buffer, "ACK"); 
            sendto(sock, tx_buffer, strlen(tx_buffer), 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
        }
        //else did not receive data
    }

    ESP_LOGE(TAG_U, "Shutting down socket...");
    shutdown(sock, 0);
    close(sock);

    vTaskDelete(NULL);
}
