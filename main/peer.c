#include "peer.h"

static const char *TAG = "PEER";

struct RX_peer_list RX_peers = SLIST_HEAD_INITIALIZER(RX_peers);
struct TX_peer_list TX_peers = SLIST_HEAD_INITIALIZER(TX_peers);

SemaphoreHandle_t RX_peers_mutex = NULL;
SemaphoreHandle_t TX_peers_mutex = NULL;

mesh_static_payload_t self_static_payload = {0};
mesh_dynamic_payload_t self_dynamic_payload = {0};
mesh_alert_payload_t self_alert_payload = {0};
mesh_tuning_params_t self_tuning_params = {0};

// self structure for comparison of locally shared data
mesh_dynamic_payload_t self_previous_dynamic_payload = {0};
mesh_alert_payload_t self_previous_alert_payload = {0};

peer_type UNIT_ROLE;

void init_HW()
{
    if (UNIT_ROLE == TX) 
    {
        OVER_CURRENT = OVERCURRENT_TX;
        OVER_TEMPERATURE = OVERTEMPERATURE_TX;
        OVER_VOLTAGE = OVERVOLTAGE_TX;
        FOD = FOD_ACTIVE;
        FULLY_CHARGED = false;
        TX_init_hw();
    }
    else
    {
        OVER_CURRENT = OVERCURRENT_RX;
        OVER_TEMPERATURE = OVERTEMPERATURE_RX;
        OVER_VOLTAGE = OVERVOLTAGE_RX;
        FOD = FOD_ACTIVE;
        FULLY_CHARGED = false;
        RX_init_hw();
    }

    /* //Testing
    // print sensor readings
    while(1)
    {
        if (UNIT_ROLE == TX) 
        {
            ESP_LOGI(TAG, "TX HW readings \n -- %.2fV, %.2fA, %.2fC, %.2fC",
                    self_dynamic_payload.TX.voltage, self_dynamic_payload.TX.current, self_dynamic_payload.TX.temp1, self_dynamic_payload.TX.temp2);
        }
        else
        {
            ESP_LOGI(TAG, "RX HW readings \n -- %.2fV, %.2fA, %.2fC, %.2fC",
                self_dynamic_payload.RX.voltage, self_dynamic_payload.RX.current, self_dynamic_payload.RX.temp1, self_dynamic_payload.RX.temp2);
        }
        vTaskDelay(2000);
    }
    */
}

void allLocalizationTxPeersOFF()
{
    struct TX_peer *p;
    if (xSemaphoreTake(TX_peers_mutex, portMAX_DELAY) == pdTRUE) {
        SLIST_FOREACH(p, &TX_peers, next) {
            if (p->tx_status == TX_LOCALIZATION) {
                p->tx_status = TX_OFF;
            }
        }
        xSemaphoreGive(TX_peers_mutex);
    }
}

struct TX_peer* find_next_TX_for_localization(int8_t previousTX_pos) {
    struct TX_peer *p;
    struct TX_peer *first_available = NULL;
    bool found_previous = (previousTX_pos == -1); // Start from beginning if no previous
    
    // Iterate through the list sequentially
    if (xSemaphoreTake(TX_peers_mutex, portMAX_DELAY) == pdTRUE) {

        SLIST_FOREACH(p, &TX_peers, next) {
            if (p->tx_status == TX_OFF || p->tx_status == TX_LOCALIZATION) {
                // Store the first available TX for wrap-around
                if (first_available == NULL) {
                    first_available = p;
                }
                
                // If we've already passed the previous TX, return this one
                if (found_previous) {
                    xSemaphoreGive(TX_peers_mutex);
                    return p;
                }
            }
            
            // Mark when we've found and passed the previous TX
            if (p->position == previousTX_pos) {
                found_previous = true;
            }
        }
        xSemaphoreGive(TX_peers_mutex);
    }
    
    // Wrap around: return the first available TX in the list
    return first_available;
}

struct RX_peer* findRXpeerWPosition(uint8_t pos)
{
    struct RX_peer * p = NULL;

    if (xSemaphoreTake(RX_peers_mutex, portMAX_DELAY) == pdTRUE) {
        SLIST_FOREACH(p, &RX_peers, next) 
        {
            if (p->position == pos)
                break;
        }
        xSemaphoreGive(RX_peers_mutex);
    }
    return p;
}


void peer_init()
{
    RX_peers_mutex = xSemaphoreCreateMutex();
    TX_peers_mutex = xSemaphoreCreateMutex();

    assert(RX_peers_mutex);
    assert(TX_peers_mutex);
    
    SLIST_INIT(&RX_peers);
    SLIST_INIT(&TX_peers);

    delete_all_peers();

    struct TX_peer *p = TX_peer_add(self_mac);
    if (p != NULL) 
    {
        ESP_LOGI(TAG, "Added self TX peer with ID %d", CONFIG_UNIT_ID);
        p->id = CONFIG_UNIT_ID;
        p->position = p->id; // Position same as ID for TX

        // Free the malloc'd memory first to avoid leak
        free(p->static_payload);
        free(p->dynamic_payload);
        free(p->previous_dynamic_payload);
        free(p->alert_payload);
        free(p->tuning_params);

        p->static_payload = &self_static_payload;
        p->dynamic_payload = &self_dynamic_payload;
        p->previous_dynamic_payload = &self_previous_dynamic_payload;
        p->alert_payload = &self_alert_payload;
        p->tuning_params = &self_tuning_params;
    }
}

struct TX_peer* TX_peer_add(uint8_t *mac)
{
    struct TX_peer *p;

    /*Make sure the peer does not exist in the memory pool yet*/
    p = TX_peer_find_by_mac(mac);
    if (p) 
    {
        ESP_LOGE(TAG, "Peer already exists in the memory pool");
        return p;
    }

    /*Allocate memory for the peer*/
    p = malloc(sizeof * p);
    if (!p) 
    {
        ESP_LOGE(TAG, "Failed to allocate memory for peer");
        return NULL;
    }
    
    /* Set everything to 0 */
    memset(p, 0, sizeof * p);

    /* Allocate memory for payloads */
    p->static_payload = malloc(sizeof(mesh_static_payload_t));
    p->dynamic_payload = malloc(sizeof(mesh_dynamic_payload_t));
    p->previous_dynamic_payload = malloc(sizeof(mesh_dynamic_payload_t));
    p->alert_payload = malloc(sizeof(mesh_alert_payload_t));
    p->tuning_params = malloc(sizeof(mesh_tuning_params_t));

    if (!p->static_payload || !p->dynamic_payload || !p->previous_dynamic_payload ||
        !p->alert_payload || !p->tuning_params) {
        ESP_LOGE(TAG, "Failed to allocate memory for payloads");
        // Free any allocated memory
        free(p->static_payload);
        free(p->dynamic_payload);
        free(p->previous_dynamic_payload);
        free(p->alert_payload);
        free(p->tuning_params);
        free(p);
        return NULL;
    }

    memset(p->static_payload, 0, sizeof(mesh_static_payload_t));
    memset(p->dynamic_payload, 0, sizeof(mesh_dynamic_payload_t));
    memset(p->previous_dynamic_payload, 0, sizeof(mesh_dynamic_payload_t));
    memset(p->alert_payload, 0, sizeof(mesh_alert_payload_t));
    memset(p->tuning_params, 0, sizeof(mesh_tuning_params_t));

    /* Initialize peer parameters */
    memcpy(p->MACaddress, mac, 6);
    p->tx_status = TX_OFF;
    p->led_command = LED_CONNECTED;

    /* Add the peer to the list */
    if (xSemaphoreTake(TX_peers_mutex, portMAX_DELAY) == pdTRUE) {
        SLIST_INSERT_HEAD(&TX_peers, p, next);
        xSemaphoreGive(TX_peers_mutex);
    }

    return p;
}

struct RX_peer* RX_peer_add(uint8_t *mac)
{
    struct RX_peer *p;

    /*Make sure the peer does not exist in the memory pool yet*/
    p = RX_peer_find_by_mac(mac);
    if (p) 
    {
        ESP_LOGE(TAG, "Peer already exists in the memory pool");
        return p;
    }

    /*Allocate memory for the peer*/
    p = malloc(sizeof * p);
    if (!p) 
    {
        ESP_LOGE(TAG, "Failed to allocate memory for peer");
        return NULL;
    }

    /* Set everything to 0 */
    memset(p, 0, sizeof * p);

    /* Allocate memory for payloads */
    p->static_payload = malloc(sizeof(mesh_static_payload_t));
    p->dynamic_payload = malloc(sizeof(mesh_dynamic_payload_t));
    p->previous_dynamic_payload = malloc(sizeof(mesh_dynamic_payload_t));
    p->alert_payload = malloc(sizeof(mesh_alert_payload_t));

    if (!p->static_payload || !p->dynamic_payload || !p->previous_dynamic_payload ||
        !p->alert_payload) {
        ESP_LOGE(TAG, "Failed to allocate memory for payloads");
        // Free any allocated memory
        free(p->static_payload);
        free(p->dynamic_payload);
        free(p->previous_dynamic_payload);
        free(p->alert_payload);
        free(p);
        return NULL;
    }

    /* Set payloads to 0 */
    memset(p->static_payload, 0, sizeof(mesh_static_payload_t));
    memset(p->dynamic_payload, 0, sizeof(mesh_dynamic_payload_t));
    memset(p->previous_dynamic_payload, 0, sizeof(mesh_dynamic_payload_t));
    memset(p->alert_payload, 0, sizeof(mesh_alert_payload_t));

    /* Initialize peer parameters */
    memcpy(p->MACaddress, mac, 6);
    p->RX_status = RX_CONNECTED;
    p->position = -1; //not assigned yet

    /* Add the peer to the list */
    if (xSemaphoreTake(RX_peers_mutex, portMAX_DELAY) == pdTRUE) {
        SLIST_INSERT_HEAD(&RX_peers, p, next);
        xSemaphoreGive(RX_peers_mutex);
    }

    return p;
}

struct TX_peer* TX_peer_find_by_mac(uint8_t *mac)
{
    struct TX_peer *p = NULL;

    if (xSemaphoreTake(TX_peers_mutex, portMAX_DELAY) == pdTRUE) {
        SLIST_FOREACH(p, &TX_peers, next) {
            if (memcmp(p->MACaddress, mac, 6) == 0) 
                break;
        }
        xSemaphoreGive(TX_peers_mutex);
    }

    return p;
}

struct RX_peer* RX_peer_find_by_mac(uint8_t *mac)
{
    struct RX_peer *p = NULL;

    if (xSemaphoreTake(RX_peers_mutex, portMAX_DELAY) == pdTRUE) {
        SLIST_FOREACH(p, &RX_peers, next) {
            if (memcmp(p->MACaddress, mac, 6) == 0)
                break;
        }
        xSemaphoreGive(RX_peers_mutex);
    }
    return p;
}

struct TX_peer* TX_peer_find_by_position(uint8_t position)
{
    struct TX_peer *p = NULL;

    if (xSemaphoreTake(TX_peers_mutex, portMAX_DELAY) == pdTRUE) {
        SLIST_FOREACH(p, &TX_peers, next) {
            if (p->position == position)
                break;
        }
        xSemaphoreGive(TX_peers_mutex);
    }

    return p;
}

struct RX_peer* RX_peer_find_by_position(int8_t position)
{
    struct RX_peer *p = NULL;

    if (xSemaphoreTake(RX_peers_mutex, portMAX_DELAY) == pdTRUE) {
        SLIST_FOREACH(p, &RX_peers, next) {
            if (p->position == position)
                break;
        }
        xSemaphoreGive(RX_peers_mutex);
    }

    return p;
}

void peer_delete(uint8_t *mac)
{
    // Don't delete self peer
    if (memcmp(mac, self_mac, 6) == 0) {
        ESP_LOGW(TAG, "Cannot delete self peer");
        return;
    }

    struct TX_peer *TX_p = TX_peer_find_by_mac(mac);
    if(TX_p != NULL)
    {
        if (xSemaphoreTake(TX_peers_mutex, portMAX_DELAY) == pdTRUE) {
            SLIST_REMOVE(&TX_peers, TX_p, TX_peer, next);
            xSemaphoreGive(TX_peers_mutex);
        }
        // Free payload memory
        free(TX_p->static_payload);
        free(TX_p->dynamic_payload);
        free(TX_p->previous_dynamic_payload);
        free(TX_p->alert_payload);
        free(TX_p->tuning_params);
        // Free peer structure
        free(TX_p);
        return;
    }

    struct RX_peer *RX_p = RX_peer_find_by_mac(mac);
    if(RX_p != NULL)
    {
        if (xSemaphoreTake(RX_peers_mutex, portMAX_DELAY) == pdTRUE) {
            SLIST_REMOVE(&RX_peers, RX_p, RX_peer, next);
            xSemaphoreGive(RX_peers_mutex);
        }
        // Free payload memory
        free(RX_p->static_payload);
        free(RX_p->dynamic_payload);
        free(RX_p->previous_dynamic_payload);
        free(RX_p->alert_payload);
        // Free peer structure
        free(RX_p);
        return;
    }
}

void delete_all_peers(void)
{
    struct TX_peer *TX_p;
    struct RX_peer *RX_p;

    if (xSemaphoreTake(TX_peers_mutex, portMAX_DELAY) == pdTRUE) {
        while (!SLIST_EMPTY(&TX_peers)) {
            TX_p = SLIST_FIRST(&TX_peers);
            SLIST_REMOVE_HEAD(&TX_peers, next);

            if (memcmp(TX_p->MACaddress, self_mac, 6) != 0) { //don't free self peer
                free(TX_p->static_payload);
                free(TX_p->dynamic_payload);
                free(TX_p->previous_dynamic_payload);
                free(TX_p->alert_payload);
                free(TX_p->tuning_params);
            }
            // Free peer structure
            free(TX_p);
        }
        xSemaphoreGive(TX_peers_mutex);
    }

    if (xSemaphoreTake(RX_peers_mutex, portMAX_DELAY) == pdTRUE) {
        while (!SLIST_EMPTY(&RX_peers)) {
            RX_p = SLIST_FIRST(&RX_peers);
            SLIST_REMOVE_HEAD(&RX_peers, next);
            // Free payload memory
            free(RX_p->static_payload);
            free(RX_p->dynamic_payload);
            free(RX_p->previous_dynamic_payload);
            free(RX_p->alert_payload);
            // Free peer structure
            free(RX_p);
        }
        xSemaphoreGive(RX_peers_mutex);
    }
}

bool atLeastOneRxNeedLocalization()
{
    bool result = false;

    //check both RX and TX structs are not empty
    if (xSemaphoreTake(RX_peers_mutex, portMAX_DELAY) == pdTRUE) {
        if (SLIST_EMPTY(&RX_peers)) {
            xSemaphoreGive(RX_peers_mutex);
            return false;
        }
        xSemaphoreGive(RX_peers_mutex);
    }

    if (xSemaphoreTake(TX_peers_mutex, portMAX_DELAY) == pdTRUE) {
        if (SLIST_EMPTY(&TX_peers)) {
            xSemaphoreGive(TX_peers_mutex);
            return false;
        }
        xSemaphoreGive(TX_peers_mutex);
    }

    struct RX_peer *RX_p;
    struct TX_peer *TX_p;

    if (xSemaphoreTake(RX_peers_mutex, portMAX_DELAY) == pdTRUE) {
            if (xSemaphoreTake(TX_peers_mutex, portMAX_DELAY) == pdTRUE) {
                
                SLIST_FOREACH(RX_p, &RX_peers, next) 
                {
                    if (RX_p->position == -1) // not localized yet
                    {
                        SLIST_FOREACH(TX_p, &TX_peers, next) 
                        {
                            if (TX_p->tx_status == TX_OFF || TX_p->tx_status == TX_LOCALIZATION)
                            {
                                result = true;
                                goto cleanup;  // Exit both loops
                            }
                        }
                    }
                }

    cleanup:
                xSemaphoreGive(TX_peers_mutex);  // Always release
                xSemaphoreGive(RX_peers_mutex);  // Always release
            } else {
                xSemaphoreGive(RX_peers_mutex);
            }
        }


    return result;
}

bool dynamic_payload_changed(mesh_dynamic_payload_t *current, 
                                    mesh_dynamic_payload_t *previous)
{
    bool res = false;

    if (fabs(current->TX.voltage - previous->TX.voltage) > DELTA_VOLTAGE) 
    {
        res = true;
        //ESP_LOGW(TAG, "Voltage change detected: %.2f %.2f V", current->TX.voltage, previous->TX.voltage);
    }
    if (fabs(current->TX.current - previous->TX.current) > DELTA_CURRENT) 
    {
        res = true;
        //ESP_LOGW(TAG, "Current change detected: %.2f %.2f A", current->TX.current, previous->TX.current);
    }
    if (fabs(current->TX.temp1 - previous->TX.temp1) > DELTA_TEMPERATURE) 
    {
        res = true;
        //ESP_LOGW(TAG, "Temp1 change detected: %.2f %.2f C", current->TX.temp1, previous->TX.temp1);
    }
    if (fabs(current->TX.temp2 - previous->TX.temp2) > DELTA_TEMPERATURE) 
    {
        res = true;
        //ESP_LOGW(TAG, "Temp2 change detected: %.2f %.2f C", current->TX.temp2, previous->TX.temp2);
    }
    if (fabs(current->RX.voltage - previous->RX.voltage) > DELTA_VOLTAGE) 
    {
        res = true;
        //ESP_LOGW(TAG, "Voltage change detected: %.2f %.2f V", current->RX.voltage, previous->RX.voltage);
    }
    if (fabs(current->RX.current - previous->RX.current) > DELTA_CURRENT) 
    {
        res = true;
        //ESP_LOGW(TAG, "Current change detected: %.2f %.2f A", current->RX.current, previous->RX.current);
    }
    if (fabs(current->RX.temp1 - previous->RX.temp1) > DELTA_TEMPERATURE) 
    {
        res = true;
        //ESP_LOGW(TAG, "Temp1 change detected: %.2f %.2f C", current->RX.temp1, previous->RX.temp1);
    }
    if (fabs(current->RX.temp2 - previous->RX.temp2) > DELTA_TEMPERATURE) 
    {
        res = true;
        //ESP_LOGW(TAG, "Temp2 change detected: %.2f %.2f C", current->RX.temp2, previous->RX.temp2);
    }

    return res;
}

bool alert_payload_changed(mesh_alert_payload_t *current, 
                                  mesh_alert_payload_t *previous)
{
    bool res = (memcmp(current, previous, sizeof(mesh_alert_payload_t)) != 0);

    return res;
}

void init_payloads()
{
    //Init payloads
    memset(&self_static_payload, 0, sizeof(self_static_payload));
    memset(&self_dynamic_payload, 0, sizeof(self_dynamic_payload));
    memset(&self_alert_payload, 0, sizeof(self_alert_payload));
    memset(&self_tuning_params, 0, sizeof(self_tuning_params));

    memcpy(self_static_payload.macAddr, self_mac, ETH_HWADDR_LEN);
    memcpy(self_dynamic_payload.TX.macAddr, self_mac, ETH_HWADDR_LEN);
    memcpy(self_alert_payload.TX.macAddr, self_mac, ETH_HWADDR_LEN);
    memcpy(self_tuning_params.macAddr, self_mac, ETH_HWADDR_LEN);

    //Define static payload
    self_static_payload.id = CONFIG_UNIT_ID;
    self_static_payload.type = UNIT_ROLE;
    if (UNIT_ROLE == TX)
    {
        self_static_payload.OVERVOLTAGE_limit = OVERVOLTAGE_TX;
        self_static_payload.OVERCURRENT_limit = OVERCURRENT_TX;
        self_static_payload.OVERTEMPERATURE_limit = OVERTEMPERATURE_TX;
        self_static_payload.FOD = FOD_ACTIVE;
    }
    else
    {
        self_static_payload.OVERVOLTAGE_limit = OVERVOLTAGE_RX;
        self_static_payload.OVERCURRENT_limit = OVERCURRENT_RX;
        self_static_payload.OVERTEMPERATURE_limit = OVERTEMPERATURE_RX;
        self_static_payload.FOD = FOD_ACTIVE;
    }
}

