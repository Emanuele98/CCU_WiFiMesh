#ifndef UTIL_H
#define UTIL_H

#include "unitID.h"

#include <string.h>
#include <inttypes.h>
#include <math.h>
#include "nvs_flash.h"
#include <sys/queue.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_mac.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "esp_bridge.h"
#include "esp_mesh_lite.h"

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

/* Mesh message IDs */
#define TO_ROOT_STATIC_MSG_ID               0x1000
#define TO_ROOT_STATIC_MSG_ID_RESP          0x1001

#define TO_ROOT_DYNAMIC_MSG_ID              0x1002
#define TO_CHILD_LOCALIZATION_MSG_ID        0x1003

//* Global Alerts variables
extern float OVER_CURRENT;
extern float OVER_TEMPERATURE;
extern float OVER_VOLTAGE;
extern bool FOD;
extern bool FULLY_CHARGED;

/* Semaphore used to protect against I2C reading simultaneously */
extern SemaphoreHandle_t i2c_sem;

// Global time variable
extern time_t now;

// Reconnection time variable
extern time_t reconnection_time;
extern time_t timePeer[MESH_LITE_MAXIMUM_NODE_NUMBER];
extern nvs_handle_t my_handle;

/**
 * @brief Init the values on NVS - This allows to keep track of each peer's minimum reconnection time over reboots.
 * 
 */
void init_NVS(void);

#endif // UTIL_H