#ifndef MQTT_CLIENT_MANAGER_H
#define MQTT_CLIENT_MANAGER_H

#include "util.h"
#include "peer.h"
#include "mqtt_client.h"

/* MQTT broker URI */
#define MQTT_BROKER_URI                 "mqtt://15.188.29.195:1883"

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

#endif /* MQTT_CLIENT_MANAGER_H */