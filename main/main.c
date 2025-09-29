#include "wifiMesh.h"
#include "peer.h"
#if CONFIG_TX_UNIT
    #include "aux_ctu_hw.h"
#else
    #include "cru_hw.h"
#endif

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

    /* Initialize Hardware */
    /*
    #if CONFIG_TX_UNIT
        TX_init_hw();
    #else
        RX_init_hw();
    #endif
    */

    /* Initialize WiFi Mesh */
    wifi_mesh_init();

    //local mesh formation done!
    //master on any node (peer s-link structure) - make 2 lists
    //static payload done


    //todo: localization - add espnow

    //todo: sensor monitoring

    //todo later: mqtt + aws management

    //todo later: visualization on web app
}