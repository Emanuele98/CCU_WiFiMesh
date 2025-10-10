#include "wifiMesh.h"
#include "peer.h"

static const char *TAG = "MAIN";

void print_firmware_version(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  %s", app_desc->project_name);
    ESP_LOGI(TAG, "  Firmware Version: v%d.%d.%d", 
             CONFIG_FW_VERSION_MAJOR, 
             CONFIG_FW_VERSION_MINOR, 
             CONFIG_FW_VERSION_PATCH);
    ESP_LOGI(TAG, "  Build Date: %s %s", app_desc->date, app_desc->time);
    ESP_LOGI(TAG, "  ESP-IDF: %s", app_desc->idf_ver);
    ESP_LOGI(TAG, "========================================");
}

void app_main(void)
{
    //! Default
    UNIT_ROLE = RX;

    print_firmware_version();

    esp_log_level_set("*", ESP_LOG_INFO);

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    /* Init values on NVS */ 
    init_NVS();

    /* Initialize I2C component */
    ESP_ERROR_CHECK(i2c_master_init());
    
    /* I2C scan to detect TX or RX */
    /*
    i2c_scan_bus();
    if (i2c_device_present(T1_SENSOR_ADDR) || i2c_device_present(T2_SENSOR_ADDR)) {
        UNIT_ROLE = RX;
        ESP_LOGI(TAG, "RX unit detected via I2C scan");
    }
    else {
        UNIT_ROLE = TX;
        ESP_LOGI(TAG, "TX unit detected via I2C scan");
    }
    */

    //Create group event 
    eventGroupHandle = xEventGroupCreate();

    /* Initialize WiFi Mesh */
    wifi_mesh_init();

    /* Wait until mesh is formed */
    xEventGroupWaitBits(eventGroupHandle, MESH_FORMEDBIT, pdTRUE, pdFALSE, portMAX_DELAY);

    /* Initialize Hardware*/
    init_HW();

    //local mesh formation done!
    //master on any node (peer s-link structure) - make 2 lists
    //static payload done

    //TX and RX join the mesh first - one TX elected as master
    //static payload from both - connect mac address to peer  (static msg initiated by the peer upon joining the mesh)
    //after that localization (one RX without position - dynamic payload only if it changes or every minute)
    //RX starts sending espnow broadcast data (TX receives it and if it is switched on replies with espnow unicast and binds the peer)
    //TX inform master <-> master inform TX (double check)
    //sensor monitoring 

    // handle localization process 
        //1. sequential switching of TX (with delay) 
        //2. rx broadcast esp-now to signal rx voltage (LOC table shared)

        //? need RX struct al all? everything is grouped inside TX. No master-RX communication.

    //todo later: mqtt + aws management

    //todo later: visualization on web app
}