#include "wifiMesh.h"

/*******************************************************
 *                Variable Definitions
 *******************************************************/
static const char *MESH_TAG = "wifiMesh";
static const uint8_t MESH_ID[6] = { 0x0A, 0xB4, 0xC7, 0x2D, 0x5E, 0xFF }; // Mesh ID
static bool is_mesh_connected = false;
static mesh_addr_t mesh_parent_addr = {0};
static mesh_addr_t root_addr = {0};
static int mesh_layer = -1;
static esp_netif_t *netif_sta = NULL;
static bool is_toDS_reachable = false;


/*******************************************************
 *                Function Definitions
 *******************************************************/


void mesh_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    mesh_addr_t id = {0,};
    static uint16_t last_layer = 0;

    switch (event_id) 
    {
        case MESH_EVENT_STARTED:
            esp_mesh_get_id(&id);
            ESP_LOGW(MESH_TAG, "<MESH_EVENT_MESH_STARTED>ID:"MACSTR"", MAC2STR(id.addr));
            is_mesh_connected = false;
            mesh_layer = esp_mesh_get_layer();
            break;

        case MESH_EVENT_STOPPED:
            ESP_LOGW(MESH_TAG, "<MESH_EVENT_STOPPED>");
            is_mesh_connected = false;
            mesh_layer = -1;
            break;

        case MESH_EVENT_CHILD_CONNECTED:
            ESP_LOGW(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, "MACSTR"",
                    ((mesh_event_child_connected_t *)event_data)->aid,
                    MAC2STR(((mesh_event_child_connected_t *)event_data)->mac));
            break;

        case MESH_EVENT_CHILD_DISCONNECTED:
            ESP_LOGW(MESH_TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, "MACSTR"",
                    ((mesh_event_child_disconnected_t *)event_data)->aid,
                    MAC2STR(((mesh_event_child_disconnected_t *)event_data)->mac));
            break;

        case MESH_EVENT_PARENT_CONNECTED:
            esp_mesh_get_id(&id);
            mesh_layer = esp_mesh_get_layer();
            mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
            memcpy(&mesh_parent_addr.addr, connected->connected.bssid, 6);
            ESP_LOGW(MESH_TAG, "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:"MACSTR"%s, ID:"MACSTR"",
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
            ESP_LOGW(MESH_TAG, "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
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
            if (esp_mesh_is_root()) {
                peer_init();
            }
            break;

        case MESH_EVENT_TODS_STATE:
            mesh_event_toDS_state_t *state = (mesh_event_toDS_state_t *)event_data;
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_TODS_STATE>%s",
                    (*state == MESH_TODS_REACHABLE) ? "MESH_TODS_REACHABLE" : "MESH_TODS_UNREACHABLE");
            is_toDS_reachable = (*state == MESH_TODS_REACHABLE);
            break;

        case MESH_EVENT_FIND_NETWORK:
            ESP_LOGI(MESH_TAG, "<MESH_EVENT_FIND_NETWORK>");
            break;

        default:
            // Suppress other events to reduce logging
            ESP_LOGW(MESH_TAG, "Unhandled mesh event id: %d", event_id);
            break;
    }
}

void ip_event_handler(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    switch (event_id) 
    {
        case IP_EVENT_STA_GOT_IP:
            ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
            ESP_LOGI(MESH_TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR, IP2STR(&event->ip_info.ip));
            if (esp_mesh_is_root()) {
                esp_mesh_post_toDS_state(true);  // true = reachable
            }
            break;

        case IP_EVENT_STA_LOST_IP:
            ESP_LOGW(MESH_TAG, "<IP_EVENT_STA_LOST_IP>");
            if (esp_mesh_is_root()) {
                esp_mesh_post_toDS_state(false);  // false = unreachable
            }
            break;

        default:
            ESP_LOGW(MESH_TAG, "Unhandled ip event id: %d", event_id);
            break;
    }
}

void wifi_mesh_init()
{
    /*  tcpip initialization */
    ESP_ERROR_CHECK(esp_netif_init());

    /*  event initialization */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /*  create network interfaces for mesh (only station instance saved for further manipulation, soft AP instance ignored */
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&netif_sta, NULL));

    /*  wifi initialization */
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));

    /*  set WiFi power save mode */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    
    /* Register WiFi event handler*/
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL));

    /* Set WiFi storage*/
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

    /* Start WiFi*/
    ESP_ERROR_CHECK(esp_wifi_start());
    
    /*  mesh initialization */
    ESP_ERROR_CHECK(esp_mesh_init());

    /* Register mesh event handler*/
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));
    
    /*  set mesh topology */
    ESP_ERROR_CHECK(esp_mesh_set_topology(MESH_TOPO_TREE));

    /*  set mesh max layer according to the topology */
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));

    /* Set mesh vote percentage for becoming root*/
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(0.9f));

    /* Set mesh RX queue allocated window to each child node*/
    ESP_ERROR_CHECK(esp_mesh_set_xon_qsize(256));

    /* Disable mesh PS function */
    ESP_ERROR_CHECK(esp_mesh_disable_ps());

    /* Set AP association expire time */
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(30));

    /* mesh configuration */
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

    /* set mesh configuration */
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));

    /* mesh start */
    ESP_ERROR_CHECK(esp_mesh_start());
}