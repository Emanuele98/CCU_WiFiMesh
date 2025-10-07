#ifndef __CTU_HW_H__
#define __CTU_HW_H__

#include "util.h"
#include "peer.h"

#include "driver/gpio.h"
#include "leds.h"
#include "cJSON.h"
#include "driver/uart.h"

#define UART_BUFFER_SIZE                1024

#define TXD_PIN                         (GPIO_NUM_4)
#define RXD_PIN                         (GPIO_NUM_5)

#define EX_UART_NUM                     UART_NUM_0
#define UART_TASK_STACK_SIZE            1024*10
#define UART_TASK_PRIORITY              6

#define MIN_DUTY_CYCLE_CHANGE           0.005

typedef enum 
{
    SWITCH_OFF,
    SWITCH_LOC,
    SWITCH_ON,
} stm32_command_t;

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
esp_err_t write_STM_command(stm32_command_t command);

/**
 * @brief Send the ALERTS limits to the STM32
*/
esp_err_t write_STM_limits();

#endif