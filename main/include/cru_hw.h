#ifndef __CRU_HW_H__
#define __CRU_HW_H__

#include "util.h"
#include "peer.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define FULLY_CHARGED_MIN_VOLTAGE       50
#define FULLY_CHARGED_MAX_CURRENT       0.2

/**
 * @brief Init I2C bus and sensors
 * 
 */
void RX_init_hw(void);


#endif