#include "wifiMesh.h"

/*******************************************************
 *                Variable Definitions
 *******************************************************/
static const char *TAG = "wifiMesh";

// Network status
static bool is_mesh_connected = false;
static bool is_root_node = false;
static uint8_t self_mac[ETH_HWADDR_LEN] = {0};
static int mesh_level = -1;

#define MAX_PAYLOAD_SIZE                1400  // Safe size for mesh messages

// Performance metrics
static uint32_t tx_bytes = 0;
static uint32_t rx_bytes = 0;
static uint32_t tx_packets = 0;
static uint32_t rx_packets = 0;
static uint32_t tx_errors = 0;
static int64_t total_latency_us = 0;
static uint32_t latency_samples = 0;
static int64_t min_latency_us = INT64_MAX;
static int64_t max_latency_us = 0;
static int64_t last_stats_time = 0;
static uint32_t packet_id_counter = 0;

typedef struct {
    uint32_t packet_id;
    int64_t timestamp_us;
    uint8_t sender_mac[6];
    uint16_t payload_size;
    uint8_t test_type;  // 0=latency, 1=throughput
    uint8_t payload[MAX_PAYLOAD_SIZE];
} __attribute__((packed)) perf_test_packet_t;

static void print_performance_stats(void)
{
    int64_t current_time = esp_timer_get_time();
    int64_t time_diff_ms = (current_time - last_stats_time) / 1000;
    
    if (time_diff_ms <= 0) return;
    
    float tx_rate_kbps = (tx_bytes * 8.0) / time_diff_ms;  // Kbps
    float rx_rate_kbps = (rx_bytes * 8.0) / time_diff_ms;  // Kbps
    float tx_pps = (tx_packets * 1000.0) / time_diff_ms;
    float rx_pps = (rx_packets * 1000.0) / time_diff_ms;
    
    float avg_latency_ms = 0;
    float min_latency_ms = 0;
    float max_latency_ms = 0;
    if (latency_samples > 0) {
        avg_latency_ms = (total_latency_us / latency_samples) / 1000.0;
        min_latency_ms = min_latency_us / 1000.0;
        max_latency_ms = max_latency_us / 1000.0;
    }
    
    ESP_LOGI(TAG, "========== MESH PERFORMANCE STATS ==========");
    ESP_LOGI(TAG, "Role: %s | Level: %d | MAC: "MACSTR, 
             is_root_node ? "ROOT" : "CHILD", mesh_level, MAC2STR(self_mac));
    ESP_LOGI(TAG, "TX: %.2f Kbps | %.1f pps | %lu packets | %lu errors", 
             tx_rate_kbps, tx_pps, tx_packets, tx_errors);
    ESP_LOGI(TAG, "RX: %.2f Kbps | %.1f pps | %lu packets", 
             rx_rate_kbps, rx_pps, rx_packets);
    
    if (latency_samples > 0) {
        ESP_LOGI(TAG, "RTT: avg=%.2f ms | min=%.2f ms | max=%.2f ms | samples=%lu", 
                 avg_latency_ms, min_latency_ms, max_latency_ms, latency_samples);
    }
    
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "===========================================");
    
    // Reset counters
    tx_bytes = 0;
    rx_bytes = 0;
    tx_packets = 0;
    rx_packets = 0;
    tx_errors = 0;
    total_latency_us = 0;
    latency_samples = 0;
    min_latency_us = INT64_MAX;
    max_latency_us = 0;
    last_stats_time = current_time;
}


/*******************************************************
 *                Function Definitions
 *******************************************************/

// process response to static message - inside child
static esp_err_t static_to_child_process_raw_msg__response(uint8_t *data, uint32_t len, 
                                     uint8_t **out_data, uint32_t* out_len, 
                                     uint32_t seq) 
{
    //set static payload
    //ESP_LOGI( TAG, "Process message for root RESPONSE!");   

    if (len < sizeof(perf_test_packet_t)) {
        ESP_LOGW(TAG, "Invalid latency response size: %lu", len);
        return ESP_FAIL;
    }
    
    perf_test_packet_t *resp = (perf_test_packet_t *)data;
    rx_bytes += len;
    rx_packets++;
    
    // Calculate RTT
    int64_t current_time = esp_timer_get_time();
    int64_t rtt_us = current_time - resp->timestamp_us;
    
    if (rtt_us > 0 && rtt_us < 10000000) {  // Sanity check: < 10 seconds
        total_latency_us += rtt_us;
        latency_samples++;
        
        if (rtt_us < min_latency_us) {
            min_latency_us = rtt_us;
        }
        if (rtt_us > max_latency_us) {
            max_latency_us = rtt_us;
        }
        
        //ESP_LOGI(TAG, "CHILD: RTT for packet %lu: %.2f ms", resp->packet_id, rtt_us / 1000.0);
    } else {
        ESP_LOGW(TAG, "CHILD: Invalid RTT for packet %lu: %lld us", resp->packet_id, rtt_us);
    }
    
    return ESP_OK;
}

// Process received raw messages - inside root
static esp_err_t static_to_root_raw_msg_process(uint8_t *data, uint32_t len, 
                                     uint8_t **out_data, uint32_t* out_len, 
                                     uint32_t seq) 
{
    //ESP_LOGI(TAG, "ROOT: latency_test_request_handler called! len=%lu, seq=%lu", len, seq);
    
    if (len < sizeof(perf_test_packet_t)) {
        ESP_LOGW(TAG, "Invalid latency request size: %lu (expected %d)", len, sizeof(perf_test_packet_t));
        return ESP_FAIL;
    }
    
    perf_test_packet_t *req = (perf_test_packet_t *)data;
    rx_bytes += len;
    rx_packets++;
    
    ESP_LOGI(TAG, "ROOT: Received latency packet %lu from "MACSTR, 
             req->packet_id, MAC2STR(req->sender_mac));
    
    // Prepare echo response
    *out_len = len;
    *out_data = malloc(*out_len);
    if (*out_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        return ESP_FAIL;
    }
    
    memcpy(*out_data, data, len);
    perf_test_packet_t *resp = (perf_test_packet_t *)*out_data;
    memcpy(resp->sender_mac, self_mac, 6);
    
    tx_bytes += *out_len;
    tx_packets++;
    
    ESP_LOGI(TAG, "ROOT: Sending echo response for packet %lu", req->packet_id);
    return ESP_OK;
}

static void send_latency_test_packet(void)
{
    perf_test_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    
    packet.packet_id = ++packet_id_counter;
    packet.timestamp_us = esp_timer_get_time();
    memcpy(packet.sender_mac, self_mac, 6);
    packet.test_type = 0;  // Latency test
    packet.payload_size = 64;  // Small payload for latency test
    memset(packet.payload, 0xAA, packet.payload_size);
    
    //ESP_LOGI(TAG, "CHILD: Sending latency test packet %lu", packet.packet_id);
    
    esp_mesh_lite_msg_config_t config = {
        .raw_msg = {
            .msg_id = TO_ROOT_STATIC_MSG_ID,
            .expect_resp_msg_id = TO_ROOT_STATIC_MSG_ID_RESP,
            .max_retry = 3,
            .retry_interval = 10,
            .data = (uint8_t *)&packet,
            .size = sizeof(perf_test_packet_t),
            .raw_resend = esp_mesh_lite_send_raw_msg_to_root,
        },
    };
    
    esp_err_t ret = esp_mesh_lite_send_msg(ESP_MESH_LITE_RAW_MSG, &config);
    if (ret == ESP_OK) {
        tx_bytes += sizeof(packet);
        tx_packets++;
        //ESP_LOGI(TAG, "CHILD: Latency packet sent successfully");
    } else {
        tx_errors++;
        ESP_LOGE(TAG, "CHILD: Failed to send latency packet: %s (%d)", esp_err_to_name(ret), ret);
    }
}

static void wifi_mesh_lite_task(void *pvParameters)
{
       // Initialize stats timer
    last_stats_time = esp_timer_get_time();

    // Initialize peer management
    peer_init();

    // Register rcv handlers
    esp_mesh_lite_raw_msg_action_t raw_actions[] = {
        { TO_ROOT_STATIC_MSG_ID, TO_ROOT_STATIC_MSG_ID_RESP, static_to_root_raw_msg_process},
        { TO_ROOT_STATIC_MSG_ID_RESP, 0, static_to_child_process_raw_msg__response},
        {0, 0, NULL}
    };
    esp_mesh_lite_raw_msg_action_list_register(raw_actions);

    TickType_t last_stats_print = xTaskGetTickCount();

    while (1) 
    {
        now = xTaskGetTickCount();
        if (!is_root_node && is_mesh_connected)
        {
            send_latency_test_packet();
        }

        // Print stats periodically (both nodes)
        if ((now - last_stats_print) >= pdMS_TO_TICKS(2000) && is_mesh_connected) {
            print_performance_stats();
            last_stats_print = now;
        }

        vTaskDelay(50); // Delay for 10 seconds
    }

    vTaskDelete(NULL);
}
 
static void print_system_info_timercb(TimerHandle_t timer)
{
    uint8_t primary                 = 0;
    uint8_t sta_mac[6]              = {0};
    wifi_ap_record_t ap_info        = {0};
    wifi_second_chan_t second       = 0;
    wifi_sta_list_t wifi_sta_list   = {0x0};

    if (esp_mesh_lite_get_level() > 1) {
        esp_wifi_sta_get_ap_info(&ap_info);
    }
    esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac);
    esp_wifi_ap_get_sta_list(&wifi_sta_list);
    esp_wifi_get_channel(&primary, &second);

    ESP_LOGI(TAG, "System information, channel: %d, layer: %d, self mac: " MACSTR ", parent bssid: " MACSTR
                ", parent rssi: %d, free heap: %"PRIu32"", primary,
                esp_mesh_lite_get_level(), MAC2STR(sta_mac), MAC2STR(ap_info.bssid),
                (ap_info.rssi != 0 ? ap_info.rssi : -120), esp_get_free_heap_size());

    for (int i = 0; i < wifi_sta_list.num; i++) {
        ESP_LOGI(TAG, "Child mac: " MACSTR, MAC2STR(wifi_sta_list.sta[i].mac));
    }

    uint32_t size = 0;
    const node_info_list_t *node = esp_mesh_lite_get_nodes_list(&size);
    printf("MeshLite nodes %ld:\r\n", size);
    for (uint32_t loop = 0; (loop < size) && (node != NULL); loop++) {
        struct in_addr ip_struct;
        ip_struct.s_addr = node->node->ip_addr;
        printf("%ld: %d, "MACSTR", %s\r\n" , loop + 1, node->node->level, MAC2STR(node->node->mac_addr), inet_ntoa(ip_struct));
        node = node->next;
    }
}

static void mesh_lite_event_handler(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data)
{
    switch (event_id)
    {
        case ESP_MESH_LITE_EVENT_NODE_JOIN:
            ESP_LOGI(TAG, "<ESP_MESH_LITE_EVENT_NODE_JOIN>");
            esp_mesh_lite_node_info_t *node_info = (esp_mesh_lite_node_info_t *)event_data;
            ESP_LOGI(TAG, "New node joined: Level %d, MAC: "MACSTR", IP: %s", node_info->level, MAC2STR(node_info->mac_addr), inet_ntoa(node_info->ip_addr));
            mesh_level = esp_mesh_lite_get_level();
            is_mesh_connected = true;
            is_root_node = (mesh_level == 1);
            // if not a root, send static payload to root
            //if (!is_root_node) {
            //    send_latency_test_packet();
            //}
            break;
        case ESP_MESH_LITE_EVENT_NODE_LEAVE:
            ESP_LOGI(TAG, "<ESP_MESH_LITE_EVENT_NODE_LEAVE>");
            esp_mesh_lite_node_info_t *left_node_info = (esp_mesh_lite_node_info_t *)event_data;
            ESP_LOGI(TAG, "Node left: Level %d, MAC: "MACSTR", IP: %s", left_node_info->level, MAC2STR(left_node_info->mac_addr), inet_ntoa(left_node_info->ip_addr));
            // Remove node from list
            break;
        case ESP_MESH_LITE_EVENT_NODE_CHANGE:
            ESP_LOGI(TAG, "<ESP_MESH_LITE_EVENT_NODE_CHANGE>");
            // NEW LEVEL OR IP ADDRESS
            esp_mesh_lite_node_info_t *changed_node_info = (esp_mesh_lite_node_info_t *)event_data;
            ESP_LOGI(TAG, "Node changed: Level %d, MAC: "MACSTR", IP: %s", changed_node_info->level, MAC2STR(changed_node_info->mac_addr), inet_ntoa(changed_node_info->ip_addr));
            // Update node in list ? no need as don't use IP address for now
            break;
        default:
            ESP_LOGW(TAG, "Unhandled mesh event id: %d", event_id);
            break;
    }

}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data)
{
    switch (event_id) 
    {
        case IP_EVENT_STA_GOT_IP:
            ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
            ESP_LOGI(TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR, IP2STR(&event->ip_info.ip));
            break;

        case IP_EVENT_STA_LOST_IP:
            ESP_LOGW(TAG, "<IP_EVENT_STA_LOST_IP>");
            is_mesh_connected = false;
            is_root_node = false;
            mesh_level = -1;
            break;

        default:
            ESP_LOGW(TAG, "Unhandled ip event id: %d", event_id);
            break;
    }
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

static void wifi_init(void)
{
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Station config (yes router connection)
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_MESH_ROUTER_SSID,
            .password = CONFIG_MESH_ROUTER_PASSWD,
        },
    };
    
    /* //! No router config test
    wifi_config_t wifi_config;
    memset(&wifi_config, 0x0, sizeof(wifi_config_t));
    */
    esp_bridge_wifi_set_config(WIFI_IF_STA, &wifi_config);
    
    // SoftAP config
    wifi_config_t softap_config = {
        .ap = {
            .ssid = CONFIG_BRIDGE_SOFTAP_SSID,
            .password = CONFIG_BRIDGE_SOFTAP_PASSWORD,
            .authmode = CONFIG_MESH_AP_AUTHMODE,
            .channel = 6,
            .max_connection = 10,
            //.beacon_interval = 5000, //5ms
            //.dtim_period = 5, // 1 to 10 - indicates how often the AP will send DTIM beacon indicating buffered data
        },
    };
    esp_bridge_wifi_set_config(WIFI_IF_AP, &softap_config);
}

void wifi_mesh_init()
{    
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

    /*
    #if CONFIG_TX_UNIT
        esp_mesh_lite_set_allowed_level(1); //! The mesh needs at least one node explicitly configured as root (level 1) for NO ROUTER configuration 
    #endif
    */

    #if CONFIG_RX_UNIT
        //esp_mesh_lite_set_disallowed_level(1);
        //esp_mesh_lite_set_disallowed_level(2);
        esp_mesh_lite_set_leaf_node(true);
    #endif

    // Set SoftAP info
    app_wifi_set_softap_info();
    
    // Get MAC address
    esp_wifi_get_mac(ESP_IF_WIFI_STA, self_mac);
    ESP_LOGI(TAG, "Device MAC: "MACSTR, MAC2STR(self_mac));
    
    // Register WiFi event handler
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL));

    // Register Mesh lite event handler
    ESP_ERROR_CHECK(esp_event_handler_register(ESP_MESH_LITE_EVENT, ESP_EVENT_ANY_ID, &mesh_lite_event_handler, NULL));
    
    // Start mesh
    esp_mesh_lite_start();
    
    // Start system info timer (debug)
    TimerHandle_t timer = xTimerCreate("print_system_info", 10000 / portTICK_PERIOD_MS,
                                      pdTRUE, NULL, print_system_info_timercb);
    xTimerStart(timer, 0);

    // WiFi Mesh Lite task
    xTaskCreate(wifi_mesh_lite_task, "wifi_mesh_lite_task", 10000, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "ESP-MESH-LITE initialized");
}