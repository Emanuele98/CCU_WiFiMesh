#include "include/mqtt_client_manager.h"
#include "cJSON.h"

static const char *TAG = "MQTT_CLIENT";

/* MQTT client handle */
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;

/* Task handle */
static TaskHandle_t mqtt_publish_task_handle = NULL;

/* Topics */
static const char *baseTopic = "bumblebee";
static const char *dynamicTopic = "dynamic";
static const char *alertTopic = "alerts";
static const char *controlTopic = "bumblebee/control";



/*******************************************************
 *                JSON Helper Functions
 *******************************************************/

/**
 * @brief Convert MAC address to string format
 */
static void mac_to_string(const uint8_t *mac, char *str, size_t len)
{
    snprintf(str, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * @brief Create JSON string from dynamic payload
 * 
 * @param payload Pointer to dynamic payload struct
 * @param node_id Unit ID to include in JSON
 * @return char* JSON string (must be freed by caller), NULL on error
 */
static char* dynamic_payload_to_json(const mesh_dynamic_payload_t *payload, uint8_t node_id)
{
    if (!payload) {
        ESP_LOGE(TAG, "Null payload pointer");
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON root");
        return NULL;
    }

    // Add unit ID
    cJSON_AddNumberToObject(root, "unit_id", node_id);

    // Create TX object
    cJSON *tx_obj = cJSON_CreateObject();
    if (tx_obj) {
        char mac_str[18];
        mac_to_string(payload->TX.macAddr, mac_str, sizeof(mac_str));
        
        cJSON_AddStringToObject(tx_obj, "mac", mac_str);
        cJSON_AddNumberToObject(tx_obj, "id", payload->TX.id);
        cJSON_AddNumberToObject(tx_obj, "status", payload->TX.tx_status);
        cJSON_AddNumberToObject(tx_obj, "voltage", payload->TX.voltage);
        cJSON_AddNumberToObject(tx_obj, "current", payload->TX.current);
        cJSON_AddNumberToObject(tx_obj, "temp1", payload->TX.temp1);
        cJSON_AddNumberToObject(tx_obj, "temp2", payload->TX.temp2);
        
        cJSON_AddItemToObject(root, "tx", tx_obj);
    }

    // Create RX object
    cJSON *rx_obj = cJSON_CreateObject();
    if (rx_obj) {
        char mac_str[18];
        mac_to_string(payload->RX.macAddr, mac_str, sizeof(mac_str));
        
        cJSON_AddStringToObject(rx_obj, "mac", mac_str);
        cJSON_AddNumberToObject(rx_obj, "id", payload->RX.id);
        cJSON_AddNumberToObject(rx_obj, "status", payload->RX.rx_status);
        cJSON_AddNumberToObject(rx_obj, "voltage", payload->RX.voltage);
        cJSON_AddNumberToObject(rx_obj, "current", payload->RX.current);
        cJSON_AddNumberToObject(rx_obj, "temp1", payload->RX.temp1);
        cJSON_AddNumberToObject(rx_obj, "temp2", payload->RX.temp2);
        
        cJSON_AddItemToObject(root, "rx", rx_obj);
    }

    // Convert to string
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to print JSON");
        return NULL;
    }

    return json_string;
}

/**
 * @brief Create JSON string from alert payload
 * 
 * @param payload Pointer to alert payload struct
 * @param node_id Unit ID to include in JSON
 * @return char* JSON string (must be freed by caller), NULL on error
 */
static char* alert_payload_to_json(const mesh_alert_payload_t *payload, uint8_t node_id)
{
    if (!payload) {
        ESP_LOGE(TAG, "Null payload pointer");
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON root");
        return NULL;
    }

    // Add unit ID
    cJSON_AddNumberToObject(root, "unit_id", node_id);

    // Create TX alerts object
    cJSON *tx_obj = cJSON_CreateObject();
    if (tx_obj) {
        char mac_str[18];
        mac_to_string(payload->TX.macAddr, mac_str, sizeof(mac_str));
        
        cJSON_AddStringToObject(tx_obj, "mac", mac_str);
        cJSON_AddNumberToObject(tx_obj, "id", payload->TX.id);
        cJSON_AddBoolToObject(tx_obj, "overtemperature", payload->TX.TX_internal.overtemperature);
        cJSON_AddBoolToObject(tx_obj, "overcurrent", payload->TX.TX_internal.overcurrent);
        cJSON_AddBoolToObject(tx_obj, "overvoltage", payload->TX.TX_internal.overvoltage);
        cJSON_AddBoolToObject(tx_obj, "fod", payload->TX.TX_internal.FOD);
        
        cJSON_AddItemToObject(root, "tx", tx_obj);
    }

    // Create RX alerts object
    cJSON *rx_obj = cJSON_CreateObject();
    if (rx_obj) {
        char mac_str[18];
        mac_to_string(payload->RX.macAddr, mac_str, sizeof(mac_str));
        
        cJSON_AddStringToObject(rx_obj, "mac", mac_str);
        cJSON_AddNumberToObject(rx_obj, "id", payload->RX.id);
        cJSON_AddBoolToObject(rx_obj, "overtemperature", payload->RX.RX_internal.overtemperature);
        cJSON_AddBoolToObject(rx_obj, "overcurrent", payload->RX.RX_internal.overcurrent);
        cJSON_AddBoolToObject(rx_obj, "overvoltage", payload->RX.RX_internal.overvoltage);
        cJSON_AddBoolToObject(rx_obj, "fully_charged", payload->RX.RX_internal.FullyCharged);
        
        cJSON_AddItemToObject(root, "rx", rx_obj);
    }

    // Convert to string
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to print JSON");
        return NULL;
    }

    return json_string;
}

/*******************************************************
 *                MQTT Publishing Functions
 *******************************************************/

/**
 * @brief Build topic string for a given peer and data type
 */
static void build_topic(char *topic_buf, size_t buf_len, 
                       uint8_t node_id, const char *data_type)
{
    snprintf(topic_buf, buf_len, "%s/%d/%s", 
             baseTopic, node_id, data_type);
}

/**
 * @brief Check if minimum publish interval has elapsed
 */
static bool should_publish_by_time(uint32_t last_publish_time)
{    
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t elapsed = current_time - last_publish_time;
    
    return elapsed >= MQTT_MIN_PUBLISH_INTERVAL_MS;
}

/**
 * @brief Publish JSON data to MQTT topic
 */
static esp_err_t publish_json_data(const char *topic, const char *json_string)
{
    if (!mqtt_connected || mqtt_client == NULL) {
        return ESP_FAIL;
    }
    
    if (!json_string) {
        ESP_LOGE(TAG, "Null JSON string");
        return ESP_FAIL;
    }
    
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, 
                                         json_string, 0,  // 0 = auto-calculate length
                                         1, 0);            // QoS 1, not retained
    
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to publish to topic: %s", topic);
        return ESP_FAIL;
    }
    
    //ESP_LOGD(TAG, "Published to %s (msg_id=%d)", topic, msg_id);
    return ESP_OK;
}

/*******************************************************
 *                TX Peer Publishing
 *******************************************************/
static void publish_peer_data(struct TX_peer *peer)
{
    char topic[128];
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Publish DYNAMIC payload
    if (dynamic_payload_changed(peer->dynamic_payload, peer->previous_dynamic_payload) ||
        should_publish_by_time(peer->lastDynamicPublished))
    {
        char *json_string = dynamic_payload_to_json(peer->dynamic_payload, peer->id);
        if (json_string) {
            build_topic(topic, sizeof(topic), peer->id, dynamicTopic);
            
            if (publish_json_data(topic, json_string) == ESP_OK) 
            {
                *peer->previous_dynamic_payload = *peer->dynamic_payload;
                peer->lastDynamicPublished = current_time;
                ESP_LOGI(TAG, "Published TX-%d dynamic: %s", peer->id, json_string);
            }
            
            cJSON_free(json_string);  // Free the JSON string
        }
    }
    
    // Publish ALERT payload (only when alerts are active)
    if (alert_payload_changed(peer->alert_payload, peer->previous_alert_payload))
    {
        char *json_string = alert_payload_to_json(peer->alert_payload, peer->id);
        if (json_string) {
            build_topic(topic, sizeof(topic), peer->id, alertTopic);
            
            if (publish_json_data(topic, json_string) == ESP_OK) 
            {
                *peer->previous_alert_payload = *peer->alert_payload;
                ESP_LOGW(TAG, "Published TX-%d ALERT: %s", peer->id, json_string);
            }
            
            cJSON_free(json_string);  // Free the JSON string
        }
    }    
}

/*******************************************************
 *                MQTT Publishing Task
 *******************************************************/

static void mqtt_publish_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MQTT publish task started");
    
    while (1)
    {
        if (mqtt_connected & is_root_node)
        {
            // Iterate through all TX peers
            struct TX_peer *tx_peer;

            WITH_TX_PEERS_LOCKED {
                SLIST_FOREACH(tx_peer, &TX_peers, next) {
                    update_status(tx_peer);
                    publish_peer_data(tx_peer);
                }
            }
        }
        //todo diconnect other nodes if root changes

        // Check every 1 second
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    vTaskDelete(NULL);
}

/*******************************************************
 *                MQTT Event Handler
 *******************************************************/

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            mqtt_connected = true;
            esp_mqtt_client_subscribe(mqtt_client, controlTopic, 1); // subscribe to control
            publish_json_data(controlTopic, "0"); // reset control button
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
            mqtt_connected = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR %d", event->error_handle->error_type);
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Last errno: %d", event->error_handle->esp_transport_sock_errno);
            }
            break;

        case MQTT_EVENT_DATA:
            //ESP_LOGI(TAG, "MQTT_EVENT_DATA received");
            //ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
            //ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);

            if (strncmp(event->topic, controlTopic, event->topic_len) == 0)
            {
                if (strncmp(event->data, "1", event->data_len) == 0) {
                    ESP_LOGW(TAG, "Switch system ON - Dashboard command!");
                    write_STM_command(TX_LOCALIZATION);
                    //todo: also send ON to all connected pads? localization messed up!
                } 
                else if (strncmp(event->data, "0", event->data_len) == 0)
                {
                    ESP_LOGW(TAG, "Switch system OFF - Dashboard command!");
                    write_STM_command(TX_OFF);
                    previousTX_pos = 0; //reset localization process
                    //todo: also send OFF to all connected pads
                }
            }
            break;
            
        default:
            ESP_LOGW(TAG, "MQTT event: %d", event_id);
            break;
    }
}

/*******************************************************
 *                Public API
 *******************************************************/

esp_err_t mqtt_client_manager_init(void)
{
    //ESP_LOGI(TAG, "Initializing MQTT client manager");
    
    // Configure MQTT client
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .network.reconnect_timeout_ms = MQTT_RECONNECT_INTERVAL_MS,
        .network.timeout_ms = 30000,
        .buffer.size = 4096,        // Increased for JSON
        .buffer.out_size = 4096,    // Increased for JSON
    };
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    
    // Register event handler
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, 
                                                    mqtt_event_handler, NULL));
    
    // Start MQTT client
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));
    
    // Create publishing task
    BaseType_t ret = xTaskCreate(mqtt_publish_task, "mqtt_publish", 
                                 10000, NULL, 7, &mqtt_publish_task_handle);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MQTT publish task");
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "MQTT client manager initialized successfully");
    return ESP_OK;
}

esp_err_t mqtt_client_manager_stop(void)
{
    if (mqtt_publish_task_handle != NULL) {
        vTaskDelete(mqtt_publish_task_handle);
        mqtt_publish_task_handle = NULL;
    }
    
    if (mqtt_client != NULL) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }
    
    mqtt_connected = false;
    
    ESP_LOGI(TAG, "MQTT client manager stopped");
    return ESP_OK;
}

bool mqtt_client_is_connected(void)
{
    return mqtt_connected;
}