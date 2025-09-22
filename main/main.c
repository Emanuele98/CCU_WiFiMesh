/* ESP-MESH-LITE Performance Test using ESP-NOW Protocol
   Using the correct esp_mesh_lite_espnow APIs
*/

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_now.h"

#include "esp_bridge.h"
#include "esp_mesh_lite.h"

/*******************************************************
 *                Constants
 *******************************************************/

// Performance Test Configuration
#define TEST_PAYLOAD_SIZE               220    // Safe size: 19 bytes header + 220 = 239 bytes total
#define STATS_INTERVAL_MS               10000  // 10 seconds
#define TX_INTERVAL_MS                  100    // Increased to 100ms for debugging
#define TX_BURST_SIZE                   1      // Single packet for debugging

// ESP-NOW data types for mesh-lite
#define ESPNOW_TYPE_PERF_TEST           0x50   // Custom type for performance test

// Broadcast MAC address
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/*******************************************************
 *                Type Definitions
 *******************************************************/
typedef enum {
    MSG_TYPE_ECHO_REQUEST = 0x01,
    MSG_TYPE_ECHO_REPLY = 0x02,
    MSG_TYPE_TEST_DATA = 0x03,
    MSG_TYPE_DISCOVERY = 0x04,
} msg_type_t;

// Performance test packet (max 249 bytes after type byte)
typedef struct {
    uint8_t msg_type;
    uint32_t packet_id;
    int64_t timestamp;
    uint8_t sender_mac[6];
    uint8_t payload[TEST_PAYLOAD_SIZE];
} __attribute__((packed)) test_packet_t;

typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    uint32_t last_seen;
} node_info_t;

/*******************************************************
 *                Global Variables
 *******************************************************/
static const char *TAG = "mesh_espnow_perf";

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
static bool espnow_initialized = false;

// Node management
static node_info_t known_nodes[10];
static int known_nodes_count = 0;
static SemaphoreHandle_t nodes_mutex;

// Message queue for received packets
static QueueHandle_t rx_queue;

// Event group
static EventGroupHandle_t mesh_event_group;
#define MESH_CONNECTED_BIT BIT0
#define GOT_IP_BIT        BIT1

/*******************************************************
 *                Function Declarations
 *******************************************************/
static void print_performance_stats(void);
static esp_err_t espnow_perf_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
static void tx_task(void *arg);
static void rx_task(void *arg);
static void process_packet(test_packet_t *packet, const uint8_t *sender_mac, int8_t rssi);
static esp_err_t send_packet(const uint8_t *dest_mac, test_packet_t *packet);
static esp_err_t broadcast_packet(test_packet_t *packet);
static void discover_nodes_task(void *arg);
static void add_peer_if_needed(const uint8_t *peer_addr);

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
        
        ESP_LOGI(TAG, "========== ESP-NOW PERFORMANCE STATS ==========");
        ESP_LOGI(TAG, "Role: %s, Level: %d, MAC: "MACSTR, 
                 is_root_node ? "ROOT" : "NODE", mesh_level, MAC2STR(self_mac));
        ESP_LOGI(TAG, "TX: %.2f Mbps, %.1f pps (%lu packets)", 
                 tx_rate_mbps, tx_pps, packets_sent);
        ESP_LOGI(TAG, "RX: %.2f Mbps, %.1f pps (%lu packets)", 
                 rx_rate_mbps, rx_pps, packets_received);
        ESP_LOGI(TAG, "RTT: avg=%.2f ms, min=%.2f ms, max=%.2f ms (samples=%lu)", 
                 avg_latency_ms, min_latency_ms, max_latency_ms, latency_samples);
        ESP_LOGI(TAG, "Dropped: %lu packets, Known nodes: %d", 
                 packets_dropped, known_nodes_count);
        ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "==============================================");
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

static void add_peer_if_needed(const uint8_t *peer_addr)
{
    if (peer_addr == NULL || memcmp(peer_addr, BROADCAST_MAC, 6) == 0) {
        return;
    }
    
    // Check if peer exists
    if (!esp_now_is_peer_exist(peer_addr)) {
        esp_now_peer_info_t peer_info = {0};
        memcpy(peer_info.peer_addr, peer_addr, 6);
        peer_info.channel = 0;  // Use current channel
        // CRITICAL: Use correct interface based on role
        // Root sends via AP, children send via STA
        peer_info.ifidx = is_root_node ? ESP_IF_WIFI_AP : ESP_IF_WIFI_STA;
        peer_info.encrypt = false;
        
        esp_err_t ret = esp_now_add_peer(&peer_info);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Added peer: "MACSTR" on %s interface", 
                    MAC2STR(peer_addr), is_root_node ? "AP" : "STA");
        } else {
            ESP_LOGE(TAG, "Failed to add peer "MACSTR": %s", 
                    MAC2STR(peer_addr), esp_err_to_name(ret));
        }
    }
}

static esp_err_t espnow_perf_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    //ESP_LOGI(TAG, "ESP-NOW received %d bytes from "MACSTR" (RSSI: %d)", 
    //        len, MAC2STR(recv_info->src_addr), recv_info->rx_ctrl->rssi);
    
    if (len != sizeof(test_packet_t)) {
        ESP_LOGW(TAG, "Invalid packet size: %d (expected %zu)", len, sizeof(test_packet_t));
        return ESP_FAIL;
    }
    
    // Add sender as peer if needed
    add_peer_if_needed(recv_info->src_addr);
    
    // Copy packet data and queue for processing
    typedef struct {
        test_packet_t packet;
        uint8_t sender_mac[6];
        int8_t rssi;
    } rx_event_t;
    
    rx_event_t rx_event;
    memcpy(&rx_event.packet, data, len);
    memcpy(rx_event.sender_mac, recv_info->src_addr, 6);
    rx_event.rssi = recv_info->rx_ctrl->rssi;
    
    if (xQueueSend(rx_queue, &rx_event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "RX queue full, dropping packet");
        packets_dropped++;
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "Packet queued for processing");
    return ESP_OK;
}

static void process_packet(test_packet_t *packet, const uint8_t *sender_mac, int8_t rssi)
{
    bytes_received += sizeof(test_packet_t);
    packets_received++;
    
    // Update node info
    xSemaphoreTake(nodes_mutex, portMAX_DELAY);
    bool found = false;
    for (int i = 0; i < known_nodes_count; i++) {
        if (memcmp(known_nodes[i].mac, sender_mac, 6) == 0) {
            known_nodes[i].rssi = rssi;
            known_nodes[i].last_seen = esp_timer_get_time() / 1000000;
            found = true;
            break;
        }
    }
    if (!found && known_nodes_count < 10) {
        memcpy(known_nodes[known_nodes_count].mac, sender_mac, 6);
        known_nodes[known_nodes_count].rssi = rssi;
        known_nodes[known_nodes_count].last_seen = esp_timer_get_time() / 1000000;
        known_nodes_count++;
        ESP_LOGI(TAG, "New node: "MACSTR" (RSSI: %d)", MAC2STR(sender_mac), rssi);
    }
    xSemaphoreGive(nodes_mutex);
    
    switch (packet->msg_type) {
        case MSG_TYPE_ECHO_REQUEST: {
            // Send echo reply
            test_packet_t reply;
            memcpy(&reply, packet, sizeof(test_packet_t));
            reply.msg_type = MSG_TYPE_ECHO_REPLY;
            memcpy(reply.sender_mac, self_mac, 6);
            
            if (send_packet(sender_mac, &reply) == ESP_OK) {
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
                
                ESP_LOGD(TAG, "RTT from "MACSTR": %.2f ms (RSSI: %d)", 
                        MAC2STR(sender_mac), round_trip_time / 1000.0, rssi);
            }
            break;
        }
        
        case MSG_TYPE_DISCOVERY: {
            ESP_LOGI(TAG, "Discovery from "MACSTR" (RSSI: %d)", 
                    MAC2STR(sender_mac), rssi);
            break;
        }
        
        default:
            ESP_LOGW(TAG, "Unknown message type: 0x%02x", packet->msg_type);
            break;
    }
}

static esp_err_t send_packet(const uint8_t *dest_mac, test_packet_t *packet)
{
    if (!espnow_initialized) {
        ESP_LOGE(TAG, "ESP-NOW not initialized!");
        return ESP_ERR_INVALID_STATE;
    }
    
    // For broadcast, we need to add the broadcast address as a peer with correct interface
    if (dest_mac == NULL || memcmp(dest_mac, BROADCAST_MAC, 6) == 0) {
        // Add broadcast peer if not exists
        if (!esp_now_is_peer_exist(BROADCAST_MAC)) {
            esp_now_peer_info_t peer_info = {0};
            memcpy(peer_info.peer_addr, BROADCAST_MAC, 6);
            peer_info.channel = 0;
            // Use AP interface for root, STA for children
            peer_info.ifidx = is_root_node ? ESP_IF_WIFI_AP : ESP_IF_WIFI_STA;
            peer_info.encrypt = false;
            
            esp_err_t add_ret = esp_now_add_peer(&peer_info);
            if (add_ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to add broadcast peer: %s", esp_err_to_name(add_ret));
                return add_ret;
            }
            ESP_LOGI(TAG, "Added broadcast peer on %s interface", 
                    is_root_node ? "AP" : "STA");
        }
        dest_mac = BROADCAST_MAC;
    } else {
        // Add unicast peer if needed
        add_peer_if_needed(dest_mac);
    }
    
    // Use esp_mesh_lite_espnow_send with our custom type
    esp_err_t ret = esp_mesh_lite_espnow_send(ESPNOW_TYPE_PERF_TEST, 
                                               (uint8_t *)dest_mac, 
                                               (uint8_t *)packet, 
                                               sizeof(test_packet_t));
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Send failed to "MACSTR": %s (interface: %s)", 
                MAC2STR(dest_mac), esp_err_to_name(ret),
                is_root_node ? "AP" : "STA");
    } else {
        ESP_LOGD(TAG, "Sent packet %lu to "MACSTR, packet->packet_id, MAC2STR(dest_mac));
    }
    
    return ret;
}

static esp_err_t broadcast_packet(test_packet_t *packet)
{
    return send_packet(BROADCAST_MAC, packet);
}

// New function to send to known mesh nodes
static esp_err_t send_to_mesh_nodes(test_packet_t *packet)
{
    esp_err_t ret = ESP_OK;
    int sent_count = 0;
    
    if (is_root_node) {
        // Root: Get connected stations and send to each
        wifi_sta_list_t sta_list = {0};
        esp_wifi_ap_get_sta_list(&sta_list);
        
        ESP_LOGD(TAG, "Root sending to %d connected children", sta_list.num);
        
        for (int i = 0; i < sta_list.num; i++) {
            add_peer_if_needed(sta_list.sta[i].mac);
            ret = send_packet(sta_list.sta[i].mac, packet);
            if (ret == ESP_OK) {
                sent_count++;
                ESP_LOGD(TAG, "Sent to child "MACSTR, MAC2STR(sta_list.sta[i].mac));
            }
        }
        
        if (sta_list.num == 0) {
            ESP_LOGW(TAG, "No children connected, trying broadcast");
            ret = broadcast_packet(packet);
        }
    } else {
        // Non-root: Send to parent (AP we're connected to)
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK && ap_info.rssi != 0) {
            add_peer_if_needed(ap_info.bssid);
            ret = send_packet(ap_info.bssid, packet);
            if (ret == ESP_OK) {
                sent_count++;
                ESP_LOGD(TAG, "Sent to parent "MACSTR, MAC2STR(ap_info.bssid));
            }
        } else {
            ESP_LOGW(TAG, "Not connected to parent, trying broadcast");
            ret = broadcast_packet(packet);
        }
    }
    
    if (sent_count > 0) {
        ESP_LOGD(TAG, "Successfully sent to %d nodes", sent_count);
    }
    
    return ret;
}

static void rx_task(void *arg)
{
    typedef struct {
        test_packet_t packet;
        uint8_t sender_mac[6];
        int8_t rssi;
    } rx_event_t;
    
    rx_event_t rx_event;
    
    ESP_LOGI(TAG, "RX task started");
    
    while (1) {
        if (xQueueReceive(rx_queue, &rx_event, portMAX_DELAY) == pdTRUE) {
            process_packet(&rx_event.packet, rx_event.sender_mac, rx_event.rssi);
        }
    }
    
    vTaskDelete(NULL);
}

static void tx_task(void *arg)
{
    test_packet_t packet;
    esp_err_t ret;
    
    ESP_LOGI(TAG, "TX task started");
    
    // Wait for mesh to be ready
    xEventGroupWaitBits(mesh_event_group, MESH_CONNECTED_BIT | GOT_IP_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    
    // Initialize packet
    memset(&packet, 0, sizeof(packet));
    memset(packet.payload, 0xAA, sizeof(packet.payload));
    memcpy(packet.sender_mac, self_mac, 6);
    
    // Initialize stats timer
    last_stats_time = esp_timer_get_time();
    
    // Wait for network to stabilize and peers to connect
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    ESP_LOGI(TAG, "Starting performance test, packet size: %d bytes", sizeof(test_packet_t));
    
    while (1) {
        if (!is_mesh_connected || !espnow_initialized) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // Send burst of packets
        for (int burst = 0; burst < TX_BURST_SIZE; burst++) {
            // Prepare echo request
            packet.msg_type = MSG_TYPE_ECHO_REQUEST;
            packet.packet_id = ++packet_id_counter;
            packet.timestamp = esp_timer_get_time();
            
            // Send to mesh nodes (not broadcast)
            ret = send_to_mesh_nodes(&packet);
            
            if (ret == ESP_OK) {
                bytes_sent += sizeof(packet);
                packets_sent++;
            } else {
                packets_dropped++;
                if (ret == ESP_ERR_ESPNOW_NO_MEM) {
                    // Queue full, stop burst
                    break;
                }
            }
        }
        
        // Print stats periodically
        int64_t current_time = esp_timer_get_time();
        if ((current_time - last_stats_time) >= (STATS_INTERVAL_MS * 1000)) {
            print_performance_stats();
        }
        
        vTaskDelay(pdMS_TO_TICKS(TX_INTERVAL_MS));
    }
    
    vTaskDelete(NULL);
}

static void discover_nodes_task(void *arg)
{
    test_packet_t discovery_packet = {0};
    discovery_packet.msg_type = MSG_TYPE_DISCOVERY;
    memcpy(discovery_packet.sender_mac, self_mac, 6);
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // Every 10 seconds for debugging
        
        if (is_mesh_connected && espnow_initialized) {
            discovery_packet.packet_id++;
            discovery_packet.timestamp = esp_timer_get_time();
            
            // Use mesh-aware sending instead of broadcast
            send_to_mesh_nodes(&discovery_packet);
            ESP_LOGI(TAG, "Sent discovery to mesh nodes");
            
            // Print known nodes
            xSemaphoreTake(nodes_mutex, portMAX_DELAY);
            ESP_LOGI(TAG, "Known nodes: %d", known_nodes_count);
            for (int i = 0; i < known_nodes_count && i < 5; i++) {
                ESP_LOGI(TAG, "  Node[%d]: "MACSTR" (RSSI: %d, Last: %lus ago)", 
                        i, MAC2STR(known_nodes[i].mac), known_nodes[i].rssi,
                        (uint32_t)(esp_timer_get_time() / 1000000 - known_nodes[i].last_seen));
            }
            xSemaphoreGive(nodes_mutex);
        }
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
    
    ESP_LOGI(TAG, "System: ch=%d, layer=%d, parent="MACSTR", rssi=%d",
             primary, esp_mesh_lite_get_level(), MAC2STR(ap_info.bssid),
             ap_info.rssi);
    
#if CONFIG_MESH_LITE_NODE_INFO_REPORT
    ESP_LOGI(TAG, "Mesh nodes: %lu", esp_mesh_lite_get_mesh_node_number());
#endif
    
    if (wifi_sta_list.num > 0) {
        ESP_LOGI(TAG, "Children: %d", wifi_sta_list.num);
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        
        mesh_level = esp_mesh_lite_get_level();
        is_root_node = (mesh_level == 1);
        is_mesh_connected = true;
        
        ESP_LOGI(TAG, "Node type: %s, Level: %d", 
                 is_root_node ? "ROOT" : "NODE", mesh_level);
        
        xEventGroupSetBits(mesh_event_group, GOT_IP_BIT | MESH_CONNECTED_BIT);
    }
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
    // Station configuration
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ROUTER_SSID,
            .password = CONFIG_ROUTER_PASSWORD,
        },
    };
    esp_bridge_wifi_set_config(WIFI_IF_STA, &wifi_config);
    
    // SoftAP configuration
    wifi_config_t softap_config = {
        .ap = {
            .ssid = CONFIG_BRIDGE_SOFTAP_SSID,
            .password = CONFIG_BRIDGE_SOFTAP_PASSWORD,
            .max_connection = 10,
            .channel = 6,
        },
    };
    esp_bridge_wifi_set_config(WIFI_IF_AP, &softap_config);
}

void app_wifi_set_softap_info(void)
{
    char softap_ssid[33];
    uint8_t softap_mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, softap_mac);
    
    snprintf(softap_ssid, sizeof(softap_ssid), "%s_%02X%02X", 
             CONFIG_BRIDGE_SOFTAP_SSID, softap_mac[4], softap_mac[5]);
    
    esp_mesh_lite_set_softap_info(softap_ssid, CONFIG_BRIDGE_SOFTAP_PASSWORD);
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_MAX);
    
    // Initialize storage
    esp_storage_init();
    
    // Initialize networking
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create network interfaces
    esp_bridge_create_all_netif();
    
    // Initialize WiFi
    wifi_init();
    
    // Initialize ESP-MESH-LITE
    esp_mesh_lite_config_t mesh_lite_config = ESP_MESH_LITE_DEFAULT_INIT();
    mesh_lite_config.join_mesh_ignore_router_status = false;  // Set true for no-router
    mesh_lite_config.join_mesh_without_configured_wifi = false;
    
    esp_mesh_lite_init(&mesh_lite_config);
    
    // Set SoftAP info
    app_wifi_set_softap_info();
    
    // Get MAC address
    esp_wifi_get_mac(ESP_IF_WIFI_STA, self_mac);
    ESP_LOGI(TAG, "Device MAC: "MACSTR, MAC2STR(self_mac));
    
    // Create synchronization primitives
    mesh_event_group = xEventGroupCreate();
    nodes_mutex = xSemaphoreCreateMutex();
    
    // Create larger queue for high throughput testing
    rx_queue = xQueueCreate(50, sizeof(struct {
        test_packet_t packet;
        uint8_t sender_mac[6];
        int8_t rssi;
    }));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &ip_event_handler, NULL, NULL));
    
    // Start mesh
    esp_mesh_lite_start();
    
    // Wait a bit for mesh to initialize
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Initialize ESP-NOW through mesh-lite
    espnow_initialized = true;
    ESP_LOGI(TAG, "ESP-NOW initialized successfully");
    
    // Register our custom ESP-NOW receive callback for performance test type
    esp_err_t espnow_ret = esp_mesh_lite_espnow_recv_cb_register(ESPNOW_TYPE_PERF_TEST, 
                                                        espnow_perf_recv_cb);
    if (espnow_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ESP-NOW callback: %s", esp_err_to_name(espnow_ret));
        return;
    }
    ESP_LOGI(TAG, "ESP-NOW callback registered for type 0x%02x", ESPNOW_TYPE_PERF_TEST);
    
    // Check packet size
    if (sizeof(test_packet_t) > ESPNOW_PAYLOAD_MAX_LEN - 1) {
        ESP_LOGE(TAG, "Packet size %zu exceeds ESP-NOW max %d!", 
                sizeof(test_packet_t), ESPNOW_PAYLOAD_MAX_LEN - 1);
        return;
    }
    
    // Create tasks
    xTaskCreate(rx_task, "rx_task", 4096, NULL, 7, NULL);  // High priority
    xTaskCreate(tx_task, "tx_task", 4096, NULL, 5, NULL);
    xTaskCreate(discover_nodes_task, "discover", 4096, NULL, 3, NULL);
    
    // Start system info timer
    TimerHandle_t timer = xTimerCreate("sys_info", 30000 / portTICK_PERIOD_MS,
                                       pdTRUE, NULL, print_system_info_timercb);
    xTimerStart(timer, 0);
    
    ESP_LOGI(TAG, "ESP-MESH-LITE ESP-NOW Performance Test Started");
    ESP_LOGI(TAG, "Packet size: %zu bytes (max: %d)", 
             sizeof(test_packet_t), ESPNOW_PAYLOAD_MAX_LEN - 1);
    ESP_LOGI(TAG, "TX interval: %d ms, Burst: %d packets", 
             TX_INTERVAL_MS, TX_BURST_SIZE);
}