#ifndef MQTT_CLIENT_MANAGER_H
#define MQTT_CLIENT_MANAGER_H

#include "util.h"
#include "peer.h"
#include "cJSON.h"
#include "wifiMesh.h"
#include "ota_manager.h"

// MQTT Broker Settings
#define MQTT_BROKER_HOST "15.188.29.195"
#define MQTT_BROKER_PORT 8883
#define MQTT_USERNAME "bumblebee"
#define MQTT_PASSWORD "bumblebee2025"

/* Publishing intervals */
#define MQTT_MIN_PUBLISH_INTERVAL_MS    30000                // 30s
#define MQTT_RECONNECT_INTERVAL_MS      10000                // 20s

/**
 * @brief Initialize MQTT client and start publishing task (root node only)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_client_manager_init(void);

/**
 * @brief Stop MQTT client and clean up resources
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_client_manager_stop(void);

/**
 * @brief Get MQTT connection status
 * 
 * @return true if connected
 * @return false if disconnected
 */
bool mqtt_client_is_connected(void);

/**
 * @brief Publish control JSON data to MQTT broker
 * 
 * @param json_string JSON string to publish
 */
void publish_json_data_control(const char *json_string);

#endif /* MQTT_CLIENT_MANAGER_H */