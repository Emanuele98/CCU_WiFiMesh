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

/*******************************************************
 *                Function Definitions
 *******************************************************/

// process response to static message - inside child
static esp_err_t raw_msg_process_response(uint8_t *data, uint32_t len, 
                                     uint8_t **out_data, uint32_t* out_len, 
                                     uint32_t seq) 
{
    //set static payload
    ESP_LOGE( TAG, "Process message for root RESPONSE!");   

    // Process the received data
    if (len != sizeof(wpt_static_payload_t)) {
        ESP_LOGW(TAG, "Received unexpected message size: %d", len);
        printf(" Expected: %d\n", sizeof(wpt_static_payload_t));
        printf("Data: ");
        for (int i = 0; i < len; i++) {
            printf("%02X ", data[i]);
        }
        printf("\n");
        return ESP_FAIL;
    }

    wpt_static_payload_t *received_payload = (wpt_static_payload_t *)data;
    ESP_LOGI(TAG, "Received static payload from ID: %d, Type: %d, MAC: "MACSTR, 
             received_payload->id, received_payload->type, MAC2STR(received_payload->macAddr));
    ESP_LOGI(TAG, "OVERVOLTAGE_limit: %.2f, OVERCURRENT_limit: %.2f, OVERTEMPERATURE_limit: %.2f, FOD: %s",
             received_payload->OVERVOLTAGE_limit,
             received_payload->OVERCURRENT_limit,
             received_payload->OVERTEMPERATURE_limit,
             received_payload->FOD ? "true" : "false");

    //* Set Local Alerts
    OVER_CURRENT = received_payload->OVERCURRENT_limit;
    OVER_VOLTAGE = received_payload->OVERVOLTAGE_limit;
    OVER_TEMPERATURE = received_payload->OVERTEMPERATURE_limit;
    FOD = received_payload->FOD;

    return ESP_OK;
}

// Process received raw messages - inside root
static esp_err_t static_to_root_raw_msg_process(uint8_t *data, uint32_t len, 
                                     uint8_t **out_data, uint32_t* out_len, 
                                     uint32_t seq) 
{
    *out_len = sizeof(wpt_static_payload_t);
    *out_data = malloc(*out_len);
    wpt_static_payload_t static_payload = {
                    .id = CONFIG_UNIT_ID,
                    .type = UNIT_ROLE, 
                    .OVERVOLTAGE_limit = OVERVOLTAGE_TX,
                    .OVERCURRENT_limit = OVERCURRENT_TX,
                    .OVERTEMPERATURE_limit = OVERTEMPERATURE_TX,
                    .FOD = FOD_ACTIVE
                };
    memcpy(static_payload.macAddr, self_mac, ETH_HWADDR_LEN);
    memcpy(*out_data, (uint8_t*)&static_payload, *out_len); // set here alerts

    ESP_LOGE( TAG, "Process message for root");   
    
    // Process the received data
    if (len != sizeof(wpt_static_payload_t)) {
        ESP_LOGW(TAG, "Received unexpected message size: %d", len);
        printf(" Expected: %d\n", sizeof(wpt_static_payload_t));
        printf("Data: ");
        for (int i = 0; i < len; i++) {
            printf("%02X ", data[i]);
        }
        printf("\n");
        return ESP_FAIL;
    }

    wpt_static_payload_t *received_payload = (wpt_static_payload_t *)data;
    ESP_LOGI(TAG, "Received static payload from ID: %d, Type: %d, MAC: "MACSTR, 
             received_payload->id, received_payload->type, MAC2STR(received_payload->macAddr));
    ESP_LOGI(TAG, "OVERVOLTAGE_limit: %.2f, OVERCURRENT_limit: %.2f, OVERTEMPERATURE_limit: %.2f, FOD: %s",
             received_payload->OVERVOLTAGE_limit,
             received_payload->OVERCURRENT_limit,
             received_payload->OVERTEMPERATURE_limit,
             received_payload->FOD ? "true" : "false");

    //* Add peer strucutre
    if (received_payload->type == TX)
    {
        ESP_ERROR_CHECK(TX_peer_add(received_payload->macAddr)); 
        struct TX_peer *p = TX_peer_find_by_mac(received_payload->macAddr);
        if (p != NULL)
        {
            p->static_payload = *received_payload;
            ESP_LOGI(TAG, "TX Peer structure added! ID: %d", p->static_payload.id);
        }
    }
    else if (received_payload->type == RX)
    {
        ESP_ERROR_CHECK(RX_peer_add(received_payload->macAddr)); 
        struct RX_peer *p = RX_peer_find_by_mac(received_payload->macAddr);
        if (p != NULL)
        {
            p->static_payload = *received_payload;
            ESP_LOGI(TAG, "TX Peer structure added! ID: %d", p->static_payload.id);
        }
    }

    return ESP_OK;
}

// Send message to Root
static void send_message_to_root(uint8_t *data, size_t data_len) 
{
    ESP_LOGE( TAG, "Sending message to Root");    
    
    esp_mesh_lite_msg_config_t config = {
        .raw_msg = {
            .msg_id = TO_ROOT_STATIC_MSG_ID,  // Define your own message ID
            .expect_resp_msg_id = TO_ROOT_STATIC_MSG_ID_RESP,  // Response ID if needed
            .max_retry = 3,
            .retry_interval = 10,
            .data = data,
            .size = data_len,
            .raw_resend = esp_mesh_lite_send_raw_msg_to_root,  // Send raw message to Root
        },
    };
    
    esp_mesh_lite_send_msg(ESP_MESH_LITE_RAW_MSG, &config);
}

//* High level sending functions
static void send_static_payload(void)
{
    wpt_static_payload_t static_payload = {
                    .id = CONFIG_UNIT_ID,
                    .type = UNIT_ROLE, 
                    .OVERVOLTAGE_limit = OVERVOLTAGE_TX,
                    .OVERCURRENT_limit = OVERCURRENT_TX,
                    .OVERTEMPERATURE_limit = OVERTEMPERATURE_TX,
                    .FOD = FOD_ACTIVE
                };
    memcpy(static_payload.macAddr, self_mac, ETH_HWADDR_LEN);
    send_message_to_root((uint8_t*)&static_payload, sizeof(static_payload));
}


static void wifi_mesh_lite_task(void *pvParameters)
{
    // Initialize peer management
    peer_init();

    // Register rcv handlers
    esp_mesh_lite_raw_msg_action_t raw_actions[] = {
        { TO_ROOT_STATIC_MSG_ID, TO_ROOT_STATIC_MSG_ID_RESP, static_to_root_raw_msg_process},
        { TO_ROOT_STATIC_MSG_ID_RESP, 0, raw_msg_process_response},
        {0, 0, NULL}
    };
    esp_mesh_lite_raw_msg_action_list_register(raw_actions);

    while (1) 
    {
        vTaskDelay(5000 / portTICK_PERIOD_MS); // Delay for 10 seconds
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
            is_root_node = (mesh_level == 1);
            // if not a root, send static payload to root
            if (!is_root_node) {
                send_static_payload();
            }
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
            mesh_level = esp_mesh_lite_get_level();
            is_root_node = (mesh_level == 1);
            is_mesh_connected = true;
            ESP_LOGI(TAG, "Node type: %s, Level: %d", is_root_node ? "ROOT" : "NODE", mesh_level);
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