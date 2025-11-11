#ifndef UTIL_H
#define UTIL_H

#include "unitID.h"

#include <string.h>
#include <inttypes.h>
#include <math.h>
#include "nvs_flash.h"
#include <sys/queue.h>
#include <sys/time.h>
#include "driver/i2c.h"

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
#include "esp_now.h"
#include "esp_crc.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "esp_bridge.h"
#include "esp_mesh_lite.h"

/* MISALIGNMENT LIMITS */
#define SCOOTER_LEFT_LIMIT                  10
#define MISALIGNED_LIMIT                    30 //10

/* ALERTS LIMITS TX */
#define OVERCURRENT_TX                      2.2
#define OVERVOLTAGE_TX                      80
#define OVERTEMPERATURE_TX                  50
#define FOD_ACTIVE                          1

/* ALERTS LIMITS RX */
#define OVERCURRENT_RX                      2 //5
#define OVERVOLTAGE_RX                      100  //150
#define OVERTEMPERATURE_RX                  60
#define MIN_RX_VOLTAGE                      40  //60

/* LOC TIMING */
#define LOCALIZATION_TIME_MS                50     //milliseconds

/* DYNAMIC PAYLOAD MAX TIMING */
#define PEER_DYNAMIC_TIMER                  15      //15s

/* ESPNOW MESSAGES */
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

/* I2C CONFIGURATION */
#define I2C_MASTER_SCL_IO                   19                   /*!< gpio number for I2C master clock */
#define I2C_MASTER_SDA_IO                   18                   /*!< gpio number for I2C master data  */

#define I2C_MASTER_NUM                      I2C_NUM_0            /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ                  400000               /*!< I2C master clock frequency */

#define V_A_SENSOR_ADDR                     0x40  /*!< slave address for voltage and current sensor */
#define T1_SENSOR_ADDR                      0x48  /*!< slave address for first temperature sensor */
#define T2_SENSOR_ADDR                      0x49  /*!< slave address for second temperature sensor */
#define V_REGISTER_ADDR                     0x02  /* bus voltage register address */
#define A_REGISTER_ADDR                     0x01  /* shunt register address */
#define T_REGISTER_ADDR                     0x00  /* temperature register address */

#define WRITE_BIT                           I2C_MASTER_WRITE     /*!< I2C master write */
#define READ_BIT                            I2C_MASTER_READ      /*!< I2C master read */
#define ACK_CHECK_EN                        0x1                  /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS                       0x0                  /*!< I2C master will not check ack from slave */
#define ACK_VAL                             0x0                  /*!< I2C ack value */
#define NACK_VAL                            0x1                  /*!< I2C nack value */

#define MESH_FORMEDBIT                      BIT0
#define LOCALIZEDBIT                        BIT1

extern bool is_root_node;

extern bool internalFWTEST;

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

extern bool rxLocalized;
extern uint8_t DynTimeout;
extern EventGroupHandle_t eventGroupHandle;

//Self-MAC address
extern uint8_t self_mac[ETH_HWADDR_LEN];

// Localization variable
extern int8_t previousTX_pos;

/** Status variable */
typedef enum {
    TX_OFF,                //when the pad is off
    TX_LOCALIZATION,       //when the pad is active for localization (soft duty cycle thresholds) 
    TX_DEPLOY,             //when the pad is active on deploy (hard duty cycle thresholds)
    TX_FULLY_CHARGED,      //when the pad is off but a fully charged scooter is still present on it
    TX_ALERT               //when the pad sent an alert (overcurrent, overvoltage, overtemperature, FOD)
} TX_status;

/**
 * @brief Init the values on NVS - This allows to keep track of each peer's minimum reconnection time over reboots.
 * 
 */
void init_NVS(void);

/**
 * @brief Scan the I2C bus and print all found devices
 * 
 */
void i2c_scan_bus(void);

/**
 * @brief Check if an I2C device is present at the given address
 * @param device_addr 7-bit I2C device address
 * @return true if device is present, false otherwise
 */
bool i2c_device_present(uint8_t device_addr);

/**
 * @brief Initialize I2C master
 */
esp_err_t i2c_master_init(void);

#endif // UTIL_H