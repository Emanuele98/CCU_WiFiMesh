#include "include/cru_hw.h"

#define ADC_UNIT                    ADC_UNIT_1

#if CONFIG_IDF_TARGET_ESP32
static adc_channel_t channel[2] = {ADC_CHANNEL_4, ADC_CHANNEL_5}; //voltage gpio 32 //current gpio 33
#else
static adc_channel_t channel[2] = {ADC_CHANNEL_2, ADC_CHANNEL_3}; //voltage gpio 2 //current gpio 3
#endif
static adc_atten_t atten[2] = {ADC_ATTEN_DB_12, ADC_ATTEN_DB_6};

static adc_oneshot_unit_handle_t adc_handle = NULL;  // Changed from continuous handle
static adc_cali_handle_t adc_cali_handle_1 = NULL, adc_cali_handle_2 = NULL;

static const char* TAG = "RX_HARDWARE";

/* Semaphore used to protect against I2C reading simultaneously */
static uint8_t counter = 0;

static void init_adc(void)
{   
    // Initialize oneshot unit
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    // Configure each channel
    adc_oneshot_chan_cfg_t config_ch0 = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = atten[0],  // DB_12
    };
    adc_oneshot_chan_cfg_t config_ch1 = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = atten[1],  // DB_6
    };
    
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, channel[0], &config_ch0));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, channel[1], &config_ch1));

    // Calibration setup (unchanged)
    adc_cali_curve_fitting_config_t cali_config_1 = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_curve_fitting_config_t cali_config_2 = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN_DB_6,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config_1, &adc_cali_handle_1));
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config_2, &adc_cali_handle_2));
}

static float i2c_read_temperature_sensor(bool n_temp_sens)
{
    int ret;
    uint8_t first_byte, second_byte;
    float value;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    if(n_temp_sens)
    {
        i2c_master_write_byte(cmd, T1_SENSOR_ADDR << 1 | WRITE_BIT, ACK_CHECK_EN);
    } else {
        i2c_master_write_byte(cmd, T2_SENSOR_ADDR << 1 | WRITE_BIT, ACK_CHECK_EN);
    }
    i2c_master_write_byte(cmd, T_REGISTER_ADDR, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    if (ret==ESP_OK)
    {
        cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        if(n_temp_sens)
        {
            i2c_master_write_byte(cmd, T1_SENSOR_ADDR << 1 | READ_BIT, ACK_CHECK_EN);
        } else {
            i2c_master_write_byte(cmd, T2_SENSOR_ADDR << 1 | READ_BIT, ACK_CHECK_EN);
        }    
        i2c_master_read_byte(cmd, &first_byte, ACK_VAL);
        i2c_master_read_byte(cmd, &second_byte, NACK_VAL);
        i2c_master_stop(cmd);
        ret = i2c_master_cmd_begin(0, cmd, 1000 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
    }

    if(ret!=ESP_OK)
    {
        value = -1;
        goto exit;
    }

    value = (int16_t)(first_byte << 4 | second_byte >> 4) * 0.0625;

    exit:
        xSemaphoreGive(i2c_sem);
        return value;
}

//read dynamic parameters from ADC sensors
static void get_adc(void *pvParameters)
{
    //ESP_LOGI(TAG, "ADC oneshot task started");

    // Averaging variables
    uint32_t ch2_sum = 0, ch3_sum = 0;
    uint32_t read_success = 0, read_errors = 0;

    while (1) 
    {
        int ch2_raw, ch3_raw;
        
        // Read both channels
        esp_err_t ret1 = adc_oneshot_read(adc_handle, channel[0], &ch2_raw);
        esp_err_t ret2 = adc_oneshot_read(adc_handle, channel[1], &ch3_raw);
        
        if (ret1 == ESP_OK && ret2 == ESP_OK) {
            read_success++;
            
            // Accumulate samples
            ch2_sum += ch2_raw;
            ch3_sum += ch3_raw;
            
            // Average every 50 samples
            if (read_success >= 50) {
                int ch2_avg = ch2_sum / read_success;
                int ch3_avg = ch3_sum / read_success;
                
                int ch2_voltage_mv, ch3_voltage_mv;
                esp_err_t err1 = adc_cali_raw_to_voltage(adc_cali_handle_1, ch2_avg, &ch2_voltage_mv);
                esp_err_t err2 = adc_cali_raw_to_voltage(adc_cali_handle_2, ch3_avg, &ch3_voltage_mv);
                
                if (err1 == ESP_OK && err2 == ESP_OK) {
                    // Update global payload
                    self_dynamic_payload.voltage = (float)(ch2_voltage_mv * 42/1000.00);
                    self_dynamic_payload.current = (float)(ch3_voltage_mv > 450 ? (ch3_voltage_mv - 400)/360.00 : 0);
                    
                    //ESP_LOGI(TAG, "Ch2: %.3fV --> Voltage: %.3f, Ch3: %.3fV --> Current: %.3f", 
                    //    ch2_voltage_mv/1000.0f, self_dynamic_payload.voltage, 
                    //    ch3_voltage_mv/1000.0f, self_dynamic_payload.current);
                }

                if (self_dynamic_payload.voltage > MIN_RX_VOLTAGE) {
                    xEventGroupSetBits(eventGroupHandle, BIT0);
                }
                
                // Log stats every 1000 cycles (~100 seconds at 1kHz effective)
                //if (read_success % 1000 == 0) {
                //    ESP_LOGI(TAG, "ADC stats - Success: %lu, Errors: %lu, Heap: %lu", 
                //             read_success, read_errors, esp_get_free_heap_size());
                //}
                
                // Reset counters
                ch2_sum = ch3_sum = read_success = 0;
            }
        } else {
            read_errors++;
            ESP_LOGW(TAG, "ADC read failed: ch0=%d, ch1=%d", ret1, ret2);
        }
        
        // Sample at ~1kHz (1ms delay) for equivalent to 5kHz/2 channels in continuous mode
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Cleanup (unreachable but good practice)
    adc_oneshot_del_unit(adc_handle);
    vTaskDelete(NULL); 
}

//read dynamic parameters from I2C sensors
static void get_temp(void *pvParameters)
{
    while (1)
    {
        float t1 = 0, t2 = 0;
        if (xSemaphoreTake(i2c_sem, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            switch(counter)
            {
                case 0:
                    counter++;
                    t1 = i2c_read_temperature_sensor(0);
                    if (t1 != -1)
                        self_dynamic_payload.temp1 = t1;
                    break;

                case 1:
                    counter = 0;
                    t2 = i2c_read_temperature_sensor(1);
                    if (t2 != -1)
                        self_dynamic_payload.temp2= t2;
                    break;
                default:
                    xSemaphoreGive(i2c_sem);
                    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelete(NULL); 
}

void RX_init_hw(void)
{
    /* Init adc */
    init_adc();
    ESP_LOGI(TAG, "ADC oneshot initialized");

    /* init I2C*/
    i2c_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(i2c_sem);
    ESP_LOGI(TAG, "I2C initialized - temperature sensors");

    uint8_t err;
    //err = xTaskCreate(get_temp, "get_temp", 4096, NULL, 5, NULL);
    //if ( err != pdPASS )
    //{
    //    ESP_LOGE(TAG, "Task get_temp was not created successfully");
    //    return;
    //}
    
    err = xTaskCreate(get_adc, "get_adc", 8192, NULL, 5, NULL);  // Priority 5 is fine
    if( err != pdPASS )
    {
        ESP_LOGE(TAG, "Task get_adc was not created successfully");
        return;
    }
}