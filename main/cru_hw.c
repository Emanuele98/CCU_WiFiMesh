
#include "include/cru_hw.h"

#define ADC_UNIT                    ADC_UNIT_1
#define ADC_CONV_MODE               ADC_CONV_SINGLE_UNIT_1
#define ADC_ATTEN                   ADC_ATTEN_DB_0
#define ADC_BIT_WIDTH               SOC_ADC_DIGI_MAX_BITWIDTH

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
#define ADC_OUTPUT_TYPE             ADC_DIGI_OUTPUT_FORMAT_TYPE1
#define ADC_GET_CHANNEL(p_data)     ((p_data)->type1.channel)
#define ADC_GET_DATA(p_data)        ((p_data)->type1.data)
#else
#define ADC_OUTPUT_TYPE             ADC_DIGI_OUTPUT_FORMAT_TYPE2
#define ADC_GET_CHANNEL(p_data)     ((p_data)->type2.channel)
#define ADC_GET_DATA(p_data)        ((p_data)->type2.data)
#endif

#define READ_LEN                    64

#if CONFIG_IDF_TARGET_ESP32
static adc_channel_t channel[2] = {ADC_CHANNEL_4, ADC_CHANNEL_5}; //voltage gpio 32 //current gpio 33
#else
static adc_channel_t channel[2] = {ADC_CHANNEL_2, ADC_CHANNEL_3}; //voltage gpio 2 //current gpio 3
#endif
static adc_atten_t atten[2] = {ADC_ATTEN_DB_12, ADC_ATTEN_DB_6};

static adc_continuous_handle_t handle = NULL;
static adc_cali_handle_t adc_cali_handle_1 = NULL, adc_cali_handle_2 = NULL;

static const char* TAG = "RX_HARDWARE";

/* Semaphore used to protect against I2C reading simultaneously */
static uint8_t counter = 0;

static void init_adc(void)
{   
    uint8_t channel_num = sizeof(channel) / sizeof(adc_channel_t);

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 1024,
        .conv_frame_size = READ_LEN,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &handle));

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = 40 * 1000,
        .conv_mode = ADC_CONV_MODE,
        .format = ADC_OUTPUT_TYPE,
    };

    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
    dig_cfg.pattern_num = channel_num;
    for (int i = 0; i < channel_num; i++) {
        adc_pattern[i].atten = atten[i];
        adc_pattern[i].channel = channel[i] & 0x7;
        adc_pattern[i].unit = ADC_UNIT;
        adc_pattern[i].bit_width = ADC_BIT_WIDTH;
    }
    dig_cfg.adc_pattern = adc_pattern;

    ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));

    adc_cali_curve_fitting_config_t cali_config_1 = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
        adc_cali_curve_fitting_config_t cali_config_2 = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_6,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config_1, &adc_cali_handle_1));
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config_2, &adc_cali_handle_2));

    ESP_ERROR_CHECK(adc_continuous_start(handle));
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
        //ESP_LOGW(TAG, "temperature reading problem");
        value = -1;
        goto exit;
    }

    //printf("first byte: %02x\n", first_byte);
    //printf("second byte : %02x\n", second_byte);
    value = (int16_t)(first_byte << 4 | second_byte >> 4) * 0.0625 ;
    //printf("temperature %d: %.02f [C]\n", n_temp_sens + 1 ,value);

    exit:
        xSemaphoreGive(i2c_sem);
        return value;
}

//read dynamic parameters from ADC sensors
static void get_adc(void *pvParameters)
{
    esp_err_t ret;
    uint32_t ret_num = 0;
    uint8_t result[READ_LEN] = {0};
    memset(result, 0xcc, READ_LEN);

    // Averaging variables
    uint32_t ch2_sum = 0, ch3_sum = 0;
    uint16_t ch2_count = 0, ch3_count = 0;

    while (1) 
    {
        ret = adc_continuous_read(handle, result, READ_LEN, &ret_num, 100); //100 ms timeout
        if (ret == ESP_OK) 
        {
            // Accumulate samples (don't log each one!)
            for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
                adc_digi_output_data_t *p = (adc_digi_output_data_t*)&result[i];
                uint32_t chan_num = ADC_GET_CHANNEL(p);
                uint32_t data = ADC_GET_DATA(p);
                
                if (chan_num == channel[0]) {
                    ch2_sum += data;
                    ch2_count++;
                } else if (chan_num == channel[1]) {
                    ch3_sum += data;
                    ch3_count++;
                }
            }
            // Log averages every 100 samples per channel (~50ms at 40kHz)
            if (ch2_count >= 100 && ch3_count >= 100) {
                int ch2_raw = ch2_sum / ch2_count;
                int ch3_raw = ch3_sum / ch3_count;
                
                int ch2_voltage_mv, ch3_voltage_mv;
                esp_err_t err1 = adc_cali_raw_to_voltage(adc_cali_handle_1, ch2_raw, &ch2_voltage_mv);
                esp_err_t err2 = adc_cali_raw_to_voltage(adc_cali_handle_2, ch3_raw, &ch3_voltage_mv);
                
                if (err1 == ESP_OK && err2 == ESP_OK) {
                    // Update global payload if needed
                    dynamic_payload.voltage = (float)(ch2_voltage_mv * 42/1000.00);
                    dynamic_payload.current = (float)(ch3_voltage_mv > 450 ? (ch3_voltage_mv - 400)/360.00 : 0);
                    ESP_LOGI(TAG, "Ch2: %.3fV --> Voltage: %.3f, Ch3: %.3fV --> Current: %.3f", 
                        ch2_voltage_mv/1000.0f, dynamic_payload.voltage, ch3_voltage_mv/1000.0f, dynamic_payload.current);
                }

                if (dynamic_payload.voltage > MIN_RX_VOLTAGE) {
                    xEventGroupSetBits(eventGroupHandle, BIT0);
                }
                
                // Reset counters
                ch2_sum = ch3_sum = 0;
                ch2_count = ch3_count = 0;
            }

            taskYIELD();  // Give other tasks a chance
        } else if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "ADC TIMEOUT");
            vTaskDelay(1); //prevent tight loop
            continue;
        }
    }

    ESP_ERROR_CHECK(adc_continuous_stop(handle));
    ESP_ERROR_CHECK(adc_continuous_deinit(handle));
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
                //temperature 1
                case 0:
                    counter++;
                    t1 = i2c_read_temperature_sensor(0);
                    if (t1 != -1)
                        dynamic_payload.temp1 = t1;
                    break;

                //temperature 2
                case 1:
                    counter = 0;
                    t2 = i2c_read_temperature_sensor(1);
                    if (t2 != -1)
                        dynamic_payload.temp2= t2;
                    break;
                default:
                    xSemaphoreGive(i2c_sem);
                    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    //if for any reason the loop terminates
    vTaskDelete(NULL); 
}

void RX_init_hw(void)
{
    /* Init adc */
    init_adc();
    ESP_LOGI(TAG, "ADC initialized");

    /* init I2C*/
    /* Initialize I2C semaphore */
    i2c_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(i2c_sem);

    ESP_LOGI(TAG, "I2C initialized - temperature sensors");

    uint8_t err;
    // create tasks to get measurements as fast as possible
    //err = xTaskCreate(get_temp, "get_temp", 4096, NULL, 5, NULL);
    //if ( err != pdPASS )
    //{
    //    ESP_LOGE(TAG, "Task get_temp was not created successfully");
    //    return;
    //}
    err = xTaskCreate(get_adc, "get_adc", 10000, NULL, 6, NULL); 
    if( err != pdPASS )
    {
        ESP_LOGE(TAG, "Task get_ was not created successfully");
        return;
    }
}