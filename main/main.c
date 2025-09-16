#include "wifiMesh.h"
#include "peer.h"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    wifi_mesh_init();

    //todo: local mesh formation
    //todo: master on any node (peer s-link structure)
    //todo: localization
    //todo: sensor monitoring

    //todo later: mqtt + aws management

    //todo later: visualization on web app
}