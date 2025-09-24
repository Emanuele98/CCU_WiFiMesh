#include "wifiMesh.h"
#include "peer.h"

static const char *TAG = "MAIN";

void app_main(void)
{
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
    //todo - detect tx or rx and get readings

    /* Initialize WiFi Mesh */
    wifi_mesh_init();

    //local mesh formation done!
    //todo: master on any node (peer s-link structure) - make 2 lists
    /* Init peers */
    //peer_init();


    //todo: localization
    //todo: sensor monitoring

    //todo later: mqtt + aws management

    //todo later: visualization on web app
}