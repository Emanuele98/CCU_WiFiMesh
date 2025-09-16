#ifndef _WIFIMESH_H_
#define _WIFIMESH_H_

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

#define UNIT_ROLE CONFIG_UNIT_ROLE //TODO USE THIS TO DISTINGUISH BETWEEN TX AND RX UNIT

/* MISALIGNMENT LIMITS */
#define SCOOTER_LEFT_LIMIT                  10
#define MISALIGNED_LIMIT                    50

/* ALERTS LIMITS TX */
#define OVERCURRENT_TX                      2.2
#define OVERVOLTAGE_TX                      80
#define OVERTEMPERATURE_TX                  50
#define FOD_ACTIVE                          1

/* ALERTS LIMITS RX */
#define OVERCURRENT_RX                      2
#define OVERVOLTAGE_RX                      100
#define OVERTEMPERATURE_RX                  60
#define MIN_VOLTAGE                         50

/* LOC TIMING */
#define REACTION_TIME                       1000    //milliseconds
#define BATON_PASS_TIMEOUT                  10000   //milliseconds
#define UART_TX_DELAY                       100     //milliseconds

/* DYNAMIC PAYLOAD TIMING */
#define PEER_DYNAMIC_TIMER                  1000    //milliseconds

/* ESPNOW MESSAGES */
#define LOC_START_MESSAGE                   0x10
#define LOC_STOP_MESSAGE                    0x01
#define ACCELEROMETER_MESSAGE               0x02
#define ALERT_MESSAGE                       0x99

/* RECONNECTION TIMING */
#define TX_FOD_TIMEOUT                      60*2     //2 minutes
#define TX_OVERTEMPERATURE_TIMEOUT          60*2     //2 minutes
#define TX_OVERVOLTAGE_TIMEOUT              60*2     //2 minutes
#define TX_OVERCURRENT_TIMEOUT              60*2     //2 minutes
#define RX_OVERTEMPERATURE_TIMEOUT          60*2     //2 minutes
#define RX_OVERVOLTAGE_TIMEOUT              60*2     //2 minutes
#define RX_OVERCURRENT_TIMEOUT              60*2     //2 minutes

/* FOD_CLEAR_TIMEOUT */
#define FOD_CLEAR_TIMEOUT                   4000     //2 seconds

typedef enum {
    ESPNOW_DATA_BROADCAST,
    ESPNOW_DATA_LOCALIZATION,
    ESPNOW_DATA_ALERT,
    ESPNOW_DATA_DYNAMIC,
    ESPNOW_DATA_CONTROL
} message_type;

typedef enum {
    LOCALIZATION_START,
    LOCALIZATION_CHECK,
    LOCALIZATION_STOP,
} localization_message_type;

/* ESP NOW PAYLOAD */
typedef struct { 
    uint8_t id;                           //Peer unit ID.
    uint8_t type;                         //Type of ESPNOW message.
    float field_1;                        
    float field_2;                        
    float field_3;                        
    float field_4;                        
} __attribute__((packed)) espnow_data_t;

/**
 * @brief Initialize the Wi-Fi mesh network.
 * 
 */
void wifi_mesh_init();

#endif /* _WIFIMESH_H_ */