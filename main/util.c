#include "util.h"

static const char *TAG = "UTIL";

time_t now;

time_t reconnection_time = 0;
time_t timePeer[CONFIG_MESH_ROUTE_TABLE_SIZE] = {0};
nvs_handle_t my_handle;

SemaphoreHandle_t i2c_sem;

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

            for (uint8_t i = 0; i < CONFIG_MESH_ROUTE_TABLE_SIZE; i++)
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