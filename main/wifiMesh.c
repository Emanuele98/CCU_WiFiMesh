#include "wifiMesh.h"

/*******************************************************
 *                Variable Definitions
 *******************************************************/
static const char *TAG = "wifiMesh";

// Network status
static bool is_mesh_connected = false;
static bool is_root_node = false;
static uint8_t self_mac[6] = {0};
static int mesh_level = -1;

// Event group
static EventGroupHandle_t mesh_event_group;
#define MESH_CONNECTED_BIT BIT0
#define GOT_IP_BIT         BIT1

/*******************************************************
 *                Function Definitions
 *******************************************************/

 
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
            // Sete event bits
            xEventGroupSetBits(mesh_event_group, GOT_IP_BIT | MESH_CONNECTED_BIT);
            break;

        case IP_EVENT_STA_LOST_IP:
            ESP_LOGW(TAG, "<IP_EVENT_STA_LOST_IP>");
            is_mesh_connected = false;
            is_root_node = false;
            mesh_level = -1;
            // Clear event bits
            xEventGroupClearBits(mesh_event_group, GOT_IP_BIT | MESH_CONNECTED_BIT);
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
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

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
            .beacon_interval = 5000, //5ms
            .dtim_period = 5, // 1 to 10 - indicates how often the AP will send DTIM beacon indicating buffered data
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
    #elif CONFIG_RX_UNIT
        esp_mesh_lite_set_disallowed_level(1);   
    #endif
    */

    // Set SoftAP info
    app_wifi_set_softap_info();
    
    // Get MAC address
    esp_wifi_get_mac(ESP_IF_WIFI_STA, self_mac);
    ESP_LOGI(TAG, "Device MAC: "MACSTR, MAC2STR(self_mac));
    
    // Register WiFi event handler
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL));
    
    // Start mesh
    esp_mesh_lite_start();

    // Create synchronization primitives
    mesh_event_group = xEventGroupCreate();
    
    // Start system info timer
    TimerHandle_t timer = xTimerCreate("print_system_info", 10000 / portTICK_PERIOD_MS,
                                      pdTRUE, NULL, print_system_info_timercb);
    xTimerStart(timer, 0);
    
    ESP_LOGI(TAG, "ESP-MESH-LITE initialized");
}