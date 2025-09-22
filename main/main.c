/* ESP-MESH-LITE Bidirectional Performance Test
   Using TCP sockets like the mesh_local_control example
*/

#include <string.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_mac.h"

#include "esp_bridge.h"
#include "esp_mesh_lite.h"

/*******************************************************
 *                Constants
 *******************************************************/

// Performance Test Configuration
#define TCP_SERVER_PORT                 8888
#define TEST_PAYLOAD_SIZE               1400
#define STATS_INTERVAL_MS               10000  // 10 seconds
#define TX_INTERVAL_MS                  1     // Send interval
#define MAX_CONNECTIONS                 10

/*******************************************************
 *                Type Definitions
 *******************************************************/
typedef enum {
    MSG_TYPE_ECHO_REQUEST = 1,
    MSG_TYPE_ECHO_REPLY = 2,
    MSG_TYPE_TEST_DATA = 3,
} msg_type_t;

typedef struct {
    uint8_t msg_type;
    uint32_t packet_id;
    int64_t timestamp;
    uint8_t sender_mac[6];
    uint8_t payload[TEST_PAYLOAD_SIZE];
} __attribute__((packed)) test_packet_t;

typedef struct {
    int sockfd;
    uint8_t mac[6];
    char ip[16];
    bool active;
} client_info_t;

/*******************************************************
 *                Global Variables
 *******************************************************/
static const char *TAG = "mesh_lite_perf";

// Performance measurement variables
static uint32_t bytes_sent = 0;
static uint32_t bytes_received = 0;
static uint32_t packets_sent = 0;
static uint32_t packets_received = 0;
static uint32_t packets_dropped = 0;
static int64_t last_stats_time = 0;
static int64_t total_latency_us = 0;
static uint32_t latency_samples = 0;
static int64_t min_latency_us = INT64_MAX;
static int64_t max_latency_us = 0;
static uint32_t packet_id_counter = 0;

// Network status
static bool is_mesh_connected = false;
static bool is_root_node = false;
static uint8_t self_mac[6] = {0};
static int mesh_level = -1;

// TCP connections
static int server_sockfd = -1;
static int root_sockfd = -1;  // For child nodes to connect to root
static client_info_t connected_clients[MAX_CONNECTIONS];
static int num_clients = 0;
static SemaphoreHandle_t clients_mutex;

// Event group
static EventGroupHandle_t mesh_event_group;
#define MESH_CONNECTED_BIT BIT0
#define GOT_IP_BIT        BIT1

/*******************************************************
 *                Function Declarations
 *******************************************************/
static void print_performance_stats(void);
static int create_tcp_server(uint16_t port);
static int create_tcp_client(const char *ip, uint16_t port);
static void tcp_server_task(void *arg);
static void tcp_client_task(void *arg);
static void tcp_rx_task(void *arg);
static void tcp_tx_task(void *arg);
static void process_packet(test_packet_t *packet, int from_sockfd);
static esp_err_t send_packet(int sockfd, test_packet_t *packet);

/*******************************************************
 *                Function Implementations
 *******************************************************/

static void print_performance_stats(void)
{
    int64_t current_time = esp_timer_get_time();
    int64_t time_diff_ms = (current_time - last_stats_time) / 1000;
    
    if (time_diff_ms > 0) {
        float tx_rate_mbps = (bytes_sent * 8.0) / (time_diff_ms * 1000.0);
        float rx_rate_mbps = (bytes_received * 8.0) / (time_diff_ms * 1000.0);
        float tx_pps = (packets_sent * 1000.0) / time_diff_ms;
        float rx_pps = (packets_received * 1000.0) / time_diff_ms;
        
        float avg_latency_ms = 0;
        float min_latency_ms = 0;
        float max_latency_ms = 0;
        if (latency_samples > 0) {
            avg_latency_ms = (total_latency_us / latency_samples) / 1000.0;
            min_latency_ms = min_latency_us / 1000.0;
            max_latency_ms = max_latency_us / 1000.0;
        }
        
        ESP_LOGI(TAG, "========== PERFORMANCE STATS ==========");
        ESP_LOGI(TAG, "Role: %s, Level: %d, MAC: "MACSTR, 
                 is_root_node ? "ROOT" : "NODE", mesh_level, MAC2STR(self_mac));
        ESP_LOGI(TAG, "TX: %.2f Mbps, %.1f pps (%lu packets)", 
                 tx_rate_mbps, tx_pps, packets_sent);
        ESP_LOGI(TAG, "RX: %.2f Mbps, %.1f pps (%lu packets)", 
                 rx_rate_mbps, rx_pps, packets_received);
        ESP_LOGI(TAG, "RTT: avg=%.2f ms, min=%.2f ms, max=%.2f ms (samples=%lu)", 
                 avg_latency_ms, min_latency_ms, max_latency_ms, latency_samples);
        ESP_LOGI(TAG, "Dropped: %lu packets", packets_dropped);
        if (is_root_node) {
            ESP_LOGI(TAG, "Connected clients: %d", num_clients);
        }
        ESP_LOGI(TAG, "Free heap: %lu", esp_get_free_heap_size());
        ESP_LOGI(TAG, "=====================================");
    }
    
    // Reset counters
    bytes_sent = 0;
    bytes_received = 0;
    packets_sent = 0;
    packets_received = 0;
    packets_dropped = 0;
    total_latency_us = 0;
    latency_samples = 0;
    min_latency_us = INT64_MAX;
    max_latency_us = 0;
    last_stats_time = current_time;
}

static int create_tcp_server(uint16_t port)
{
    int sockfd;
    struct sockaddr_in server_addr;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return -1;
    }
    
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket");
        close(sockfd);
        return -1;
    }
    
    if (listen(sockfd, MAX_CONNECTIONS) < 0) {
        ESP_LOGE(TAG, "Failed to listen");
        close(sockfd);
        return -1;
    }
    
    ESP_LOGI(TAG, "TCP server listening on port %d", port);
    return sockfd;
}

static int create_tcp_client(const char *ip, uint16_t port)
{
    ESP_LOGD(TAG, "Creating TCP client to %s:%d", ip, port);
    
    int sockfd;
    struct sockaddr_in server_addr;
    struct ifreq iface;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return -1;
    }
    
    // Bind to STA interface
    memset(&iface, 0x0, sizeof(iface));
    esp_netif_get_netif_impl_name(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), iface.ifr_name);
    if (setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, &iface, sizeof(struct ifreq)) != 0) {
        ESP_LOGW(TAG, "Failed to bind socket to interface %s", iface.ifr_name);
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);
    
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGD(TAG, "Failed to connect to %s:%d", ip, port);
        close(sockfd);
        return -1;
    }
    
    ESP_LOGI(TAG, "Connected to TCP server at %s:%d", ip, port);
    return sockfd;
}

static esp_err_t send_packet(int sockfd, test_packet_t *packet)
{
    if (sockfd < 0) {
        return ESP_FAIL;
    }
    
    int ret = send(sockfd, packet, sizeof(test_packet_t), 0);
    if (ret != sizeof(test_packet_t)) {
        ESP_LOGD(TAG, "Send failed: %d", ret);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

static void process_packet(test_packet_t *packet, int from_sockfd)
{
    bytes_received += sizeof(test_packet_t);
    packets_received++;
    
    switch (packet->msg_type) {
        case MSG_TYPE_ECHO_REQUEST: {
            // Send echo reply
            test_packet_t reply;
            memcpy(&reply, packet, sizeof(test_packet_t));
            reply.msg_type = MSG_TYPE_ECHO_REPLY;
            memcpy(reply.sender_mac, self_mac, 6);
            
            if (send_packet(from_sockfd, &reply) == ESP_OK) {
                bytes_sent += sizeof(reply);
                packets_sent++;
            } else {
                packets_dropped++;
            }
            break;
        }
        
        case MSG_TYPE_ECHO_REPLY: {
            // Calculate RTT
            int64_t current_time = esp_timer_get_time();
            int64_t round_trip_time = current_time - packet->timestamp;
            
            if (round_trip_time > 0 && round_trip_time < 5000000) {
                total_latency_us += round_trip_time;
                latency_samples++;
                
                if (round_trip_time < min_latency_us) {
                    min_latency_us = round_trip_time;
                }
                if (round_trip_time > max_latency_us) {
                    max_latency_us = round_trip_time;
                }
            }
            break;
        }
        
        default:
            break;
    }
}

static void tcp_server_task(void *arg)
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    ESP_LOGI(TAG, "TCP server task started");
    
    server_sockfd = create_tcp_server(TCP_SERVER_PORT);
    if (server_sockfd < 0) {
        ESP_LOGE(TAG, "Failed to create TCP server");
        vTaskDelete(NULL);
        return;
    }
    
    while (1) {
        int client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (client_sockfd < 0) {
            ESP_LOGW(TAG, "Failed to accept connection");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        ESP_LOGI(TAG, "New client connected from %s", inet_ntoa(client_addr.sin_addr));
        
        // Add to clients list
        xSemaphoreTake(clients_mutex, portMAX_DELAY);
        if (num_clients < MAX_CONNECTIONS) {
            connected_clients[num_clients].sockfd = client_sockfd;
            strcpy(connected_clients[num_clients].ip, inet_ntoa(client_addr.sin_addr));
            connected_clients[num_clients].active = true;
            num_clients++;
        } else {
            ESP_LOGW(TAG, "Max clients reached, closing connection");
            close(client_sockfd);
        }
        xSemaphoreGive(clients_mutex);
    }
    
    close(server_sockfd);
    vTaskDelete(NULL);
}

static void tcp_client_task(void *arg)
{
    ESP_LOGI(TAG, "TCP client task started");
    
    // Wait for IP
    xEventGroupWaitBits(mesh_event_group, GOT_IP_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(2000));  // Wait for network to stabilize
    
    while (1) {
        if (!is_root_node && root_sockfd < 0) {
            // For ESP-MESH-LITE, we need to discover the root IP
            // Method 1: Try to get parent AP's gateway (usually the root in mesh-lite)
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            esp_netif_ip_info_t ip_info;
            char root_ip[16];
            
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                // Try the gateway IP first (likely the parent node)
                inet_ntoa_r(ip_info.gw, root_ip, sizeof(root_ip));
                ESP_LOGI(TAG, "Trying to connect to gateway/parent at %s", root_ip);
                
                root_sockfd = create_tcp_client(root_ip, TCP_SERVER_PORT);
                
                if (root_sockfd < 0) {
                    // If gateway doesn't work, try common AP subnet
                    // Parent's AP is typically at x.x.x.1
                    uint32_t ip = ntohl(ip_info.ip.addr);
                    uint32_t subnet = ip & 0xFFFFFF00;  // Get subnet
                    uint32_t parent_ip = subnet | 0x00000001;  // x.x.x.1
                    struct in_addr addr;
                    addr.s_addr = htonl(parent_ip);
                    inet_ntoa_r(addr, root_ip, sizeof(root_ip));
                    
                    ESP_LOGI(TAG, "Trying parent AP at %s", root_ip);
                    root_sockfd = create_tcp_client(root_ip, TCP_SERVER_PORT);
                }
                
                if (root_sockfd >= 0) {
                    ESP_LOGI(TAG, "Connected to parent/root at %s", root_ip);
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));  // Retry every 5 seconds
    }
    
    vTaskDelete(NULL);
}

static void tcp_rx_task(void *arg)
{
    test_packet_t packet;
    fd_set read_fds;
    struct timeval timeout;
    int max_fd;
    
    ESP_LOGI(TAG, "TCP RX task started");
    
    while (1) {
        FD_ZERO(&read_fds);
        max_fd = 0;
        
        // Add root connection (for child nodes)
        if (!is_root_node && root_sockfd >= 0) {
            FD_SET(root_sockfd, &read_fds);
            max_fd = root_sockfd;
        }
        
        // Add client connections (for root node)
        if (is_root_node) {
            xSemaphoreTake(clients_mutex, portMAX_DELAY);
            for (int i = 0; i < num_clients; i++) {
                if (connected_clients[i].active && connected_clients[i].sockfd >= 0) {
                    FD_SET(connected_clients[i].sockfd, &read_fds);
                    if (connected_clients[i].sockfd > max_fd) {
                        max_fd = connected_clients[i].sockfd;
                    }
                }
            }
            xSemaphoreGive(clients_mutex);
        }
        
        if (max_fd == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;  // 100ms timeout
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity <= 0) {
            continue;
        }
        
        // Check root connection
        if (!is_root_node && root_sockfd >= 0 && FD_ISSET(root_sockfd, &read_fds)) {
            int ret = recv(root_sockfd, &packet, sizeof(packet), 0);
            if (ret == sizeof(packet)) {
                process_packet(&packet, root_sockfd);
            } else if (ret <= 0) {
                ESP_LOGW(TAG, "Lost connection to root");
                close(root_sockfd);
                root_sockfd = -1;
            }
        }
        
        // Check client connections
        if (is_root_node) {
            xSemaphoreTake(clients_mutex, portMAX_DELAY);
            for (int i = 0; i < num_clients; i++) {
                if (connected_clients[i].active && connected_clients[i].sockfd >= 0 &&
                    FD_ISSET(connected_clients[i].sockfd, &read_fds)) {
                    
                    int ret = recv(connected_clients[i].sockfd, &packet, sizeof(packet), 0);
                    if (ret == sizeof(packet)) {
                        process_packet(&packet, connected_clients[i].sockfd);
                    } else if (ret <= 0) {
                        ESP_LOGW(TAG, "Client disconnected: %s", connected_clients[i].ip);
                        close(connected_clients[i].sockfd);
                        connected_clients[i].active = false;
                        
                        // Remove from list
                        for (int j = i; j < num_clients - 1; j++) {
                            connected_clients[j] = connected_clients[j + 1];
                        }
                        num_clients--;
                        i--;
                    }
                }
            }
            xSemaphoreGive(clients_mutex);
        }
    }
    
    vTaskDelete(NULL);
}

static void tcp_tx_task(void *arg)
{
    test_packet_t packet;
    
    ESP_LOGI(TAG, "TCP TX task started");
    
    // Initialize packet
    memset(packet.payload, 0xAA, sizeof(packet.payload));
    memcpy(packet.sender_mac, self_mac, 6);
    
    // Initialize stats timer
    last_stats_time = esp_timer_get_time();
    
    // Wait for connections
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    while (1) {
        // Prepare echo request
        packet.msg_type = MSG_TYPE_ECHO_REQUEST;
        packet.packet_id = ++packet_id_counter;
        packet.timestamp = esp_timer_get_time();
        
        if (is_root_node) {
            // Send to all clients
            xSemaphoreTake(clients_mutex, portMAX_DELAY);
            for (int i = 0; i < num_clients; i++) {
                if (connected_clients[i].active && connected_clients[i].sockfd >= 0) {
                    if (send_packet(connected_clients[i].sockfd, &packet) == ESP_OK) {
                        bytes_sent += sizeof(packet);
                        packets_sent++;
                    } else {
                        packets_dropped++;
                    }
                }
            }
            xSemaphoreGive(clients_mutex);
        } else {
            // Send to root
            if (root_sockfd >= 0) {
                if (send_packet(root_sockfd, &packet) == ESP_OK) {
                    bytes_sent += sizeof(packet);
                    packets_sent++;
                } else {
                    packets_dropped++;
                }
            }
        }
        
        // Print stats periodically
        int64_t current_time = esp_timer_get_time();
        if ((current_time - last_stats_time) >= (STATS_INTERVAL_MS * 1000)) {
            print_performance_stats();
        }
        
        vTaskDelay(TX_INTERVAL_MS);
    }
    
    vTaskDelete(NULL);
}

static void print_system_info_timercb(TimerHandle_t timer)
{
    uint8_t primary = 0;
    wifi_second_chan_t second = 0;
    wifi_ap_record_t ap_info = {0};
    wifi_sta_list_t wifi_sta_list = {0x0};
    
    esp_wifi_sta_get_ap_info(&ap_info);
    esp_wifi_ap_get_sta_list(&wifi_sta_list);
    esp_wifi_get_channel(&primary, &second);
    
    ESP_LOGI(TAG, "System: channel=%d, layer=%d, parent="MACSTR", rssi=%d, free_heap=%lu",
             primary, esp_mesh_lite_get_level(), MAC2STR(ap_info.bssid),
             ap_info.rssi, esp_get_free_heap_size());
    
#if CONFIG_MESH_LITE_NODE_INFO_REPORT
    ESP_LOGI(TAG, "Total nodes: %lu", esp_mesh_lite_get_mesh_node_number());
#endif
    
    for (int i = 0; i < wifi_sta_list.num; i++) {
        ESP_LOGI(TAG, "Child[%d]: "MACSTR, i, MAC2STR(wifi_sta_list.sta[i].mac));
    }
}

static void ip_event_sta_got_ip_handler(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    
    mesh_level = esp_mesh_lite_get_level();
    is_root_node = (mesh_level == 1);
    is_mesh_connected = true;
    
    ESP_LOGI(TAG, "Node type: %s, Level: %d", is_root_node ? "ROOT" : "NODE", mesh_level);
    
    xEventGroupSetBits(mesh_event_group, GOT_IP_BIT | MESH_CONNECTED_BIT);
}

static esp_err_t esp_storage_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static void wifi_init(void)
{
    // Station config
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ROUTER_SSID,
            .password = CONFIG_ROUTER_PASSWORD,
        },
    };
    esp_bridge_wifi_set_config(WIFI_IF_STA, &wifi_config);
    
    // SoftAP config
    wifi_config_t softap_config = {
        .ap = {
            .ssid = CONFIG_BRIDGE_SOFTAP_SSID,
            .password = CONFIG_BRIDGE_SOFTAP_PASSWORD,
            .max_connection = 10,
        },
    };
    esp_bridge_wifi_set_config(WIFI_IF_AP, &softap_config);
}

void app_wifi_set_softap_info(void)
{
    char softap_ssid[33];
    uint8_t softap_mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, softap_mac);
    
    // Create unique SSID with MAC
    snprintf(softap_ssid, sizeof(softap_ssid), "%s_%02X%02X", 
             CONFIG_BRIDGE_SOFTAP_SSID, softap_mac[4], softap_mac[5]);
    
    esp_mesh_lite_set_softap_info(softap_ssid, CONFIG_BRIDGE_SOFTAP_PASSWORD);
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    
    // Initialize storage
    esp_storage_init();
    
    // Initialize networking
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create network interfaces
    esp_bridge_create_all_netif();
    
    // Initialize WiFi
    wifi_init();
    
    // Initialize mesh-lite
    esp_mesh_lite_config_t mesh_lite_config = ESP_MESH_LITE_DEFAULT_INIT();
    esp_mesh_lite_init(&mesh_lite_config);
    
    // Set SoftAP info
    app_wifi_set_softap_info();
    
    // Get MAC address
    esp_wifi_get_mac(ESP_IF_WIFI_STA, self_mac);
    ESP_LOGI(TAG, "Device MAC: "MACSTR, MAC2STR(self_mac));
    
    // Create synchronization primitives
    mesh_event_group = xEventGroupCreate();
    clients_mutex = xSemaphoreCreateMutex();
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &ip_event_sta_got_ip_handler, NULL, NULL));
    
    // Start mesh
    esp_mesh_lite_start();
    
    // Start performance test tasks
    xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);
    xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, NULL);
    xTaskCreate(tcp_rx_task, "tcp_rx", 4096, NULL, 8, NULL);
    xTaskCreate(tcp_tx_task, "tcp_tx", 4096, NULL, 5, NULL);
    
    // Start system info timer
    TimerHandle_t timer = xTimerCreate("print_system_info", 30000 / portTICK_PERIOD_MS,
                                       pdTRUE, NULL, print_system_info_timercb);
    xTimerStart(timer, 0);
    
    ESP_LOGI(TAG, "ESP-MESH-LITE Performance Test Started");
}