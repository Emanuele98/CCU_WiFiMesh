#ifndef __CTU_HW_H__
#define __CTU_HW_H__

#include "util.h"
#include "peer.h"

#include "driver/gpio.h"
#include "leds.h"
#include "cJSON.h"
#include "driver/uart.h"

/** Simulate POWER */
#define GPIO_OUTPUT_PIN    GPIO_NUM_10

#define UART_BUFFER_SIZE                1024

#define TXD_PIN                         (GPIO_NUM_4)
#define RXD_PIN                         (GPIO_NUM_5)

#define EX_UART_NUM                     UART_NUM_1
#define UART_TASK_STACK_SIZE            1024*10
#define UART_TASK_PRIORITY              3

#define MIN_DUTY_CYCLE_CHANGE           0.005

// Helper macro to safely get JSON values
#define SAFE_GET_DOUBLE(obj, key, dest, default_val) \
    do { \
        cJSON *item = cJSON_GetObjectItem(obj, key); \
        if (item != NULL && cJSON_IsNumber(item)) { \
            dest = item->valuedouble; \
        } else { \
            ESP_LOGW(TAG, "Missing or invalid JSON field: %s", key); \
            dest = default_val; \
        } \
    } while(0)

#define SAFE_GET_INT(obj, key, dest, default_val) \
    do { \
        cJSON *item = cJSON_GetObjectItem(obj, key); \
        if (item != NULL && cJSON_IsNumber(item)) { \
            dest = item->valueint; \
        } else { \
            ESP_LOGW(TAG, "Missing or invalid JSON field: %s", key); \
            dest = default_val; \
        } \
    } while(0)

typedef enum 
{
    NONE,
    OV,
    OC,
    OT,
    HS,
    DC,
} alertType_t;

/**
 * @brief Initialize the hardware
*/
void TX_init_hw();

/**
 * @brief Send switch ON command to the STM32
*/
esp_err_t write_STM_command(TX_status command);

/**
 * @brief Send the ALERTS limits to the STM32
*/
esp_err_t write_STM_limits();

#endif