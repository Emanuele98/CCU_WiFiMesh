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
#define GOT_IP_BIT        BIT1

/*******************************************************
 *                Function Definitions
 *******************************************************/

 
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

static void wifi_init(void)
{
    // Station config
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_MESH_ROUTER_SSID,
            .password = CONFIG_MESH_ROUTER_PASSWD,
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
    
    // Set SoftAP info
    app_wifi_set_softap_info();
    
    // Get MAC address
    esp_wifi_get_mac(ESP_IF_WIFI_STA, self_mac);
    ESP_LOGI(TAG, "Device MAC: "MACSTR, MAC2STR(self_mac));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &ip_event_sta_got_ip_handler, NULL, NULL));
    
    // Start mesh
    esp_mesh_lite_start();

    // Create synchronization primitives
    mesh_event_group = xEventGroupCreate();
    
    // Start system info timer
    TimerHandle_t timer = xTimerCreate("print_system_info", 30000 / portTICK_PERIOD_MS,
                                       pdTRUE, NULL, print_system_info_timercb);
    xTimerStart(timer, 0);
    
    ESP_LOGI(TAG, "ESP-MESH-LITE initialized");
}