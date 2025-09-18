idf_component_register(SRCS "main.c" "wifiMesh.c" "peer.c" "util.c"
                    PRIV_REQUIRES esp_wifi esp_driver_gpio esp_driver_ledc nvs_flash esp_timer
                    INCLUDE_DIRS "." "include")
