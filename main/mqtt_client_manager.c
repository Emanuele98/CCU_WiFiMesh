#include "include/mqtt_client_manager.h"

static const char *TAG = "MQTT_CLIENT";

/* MQTT client handle */
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;

/* Task handle */
static TaskHandle_t mqtt_publish_task_handle = NULL;

/*******************************************************
 *                Helper Functions
 *******************************************************/

/**
 * @brief Build topic string for a given peer and data type
 */
static void build_topic(char *topic_buf, size_t buf_len, peer_type type, 
                       uint8_t node_id, const char *data_type)
{
    const char *type_str = (type == TX) ? "tx" : "rx";
    snprintf(topic_buf, buf_len, "%s/%s/%d/%s", 
             MQTT_TOPIC_BASE, type_str, node_id, data_type);
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
 * @brief Publish binary data to MQTT topic
 */
static esp_err_t publish_binary_data(const char *topic, const void *data, 
                                     size_t data_len)
{
    if (!mqtt_connected || mqtt_client == NULL) {
        return ESP_FAIL;
    }
    
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, 
                                         (const char *)data, data_len, 
                                         1, 0); // QoS 1, not retained
    
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to publish to topic: %s", topic);
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "Published %d bytes to %s (msg_id=%d)", data_len, topic, msg_id);
    return ESP_OK;
}

/*******************************************************
 *                TX Peer Publishing
 *******************************************************/

static void publish_tx_peer_data(struct TX_peer *peer)
{
    char topic[128];
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Publish DYNAMIC payload
    if (dynamic_payload_changed(peer->dynamic_payload, peer->previous_dynamic_payload) ||
        should_publish_by_time(peer->lastDynamicPublished))
    {
        build_topic(topic, sizeof(topic), TX, peer->id, "dynamic");
        
        if (publish_binary_data(topic, peer->dynamic_payload, 
                               sizeof(mesh_dynamic_payload_t)) == ESP_OK)
        {
            *peer->previous_dynamic_payload = *peer->dynamic_payload;
            peer->lastDynamicPublished = current_time;
            //ESP_LOGI(TAG, "Published TX%d dynamic payload (V:%.2f, I:%.2f)", 
            //         peer->id, peer->dynamic_payload->TX.voltage, 
            //         peer->dynamic_payload->TX.current);
        }
    }
    
    //todo interrupt
    /*
    // Publish ALERT payload // todo interrupt? or maybe not for monitoring?
    if (alert_payload_changed(peer->alert_payload, peer->alert_payload))
    {
        build_topic(topic, sizeof(topic), TX, peer->id, "alerts");
        
        if (publish_binary_data(topic, peer->alert_payload, 
                               sizeof(mesh_alert_payload_t)) == ESP_OK)
        {
            peer->previous_dynamic_payload = peer->alert_payload;
            
            if (peer->alert_payload->TX.TX_all_flags || peer->alert_payload->RX.RX_all_flags) {
                ESP_LOGW(TAG, "Published TX%d ALERT payload!", peer->id);
            }
        }
    }    
        */
}

/*******************************************************
 *                RX Peer Publishing
 *******************************************************/

static void publish_rx_peer_data(struct RX_peer *peer)
{
    char topic[128];
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Publish DYNAMIC payload
    if (dynamic_payload_changed(peer->dynamic_payload, peer->previous_dynamic_payload) ||
        should_publish_by_time(peer->lastDynamicPublished))
    {
        build_topic(topic, sizeof(topic), RX, peer->id, "dynamic");
        
        if (publish_binary_data(topic, peer->dynamic_payload, 
                               sizeof(mesh_dynamic_payload_t)) == ESP_OK)
        {
            *peer->previous_dynamic_payload = *peer->dynamic_payload;
            peer->lastDynamicPublished = current_time;
            //ESP_LOGI(TAG, "Published RX%d dynamic payload (V:%.2f, I:%.2f)", 
            //         peer->id, peer->dynamic_payload->RX.voltage, 
            //         peer->dynamic_payload->RX.current);
        }
    }
    
    // todo interrupt
    /*
    // Publish ALERT payload
    if (alert_payload_changed(peer->alert_payload, peer->previous_al))
    {
        build_topic(topic, sizeof(topic), RX, peer->id, "alerts");
        
        if (publish_binary_data(topic, peer->alert_payload, 
                               sizeof(mesh_alert_payload_t)) == ESP_OK)
        {
            tracker->last_alert = *peer->alert_payload;
            tracker->last_alert_publish_time = current_time;
            tracker->alert_published_once = true;
            
            if (peer->alert_payload->RX.RX_all_flags) {
                ESP_LOGW(TAG, "Published RX%d ALERT payload!", peer->id);
            }
        }
    }
        */
}

/*******************************************************
 *                MQTT Publishing Task
 *******************************************************/

static void mqtt_publish_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MQTT publish task started");
    
    while (1)
    {
        if (mqtt_connected)
        {
            // Iterate through all TX peers
            struct TX_peer *tx_peer;
                        
            if (xSemaphoreTake(TX_peers_mutex, portMAX_DELAY) == pdTRUE) {
                SLIST_FOREACH(tx_peer, &TX_peers, next) {
                        publish_tx_peer_data(tx_peer);
                }
                xSemaphoreGive(TX_peers_mutex);
            }
            
            
            // Iterate through all RX peers
            struct RX_peer *rx_peer;

            if (xSemaphoreTake(RX_peers_mutex, portMAX_DELAY) == pdTRUE) {
                SLIST_FOREACH(rx_peer, &RX_peers, next) {
                    publish_rx_peer_data(rx_peer);
                }
                xSemaphoreGive(RX_peers_mutex);
            }
        }
        
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
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
            mqtt_connected = false;
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
            
        default:
            ESP_LOGD(TAG, "MQTT event: %d", event_id);
            break;
    }
}

/*******************************************************
 *                Public API
 *******************************************************/

esp_err_t mqtt_client_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing MQTT client manager");
    
    // Configure MQTT client
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .network.reconnect_timeout_ms = MQTT_RECONNECT_INTERVAL_MS,
        .network.timeout_ms = 10000,
        .buffer.size = 2048,
        .buffer.out_size = 2048,
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
                                 4096, NULL, 5, &mqtt_publish_task_handle);
    
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