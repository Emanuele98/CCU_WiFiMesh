#include "util.h"

static const char *TAG = "UTIL";

uint8_t UNIT_ID = 0; // Default unit ID, will be set from NVS

bool is_root_node = false;

bool internalFWTEST = false;

time_t now;

time_t reconnection_time = 0;
time_t timePeer[MESH_LITE_MAXIMUM_NODE_NUMBER] = {0};
nvs_handle_t my_handle;

SemaphoreHandle_t i2c_sem;

bool rxLocalized = false;
uint8_t DynTimeout = PEER_DYNAMIC_TIMER;
EventGroupHandle_t eventGroupHandle;

uint8_t self_mac[ETH_HWADDR_LEN] = {0};

// Localization variable
int8_t previousTX_pos = 0;

//* Global Alerts variables
float OVER_CURRENT;
float OVER_TEMPERATURE;
float OVER_VOLTAGE;
bool FOD;
bool FULLY_CHARGED;

/*
void init_NVS(void)
{
    //NVS reading
    esp_err_t err = nvs_open("reconnection", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else 
        {
            // save timePeer after connection to NTP Server
            time(&now);
            time(&reconnection_time);

            for (uint8_t i = 0; i < MESH_LITE_MAXIMUM_NODE_NUMBER; i++)
            {
                char peer_name[5];
                sprintf(peer_name, "%d", i+1);
                err = nvs_get_i64(my_handle, peer_name, &timePeer[i]);
                if (err == ESP_ERR_NVS_NOT_FOUND)
                {   
                    timePeer[i] = now;
                    nvs_set_i64(my_handle, peer_name, timePeer[i]);
                }
            }

            err = nvs_commit(my_handle);
            if (!err)
                ESP_LOGI(TAG, "NVS INIT DONE");
            else
                ESP_LOGE(TAG, "NVS INIT FAILED");
            
            // Close
            nvs_close(my_handle);
        }
}
*/

void i2c_scan_bus(void)
{
    printf("Scanning I2C bus...\n");
    uint8_t devices_found = 0;
    
    for (uint8_t addr = 1; addr < 127; addr++)
    {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | WRITE_BIT, ACK_CHECK_EN);
        i2c_master_stop(cmd);
        
        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 50 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
        
        if (ret == ESP_OK)
        {
            printf("Device found at address 0x%02X\n", addr);
            devices_found++;
        }
    }
    
    printf("Scan complete. Found %d device(s).\n", devices_found);
}


bool i2c_device_present(uint8_t device_addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device_addr << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 50 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    
    return (ret == ESP_OK);
}

/**
 * @brief Initialize I2C master
 */
esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK)
    {
        return err;
    }
    
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}