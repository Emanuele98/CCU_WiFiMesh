/* Mesh Internal Bidirectional Communication Example - Optimized

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <inttypes.h>
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

/*******************************************************
 *                Constants
 *******************************************************/
#define RX_SIZE                    (1500)
#define TX_SIZE                    (1460)
#define STATS_INTERVAL_MS          (10000)  // 10 seconds
#define TEST_PAYLOAD_SIZE          (1400)   // Larger payload for max throughput
#define TX_BURST_SIZE              (1)      // Number of packets to send in a burst

/*******************************************************
 *                Variable Definitions
 *******************************************************/
static const char *MESH_TAG = "mesh_main";
static const uint8_t MESH_ID[6] = { 0x77, 0x77, 0x77, 0x77, 0x77, 0x77};
static uint8_t tx_buf[TX_SIZE] = { 0, };
static uint8_t rx_buf[RX_SIZE] = { 0, };
static bool is_mesh_connected = false;
static mesh_addr_t mesh_parent_addr;
static mesh_addr_t root_addr = {0};  // Store root address
static int mesh_layer = -1;
static esp_netif_t *netif_sta = NULL;

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

// Packet structure for latency measurement
typedef struct {
    uint32_t packet_id;
    int64_t timestamp;
    uint8_t direction;  // 0 = original, 1 = echo reply
    uint8_t payload[TEST_PAYLOAD_SIZE - sizeof(uint32_t) - sizeof(int64_t) - sizeof(uint8_t)];
} test_packet_t;

static uint32_t packet_id_counter = 0;

/*******************************************************
 *                Function Declarations
 *******************************************************/
void mesh_event_handler(void *arg, esp_event_base_t event_base,
                       int32_t event_id, void *event_data);
void esp_mesh_p2p_tx_main(void *arg);
void esp_mesh_p2p_rx_main(void *arg);
void print_performance_stats(void);

/*******************************************************
 *                Function Definitions
 *******************************************************/

void print_performance_stats(void)
{
    int64_t current_time = esp_timer_get_time();
    int64_t time_diff_ms = (current_time - last_stats_time) / 1000;
    
    if (time_diff_ms > 0) {
        // Calculate data rates in Mbps
        float tx_rate_mbps = (bytes_sent * 8.0) / (time_diff_ms * 1000.0);
        float rx_rate_mbps = (bytes_received * 8.0) / (time_diff_ms * 1000.0);
        
        // Calculate packet rate
        float tx_pps = (packets_sent * 1000.0) / time_diff_ms;
        float rx_pps = (packets_received * 1000.0) / time_diff_ms;
        
        // Calculate average latency in ms
        float avg_latency_ms = 0;
        float min_latency_ms = 0;
        float max_latency_ms = 0;
        if (latency_samples > 0) {
            avg_latency_ms = (total_latency_us / latency_samples) / 1000.0;
            min_latency_ms = min_latency_us / 1000.0;
            max_latency_ms = max_latency_us / 1000.0;
        }
        
        ESP_LOGI(MESH_TAG, "========== PERFORMANCE STATS (last %lld ms) ==========", time_diff_ms);
        ESP_LOGI(MESH_TAG, "TX: %.2f Mbps, %.1f pps, %u packets, %u bytes", 
                 tx_rate_mbps, tx_pps, packets_sent, bytes_sent);
        ESP_LOGI(MESH_TAG, "RX: %.2f Mbps, %.1f pps, %u packets, %u bytes", 
                 rx_rate_mbps, rx_pps, packets_received, bytes_received);
        ESP_LOGI(MESH_TAG, "RTT: avg=%.2f ms, min=%.2f ms, max=%.2f ms (samples=%u)", 
                 avg_latency_ms, min_latency_ms, max_latency_ms, latency_samples);
        ESP_LOGI(MESH_TAG, "Dropped: %u packets, Layer: %d, Connected: %s", 
                 packets_dropped, mesh_layer, is_mesh_connected ? "YES" : "NO");
        ESP_LOGI(MESH_TAG, "=====================================================");
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

void mesh_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    mesh_addr_t id = {0,};
    static uint16_t last_layer = 0;

    switch (event_id) {
    case MESH_EVENT_STARTED:
        esp_mesh_get_id(&id);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_MESH_STARTED>ID:"MACSTR"", MAC2STR(id.addr));
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
        break;
    case MESH_EVENT_STOPPED:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOPPED>");
        is_mesh_connected = false;
        mesh_layer = -1;
        break;
    case MESH_EVENT_CHILD_CONNECTED:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, "MACSTR"",
                 ((mesh_event_child_connected_t *)event_data)->aid,
                 MAC2STR(((mesh_event_child_connected_t *)event_data)->mac));
        break;
    case MESH_EVENT_CHILD_DISCONNECTED:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, "MACSTR"",
                 ((mesh_event_child_disconnected_t *)event_data)->aid,
                 MAC2STR(((mesh_event_child_disconnected_t *)event_data)->mac));
        break;
    case MESH_EVENT_PARENT_CONNECTED:
        esp_mesh_get_id(&id);
        mesh_layer = esp_mesh_get_layer();
        memcpy(&mesh_parent_addr.addr, id.addr, 6);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:"MACSTR"%s, ID:"MACSTR"",
                 last_layer, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "", MAC2STR(id.addr));
        last_layer = mesh_layer;
        is_mesh_connected = true;
        if (esp_mesh_is_root()) {
            esp_netif_dhcpc_start(netif_sta);
        }
        break;
    case MESH_EVENT_PARENT_DISCONNECTED:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
                 ((mesh_event_disconnected_t *)event_data)->reason);
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
        break;
    case MESH_EVENT_LAYER_CHANGE:
        mesh_layer = esp_mesh_get_layer();
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d-->%d%s",
                 last_layer, mesh_layer,
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "");
        last_layer = mesh_layer;
        break;
    case MESH_EVENT_ROOT_ADDRESS:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_ADDRESS>root address:"MACSTR"",
                 MAC2STR(((mesh_event_root_address_t *)event_data)->addr));
        // Store root address for later use
        memcpy(&root_addr.addr, ((mesh_event_root_address_t *)event_data)->addr, 6);
        break;
    default:
        // Suppress other events to reduce logging
        break;
    }
}

void ip_event_handler(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    ESP_LOGI(MESH_TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR, IP2STR(&event->ip_info.ip));
}

void esp_mesh_p2p_tx_main(void *arg)
{
    esp_err_t err;
    mesh_data_t data;
    data.data = tx_buf;
    data.size = sizeof(test_packet_t);
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;
    
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    test_packet_t *packet = (test_packet_t *)tx_buf;
    mesh_addr_t self_addr;
    
    // Get self MAC address
    esp_wifi_get_mac(WIFI_IF_STA, self_addr.addr);
    
    // Initialize stats timer
    last_stats_time = esp_timer_get_time();
    
    // Fill payload with test pattern once
    memset(packet->payload, 0xAA, sizeof(packet->payload));
    
    while (true) {
        if (!is_mesh_connected) {
            vTaskDelay(10);
            continue;
        }
        
        // Send multiple packets in a burst for higher throughput
        for (int burst = 0; burst < TX_BURST_SIZE; burst++) {
            // Prepare test packet with current timestamp
            packet->packet_id = ++packet_id_counter;
            packet->timestamp = esp_timer_get_time();
            packet->direction = 0;  // Original packet
            
            if (esp_mesh_is_root()) {
                // Root node: send to all children (skip self which is element 0)
                esp_mesh_get_routing_table((mesh_addr_t *)&route_table,
                                           CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);
                
                if (route_table_size > 1) {  // More than just self in routing table
                    // Send to all nodes except self (element 0)
                    for (int i = 1; i < route_table_size; i++) {
                        err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P | MESH_DATA_NONBLOCK, NULL, 0);
                        if (err == ESP_OK) {
                            bytes_sent += data.size;
                            packets_sent++;
                        } else if (err == ESP_ERR_MESH_QUEUE_FULL) {
                            packets_dropped++;
                            break; // Stop burst if queue is full
                        } else {
                            packets_dropped++;
                        }
                    }
                }
            } else {
                // Child node: send to root using NULL (will automatically route to root)
                err = esp_mesh_send(NULL, &data, MESH_DATA_P2P | MESH_DATA_NONBLOCK, NULL, 0);
                if (err == ESP_OK) {
                    bytes_sent += data.size;
                    packets_sent++;
                } else if (err == ESP_ERR_MESH_QUEUE_FULL) {
                    packets_dropped++;
                    break; // Stop burst if queue is full
                } else {
                    packets_dropped++;
                }
            }
        }
        
        // Print stats every 10 seconds
        int64_t current_time = esp_timer_get_time();
        if ((current_time - last_stats_time) >= (STATS_INTERVAL_MS * 1000)) {
            print_performance_stats();
        }
        
        // High frequency sending for maximum throughput
        vTaskDelay(1);
    }
}

void esp_mesh_p2p_rx_main(void *arg)
{
    esp_err_t err;
    mesh_data_t data;
    int flag = 0;
    mesh_addr_t from;
    
    data.data = rx_buf;
    data.size = RX_SIZE;
    
    // Prepare echo reply buffer
    static uint8_t reply_buf[TX_SIZE];
    mesh_data_t reply_data;
    reply_data.data = reply_buf;
    reply_data.proto = MESH_PROTO_BIN;
    reply_data.tos = MESH_TOS_P2P;
    
    while (true) {
        if (!is_mesh_connected) {
            vTaskDelay(10);
            continue;
        }
        
        data.size = RX_SIZE;
        err = esp_mesh_recv(&from, &data, 0, &flag, NULL, 0); // Non-blocking receive
        
        if (err != ESP_OK) {
            if (err != ESP_ERR_MESH_TIMEOUT) {
                // Only log real errors, not timeouts
                packets_dropped++;
            }
            vTaskDelay(1);
            continue;
        }
        
        if (!data.size) {
            continue;
        }
        
        // Update receive stats
        bytes_received += data.size;
        packets_received++;
        
        // Process received packet
        if (data.size >= sizeof(test_packet_t)) {
            test_packet_t *rx_packet = (test_packet_t *)data.data;
            
            if (rx_packet->direction == 0) {
                // This is an original packet, send echo reply
                memcpy(reply_buf, data.data, data.size);
                test_packet_t *reply_packet = (test_packet_t *)reply_buf;
                reply_packet->direction = 1;  // Mark as echo reply
                reply_data.size = data.size;
                
                if (esp_mesh_is_root()) {
                    // Root echoes back to the child that sent it
                    err = esp_mesh_send(&from, &reply_data, MESH_DATA_P2P | MESH_DATA_NONBLOCK, NULL, 0);
                } else {
                    // Child echoes back to root (using NULL for auto-routing)
                    err = esp_mesh_send(NULL, &reply_data, MESH_DATA_P2P | MESH_DATA_NONBLOCK, NULL, 0);
                }
                
                if (err == ESP_OK) {
                    bytes_sent += reply_data.size;
                    packets_sent++;
                } else {
                    packets_dropped++;
                }
            } else if (rx_packet->direction == 1) {
                // This is an echo reply, calculate RTT
                int64_t current_time = esp_timer_get_time();
                int64_t round_trip_time = current_time - rx_packet->timestamp;
                
                // Only count reasonable RTT values (< 5 seconds)
                if (round_trip_time > 0 && round_trip_time < 5000000) {
                    total_latency_us += round_trip_time;
                    latency_samples++;
                    
                    // Track min/max
                    if (round_trip_time < min_latency_us) {
                        min_latency_us = round_trip_time;
                    }
                    if (round_trip_time > max_latency_us) {
                        max_latency_us = round_trip_time;
                    }
                }
            }
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    /*  tcpip initialization */
    ESP_ERROR_CHECK(esp_netif_init());
    /*  event initialization */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /*  create network interfaces for mesh (only station instance saved for further manipulation, soft AP instance ignored */
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&netif_sta, NULL));
    /*  wifi initialization */
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));  // Disable power save completely
    
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());
    /*  mesh initialization */
    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));
    /*  set mesh topology */
    ESP_ERROR_CHECK(esp_mesh_set_topology(CONFIG_MESH_TOPOLOGY));
    /*  set mesh max layer according to the topology */
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));
    ESP_ERROR_CHECK(esp_mesh_set_xon_qsize(256));

    /* Disable mesh PS function */
    ESP_ERROR_CHECK(esp_mesh_disable_ps());
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));
    
    /* Set mesh AP connections */
    ESP_ERROR_CHECK(esp_mesh_set_ap_connections(6));  // Allow more connections

    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    /* mesh ID */
    memcpy((uint8_t *) &cfg.mesh_id, MESH_ID, 6);
    /* router */
    cfg.channel = CONFIG_MESH_CHANNEL;
    cfg.router.ssid_len = strlen(CONFIG_MESH_ROUTER_SSID);
    memcpy((uint8_t *) &cfg.router.ssid, CONFIG_MESH_ROUTER_SSID, cfg.router.ssid_len);
    memcpy((uint8_t *) &cfg.router.password, CONFIG_MESH_ROUTER_PASSWD,
           strlen(CONFIG_MESH_ROUTER_PASSWD));
    /* mesh softAP */
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(CONFIG_MESH_AP_AUTHMODE));
    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
    cfg.mesh_ap.nonmesh_max_connection = CONFIG_MESH_NON_MESH_AP_CONNECTIONS;
    memcpy((uint8_t *) &cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD,
           strlen(CONFIG_MESH_AP_PASSWD));
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));
    /* mesh start */
    ESP_ERROR_CHECK(esp_mesh_start());

    ESP_LOGI(MESH_TAG, "mesh starts successfully, heap:%" PRId32 ", %s<%d>%s, ps:%d",  esp_get_minimum_free_heap_size(),
            esp_mesh_is_root_fixed() ? "root fixed" : "root not fixed",
            esp_mesh_get_topology(), esp_mesh_get_topology() ? "(chain)":"(tree)", esp_mesh_is_ps_enabled());

    // Create TX and RX tasks with higher priority and more stack
    xTaskCreate(esp_mesh_p2p_tx_main, "MPTX", 4096, NULL, 6, NULL);
    xTaskCreate(esp_mesh_p2p_rx_main, "MPRX", 4096, NULL, 7, NULL);  // RX higher priority
    
    ESP_LOGI(MESH_TAG, "Mesh bidirectional communication started - OPTIMIZED");

    //todo latency ok
    //todo throughput! avoid sending from child and maximimise throughput
}