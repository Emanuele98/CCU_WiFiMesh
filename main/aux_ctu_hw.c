#include "driver/gpio.h"
#include "include/aux_ctu_hw.h"

static QueueHandle_t uart0_queue;

TimerHandle_t connected_leds_timer, misaligned_leds_timer, charging_leds_timer, hw_readings_timer;

static float last_duty_cycle = 0.30;

stm32_command_t powerStatus = SWITCH_OFF;

static const char* TAG = "HARDWARE";

static void parse_received_UART(uint8_t *rx_uart)
{
    // Parse json
    cJSON *root = cJSON_Parse((char*)rx_uart);
    
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON Parse Error before: %s", error_ptr);
        } else {
            ESP_LOGE(TAG, "JSON Parse Error: Invalid JSON");
        }
        ESP_LOGE(TAG, "Received data: %s", rx_uart);
        return;  // Don't crash, just skip this packet
    }
    
    // Safely extract values
    SAFE_GET_DOUBLE(root, "temperature1", self_dynamic_payload.TX.temp1, 0.0);
    SAFE_GET_DOUBLE(root, "temperature2", self_dynamic_payload.TX.temp2, 0.0);
    SAFE_GET_DOUBLE(root, "voltage", self_dynamic_payload.TX.voltage, 0.0);
    SAFE_GET_DOUBLE(root, "current", self_dynamic_payload.TX.current, 0.0);
    
    alertType_t alertType = NONE;
    SAFE_GET_INT(root, "alert", alertType, NONE);
    
    // Handle alert
    if (alertType == OV)
        self_alert_payload.TX.TX_internal.overvoltage = 1;
    else if (alertType == OC)
        self_alert_payload.TX.TX_internal.overcurrent = 1;
    else if (alertType == OT)
        self_alert_payload.TX.TX_internal.overtemperature = 1;
    else if ((alertType == HS) || (alertType == DC))
        self_alert_payload.TX.TX_internal.FOD = 1;

    if (self_alert_payload.TX.TX_all_flags) {
        ESP_LOGE(TAG, "ALERT: %d", alertType);
        write_STM_command(SWITCH_OFF);
    }

    // Get tuning parameters
    float duty_cycle = 0.0;
    SAFE_GET_DOUBLE(root, "duty", duty_cycle, 0.0);
    self_tuning_params.duty_cycle = duty_cycle;
    
    int tuning = 0;
    SAFE_GET_INT(root, "tuning", tuning, 0);
    self_tuning_params.tuning = tuning;
    
    int low_vds_threshold = 0;
    SAFE_GET_INT(root, "low_vds_threshold", low_vds_threshold, 0);
    self_tuning_params.low_vds_threshold = low_vds_threshold;
    
    int low_vds = 0;
    SAFE_GET_INT(root, "low_vds", low_vds, 0);
    self_tuning_params.low_vds = low_vds;

    if (fabs(self_tuning_params.duty_cycle - last_duty_cycle) > MIN_DUTY_CYCLE_CHANGE) {
        //send_tuning_message();
        last_duty_cycle = self_tuning_params.duty_cycle;
    }

    //print json
    /*
    char *out = cJSON_Print(root);
    ESP_LOGI(TAG, "JSON: %s", out);
    free(out);
    */

    // Free json
    cJSON_Delete(root);
}

static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    for(;;) {
        if(xQueueReceive(uart0_queue, (void * )&event, (TickType_t)portMAX_DELAY)) {
            switch(event.type) {
                case UART_DATA:
                    // Normal data - handled by rx_task
                    break;
                    
                case UART_FIFO_OVF:
                    ESP_LOGE(TAG, "UART FIFO overflow - flushing");
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;
                    
                case UART_BUFFER_FULL:
                    ESP_LOGE(TAG, "UART buffer full - flushing");
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;
                    
                case UART_BREAK:
                    ESP_LOGW(TAG, "UART break detected - flushing");
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;
                    
                case UART_PARITY_ERR:
                    ESP_LOGE(TAG, "UART parity error - flushing");
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;
                    
                case UART_FRAME_ERR:
                    ESP_LOGE(TAG, "UART frame error - flushing");
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;
                    
                case UART_PATTERN_DET:
                    ESP_LOGD(TAG, "UART pattern detected");
                    break;
                    
                default:
                    ESP_LOGW(TAG, "Unknown UART event: %d", event.type);
                    break;
            }
        }
    }
    vTaskDelete(NULL);
}

static void rx_task(void *pvParameters)
{
    uint8_t buffer[UART_BUFFER_SIZE];
    uint16_t rxIndex = 0;
    uint8_t* data = (uint8_t*) malloc(UART_BUFFER_SIZE/2);
    bool json = false;
    
    if (data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate rx_task buffer");
        vTaskDelete(NULL);
        return;
    }
    
    while (1) {
        const int rxBytes = uart_read_bytes(EX_UART_NUM, data, 1, portMAX_DELAY);
        if (rxBytes > 0) {
            switch(data[0])
            {
                case '{':
                    json = true;
                    rxIndex = 0;
                    memset(buffer, 0, UART_BUFFER_SIZE);  // Clear buffer
                    buffer[rxIndex++] = data[0];
                    break;
                    
                case '}':
                    if (rxIndex < UART_BUFFER_SIZE - 1) {  // Bounds check
                        buffer[rxIndex++] = data[0];
                        buffer[rxIndex] = '\0';  // NULL terminate

                        // LOG THE RAW DATA BEFORE PARSING
                        //ESP_LOGI(TAG, "=== RAW JSON ===");
                        //ESP_LOGI(TAG, "%s", buffer);
                        //ESP_LOGI(TAG, "=== END (length: %d) ===", rxIndex);
                        
                        if (json && rxIndex > 2) {  // At least "{}"
                            // Validate it looks like JSON before parsing
                            ESP_LOGD(TAG, "Received JSON (%d bytes): %s", rxIndex, buffer);
                            parse_received_UART(buffer);
                        } else {
                            ESP_LOGW(TAG, "Invalid JSON packet (length: %d)", rxIndex);
                        }
                    } else {
                        ESP_LOGE(TAG, "Buffer overflow prevented (rxIndex: %d)", rxIndex);
                    }
                    
                    // Reset state
                    rxIndex = 0;
                    json = false;
                    memset(buffer, 0, UART_BUFFER_SIZE);
                    break;
                    
                default:
                    if (json) {
                        if (rxIndex < UART_BUFFER_SIZE - 1) {  // Leave room for null terminator
                            buffer[rxIndex++] = data[0];
                        } else {
                            ESP_LOGE(TAG, "Buffer overflow - discarding packet");
                            rxIndex = 0;
                            json = false;
                            memset(buffer, 0, UART_BUFFER_SIZE);
                        }
                    }
                    // Ignore data outside of JSON packets
                    break;
            }
        }
    }
    free(data);
}

esp_err_t write_STM_limits()
{
    //create json file
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "overcurrent", OVER_CURRENT);
    cJSON_AddNumberToObject(root, "overvoltage", OVER_VOLTAGE);
    cJSON_AddNumberToObject(root, "overtemperature", OVER_TEMPERATURE);

    char *my_json_string = cJSON_Print(root);
    const uint8_t len = strlen(my_json_string);

    const uint8_t txBytes = uart_write_bytes(EX_UART_NUM, my_json_string, len);

    cJSON_Delete(root);
    free(my_json_string);

    if (txBytes > 0)
        return ESP_OK;
    else
        return ESP_FAIL;
}

esp_err_t write_STM_command(stm32_command_t command)
{
    /*
    //create json file
    cJSON *root = cJSON_CreateObject();
    if (command == SWITCH_ON)
        cJSON_AddStringToObject(root, "mode", "deploy");
    else if (command == SWITCH_LOC)
        cJSON_AddStringToObject(root, "mode", "localization");
    else if (command == SWITCH_OFF)
        cJSON_AddStringToObject(root, "mode", "off");

    char *my_json_string = cJSON_Print(root);
    const uint8_t len = strlen(my_json_string);

    const uint8_t txBytes = uart_write_bytes(EX_UART_NUM, my_json_string, len);

    cJSON_Delete(root);
    free(my_json_string);
    
    if (txBytes > 0)
        return ESP_OK;
    else
        return ESP_FAIL;
    */

    //Simulate POWER
    if (command == SWITCH_ON)
    {
        gpio_set_level(GPIO_OUTPUT_PIN, 1);
        powerStatus = SWITCH_ON;
    }
    else if (command == SWITCH_OFF)
    {
        //ESP_LOGW(TAG, "OFF");
        gpio_set_level(GPIO_OUTPUT_PIN, 0);
        powerStatus = SWITCH_OFF;
    }
    else if (command == SWITCH_LOC)
    {
        //ESP_LOGW(TAG, "ON");
        gpio_set_level(GPIO_OUTPUT_PIN, 1); //no localization mode for now
        powerStatus = SWITCH_LOC;
    }

    return ESP_OK;
}

void uart_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    // Install driver with lower interrupt priority
    uart_driver_install(EX_UART_NUM, UART_BUFFER_SIZE * 2, UART_BUFFER_SIZE * 2, 
                       20, &uart0_queue, ESP_INTR_FLAG_LEVEL1);
    uart_param_config(EX_UART_NUM, &uart_config);
    uart_set_pin(EX_UART_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    
    // Optional: Disable pattern detection to avoid spurious interrupts
    uart_disable_pattern_det_intr(EX_UART_NUM);
    uart_flush(EX_UART_NUM);
    
    ESP_LOGI(TAG, "UART%d initialized on TX:%d RX:%d", EX_UART_NUM, TXD_PIN, RXD_PIN);
}

void TX_init_hw()
{
    //*LED STRIP
    install_strip(STRIP_PIN);
    ESP_LOGI(TAG, "LED strip initialized successfully");

    // LED strip timers
    connected_leds_timer = xTimerCreate("connected_leds", CONNECTED_LEDS_TIMER_PERIOD, pdTRUE, NULL, connected_leds);   
    misaligned_leds_timer = xTimerCreate("misaligned_leds", MISALIGNED_LEDS_TIMER_PERIOD, pdTRUE, NULL, misaligned_leds);
    charging_leds_timer = xTimerCreate("charging leds", CHARGING_LEDS_TIMER_PERIOD, pdTRUE, NULL, charging_state);

    if ( (connected_leds_timer == NULL) || (misaligned_leds_timer == NULL) || (charging_leds_timer == NULL))
    {
        ESP_LOGW(TAG, "Timers were not created successfully");
        return;
    }

    xTimerStart(connected_leds_timer, 10);
    xTimerStart(misaligned_leds_timer, 10);
    xTimerStart(charging_leds_timer, 10);
    
    
    //*UART CONNECTION TO STM32
    // the STM32 board sends sensor measurements to the ESP32 via UART, along with FOD alerts and other information
	uart_init();
	xTaskCreate(uart_event_task, "uart_event_task", UART_TASK_STACK_SIZE, NULL, UART_TASK_PRIORITY, NULL);
    xTaskCreate(rx_task, "uart_rx_task", UART_TASK_STACK_SIZE, NULL, UART_TASK_PRIORITY, NULL);

    // safely switch off
    ESP_ERROR_CHECK(write_STM_command(SWITCH_OFF));
    ESP_ERROR_CHECK(write_STM_limits());

    /** Simulate POWER */
    // Reset and set GPIO as output
    gpio_reset_pin(GPIO_OUTPUT_PIN);
    gpio_set_direction(GPIO_OUTPUT_PIN, GPIO_MODE_INPUT_OUTPUT);
}
