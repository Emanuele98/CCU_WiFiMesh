#include "peer.h"

static const char *TAG = "PEER";

struct RX_peer_list RX_peers = SLIST_HEAD_INITIALIZER(RX_peers);
struct TX_peer_list TX_peers = SLIST_HEAD_INITIALIZER(TX_peers);

SemaphoreHandle_t RX_peers_mutex = NULL;
SemaphoreHandle_t TX_peers_mutex = NULL;

mesh_static_payload_t self_static_payload = {0};
mesh_dynamic_payload_t self_dynamic_payload = {0};
mesh_dynamic_payload_t self_previous_dynamic_payload = {0};
mesh_alert_payload_t self_alert_payload = {0};
mesh_alert_payload_t self_previous_alert_payload = {0};
mesh_tuning_params_t self_tuning_params = {0};

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

void peer_init()
{
    RX_peers_mutex = xSemaphoreCreateMutex();
    TX_peers_mutex = xSemaphoreCreateMutex();

    assert(RX_peers_mutex);
    assert(TX_peers_mutex);
    
    SLIST_INIT(&RX_peers);
    SLIST_INIT(&TX_peers);

    delete_all_peers();

    struct TX_peer *p = TX_peer_add(self_mac, CONFIG_UNIT_ID);
    if (p != NULL) 
    {
        ESP_LOGI(TAG, "Added self TX peer with ID %d", CONFIG_UNIT_ID);

        // Free the malloc'd memory first to avoid leak
        if (p->static_payload)
            free(p->static_payload);
        if (p->dynamic_payload)
            free(p->dynamic_payload);
        if (p->previous_dynamic_payload)
            free(p->previous_dynamic_payload);
        if (p->alert_payload)
            free(p->alert_payload);
        if (p->previous_alert_payload)
            free(p->previous_alert_payload);
        if (p->tuning_params)
            free(p->tuning_params);

        p->static_payload = &self_static_payload;
        p->dynamic_payload = &self_dynamic_payload;
        p->previous_dynamic_payload = &self_previous_dynamic_payload;
        p->alert_payload = &self_alert_payload;
        p->previous_alert_payload = &self_previous_alert_payload;
        p->tuning_params = &self_tuning_params;
    }
}

// =============================================================================
// MUTEX-PROTECTED OPERATIONS
// =============================================================================

void allLocalizationTxPeersOFF()
{
    WITH_TX_PEERS_LOCKED {
        struct TX_peer *p;
        SLIST_FOREACH(p, &TX_peers, next) {
            if (p->dynamic_payload->TX.tx_status == TX_LOCALIZATION) {
                p->dynamic_payload->TX.tx_status = TX_OFF;
            }
        }
    }
}

struct TX_peer* find_next_TX_for_localization(int8_t previousTX_pos) 
{
    struct TX_peer *result = NULL;
    struct TX_peer *first_available = NULL;
    bool found_previous = (!previousTX_pos);
    
    WITH_TX_PEERS_LOCKED {
        struct TX_peer *p;
        SLIST_FOREACH(p, &TX_peers, next) {
            if (p->dynamic_payload->TX.tx_status == TX_OFF || p->dynamic_payload->TX.tx_status == TX_LOCALIZATION) {
                if (first_available == NULL) {
                    first_available = p;
                }
                
                if (found_previous) {
                    result = p;
                    break;  //OK - breaks FOREACH, not the guard
                }
            }
            
            if (p->position == previousTX_pos) {
                found_previous = true;
            }
        }
        
        if (result == NULL) {
            result = first_available;  // Wrap around
        }
    }
    
    return result;  //Pointer - use immediately!
}

struct RX_peer* findRXpeerWPosition(uint8_t pos)
{
    struct RX_peer *result = NULL;

    WITH_RX_PEERS_LOCKED {
        struct RX_peer *p;
        SLIST_FOREACH(p, &RX_peers, next) 
        {
            if (p->position == pos) {
                result = p;
                break;
            }
        }
    }
    
    return result;
}

struct TX_peer* TX_peer_find_by_mac(uint8_t *mac)
{
    struct TX_peer *result = NULL;

    WITH_TX_PEERS_LOCKED {
        struct TX_peer *p;
        SLIST_FOREACH(p, &TX_peers, next) {
            if (memcmp(p->MACaddress, mac, 6) == 0) {
                result = p;
                break;
            }
        }
    }

    return result;
}

struct RX_peer* RX_peer_find_by_mac(uint8_t *mac)
{
    struct RX_peer *result = NULL;

    WITH_RX_PEERS_LOCKED {
        struct RX_peer *p;
        SLIST_FOREACH(p, &RX_peers, next) {
            if (memcmp(p->MACaddress, mac, ETH_HWADDR_LEN) == 0) {
                result = p;
                break;
            }
        }
    }
    
    return result;
}

struct TX_peer* TX_peer_find_by_position(uint8_t position)
{
    struct TX_peer *result = NULL;

    WITH_TX_PEERS_LOCKED {
        struct TX_peer *p;
        SLIST_FOREACH(p, &TX_peers, next) {
            if (p->position == position) {
                result = p;
                break;
            }
        }
    }

    return result;
}

struct RX_peer* RX_peer_find_by_position(int8_t position)
{
    struct RX_peer *result = NULL;

    WITH_RX_PEERS_LOCKED {
        struct RX_peer *p;
        SLIST_FOREACH(p, &RX_peers, next) {
            if (p->position == position) {
                result = p;
                break;
            }
        }
    }

    return result;
}

void removeFromRelativeTX(int8_t pos)
{
    // Atomic: Find and remove in one critical section
    WITH_TX_PEERS_LOCKED {
        struct TX_peer *p;
        SLIST_FOREACH(p, &TX_peers, next) {
            if (p->dynamic_payload->RX.id == pos) 
            {
                memset(p->dynamic_payload->RX.macAddr, 0, ETH_HWADDR_LEN);
                p->dynamic_payload->RX.rx_status = RX_NOT_PRESENT;
                p->dynamic_payload->RX.id = 0;
                p->dynamic_payload->RX.current = p->dynamic_payload->RX.voltage = p->dynamic_payload->RX.temp1 = p->dynamic_payload->RX.temp2 = 0;
                p->dynamic_payload->TX.tx_status = TX_OFF;

                //todo switch off! also - remove from espNOW how?
                break;
            }
        }
    }
    
}

// =============================================================================
// SAFE PEER DELETION - Atomic find and remove
// =============================================================================

void peer_delete(uint8_t *mac)
{
    // Don't delete self peer
    if (memcmp(mac, self_mac, 6) == 0) {
        ESP_LOGW(TAG, "Cannot delete self peer");
        return;
    }

    struct TX_peer *TX_p = NULL;
    
    // Atomic: Find and remove in one critical section
    WITH_TX_PEERS_LOCKED {
        struct TX_peer *p;
        if (!SLIST_EMPTY(&TX_peers)) {
            SLIST_FOREACH(p, &TX_peers, next) {
                if (memcmp(p->MACaddress, mac, ETH_HWADDR_LEN) == 0) {
                    SLIST_REMOVE(&TX_peers, p, TX_peer, next);
                    TX_p = p;  // Store for freeing outside lock
                    break;
                }
            }
        }
    }
    
    // Free outside mutex (peer already removed from list)
    if (TX_p != NULL) {
        // Defensive: Only free if not pointing to globals
        if (TX_p->static_payload && TX_p->static_payload != &self_static_payload) {
            free(TX_p->static_payload);
        }
        if (TX_p->dynamic_payload && TX_p->dynamic_payload != &self_dynamic_payload) {
            free(TX_p->dynamic_payload);
        }
        if (TX_p->previous_dynamic_payload && 
            TX_p->previous_dynamic_payload != &self_previous_dynamic_payload) {
            free(TX_p->previous_dynamic_payload);
        }
        if (TX_p->alert_payload && TX_p->alert_payload != &self_alert_payload) {
            free(TX_p->alert_payload);
        }
        if (TX_p->previous_alert_payload && 
            TX_p->previous_alert_payload != &self_previous_alert_payload) {
            free(TX_p->previous_alert_payload);
        }
        if (TX_p->tuning_params && TX_p->tuning_params != &self_tuning_params) {
            free(TX_p->tuning_params);
        }
        free(TX_p);
        ESP_LOGI(TAG, "Deleted TX peer "MACSTR, MAC2STR(mac));
        return;
    }

    // Try RX list
    struct RX_peer *RX_p = NULL;
    
    WITH_RX_PEERS_LOCKED {
        struct RX_peer *p;
            if (!SLIST_EMPTY(&RX_peers)) {
            SLIST_FOREACH(p, &RX_peers, next) {
                if (memcmp(p->MACaddress, mac, ETH_HWADDR_LEN) == 0) {
                    //remove from relative TX (if any)
                    removeFromRelativeTX(p->id);
                    SLIST_REMOVE(&RX_peers, p, RX_peer, next);
                    RX_p = p;
                    break;
                }
            }
        }
    }
    
    if (RX_p != NULL) {
        free(RX_p);
        ESP_LOGI(TAG, "Deleted RX peer "MACSTR, MAC2STR(mac));
    } else {
        ESP_LOGW(TAG, "Peer "MACSTR" not found for deletion", MAC2STR(mac));
    }
}

void delete_all_peers(void)
{
    struct TX_peer *TX_p;

    WITH_TX_PEERS_LOCKED {
        while (!SLIST_EMPTY(&TX_peers)) {
            TX_p = SLIST_FIRST(&TX_peers);
            SLIST_REMOVE_HEAD(&TX_peers, next);

            // Defensive: Only free if not pointing to globals
            if (TX_p->static_payload && TX_p->static_payload != &self_static_payload) {
                free(TX_p->static_payload);
            }
            if (TX_p->dynamic_payload && TX_p->dynamic_payload != &self_dynamic_payload) {
                free(TX_p->dynamic_payload);
            }
            if (TX_p->previous_dynamic_payload && 
                TX_p->previous_dynamic_payload != &self_previous_dynamic_payload) {
                free(TX_p->previous_dynamic_payload);
            }
            if (TX_p->alert_payload && TX_p->alert_payload != &self_alert_payload) {
                free(TX_p->alert_payload);
            }
            if (TX_p->previous_alert_payload && 
                TX_p->previous_alert_payload != &self_previous_alert_payload) {
                free(TX_p->previous_alert_payload);
            }
            if (TX_p->tuning_params && TX_p->tuning_params != &self_tuning_params) {
                free(TX_p->tuning_params);
            }
            free(TX_p);
        }
    }

    struct RX_peer *RX_p;
    
    WITH_RX_PEERS_LOCKED {
        while (!SLIST_EMPTY(&RX_peers)) {
            RX_p = SLIST_FIRST(&RX_peers);
            SLIST_REMOVE_HEAD(&RX_peers, next);
            free(RX_p);
        }
    }
}

bool atLeastOneRxNeedLocalization()
{
    bool result = false;

    WITH_BOTH_PEERS_LOCKED {
        // Check if either list is empty
        if (SLIST_EMPTY(&RX_peers) || SLIST_EMPTY(&TX_peers)) {
            result = false;
        } else {
            struct RX_peer *RX_p;
            SLIST_FOREACH(RX_p, &RX_peers, next) 
            {
                if (!RX_p->position)  // Not localized yet
                {
                    struct TX_peer *TX_p;
                    SLIST_FOREACH(TX_p, &TX_peers, next) 
                    {
                        if (TX_p->dynamic_payload->TX.tx_status == TX_OFF || 
                            TX_p->dynamic_payload->TX.tx_status == TX_LOCALIZATION)
                        {
                            result = true;
                            break;  // OK - breaks inner loop only
                        }
                    }
                    if (result) break;  // Break outer loop too
                }
            }
        }
    }

    return result;
}

// =============================================================================
// ADD FUNCTIONS - guard to list insertion
// =============================================================================

struct TX_peer* TX_peer_add(uint8_t *mac, uint8_t id)
{
    struct TX_peer *p;

    // Check if peer already exists
    p = TX_peer_find_by_mac(mac);
    if (p) 
    {
        ESP_LOGE(TAG, "Peer already exists in the memory pool");
        return p;
    }

    // Allocate memory for the peer (outside mutex - slow operation)
    p = malloc(sizeof * p);
    if (!p) 
    {
        ESP_LOGE(TAG, "Failed to allocate memory for peer");
        return NULL;
    }
    
    memset(p, 0, sizeof * p);

    // Allocate memory for payloads
    p->static_payload = malloc(sizeof(mesh_static_payload_t));
    p->dynamic_payload = malloc(sizeof(mesh_dynamic_payload_t));
    p->previous_dynamic_payload = malloc(sizeof(mesh_dynamic_payload_t));
    p->alert_payload = malloc(sizeof(mesh_alert_payload_t));
    p->previous_alert_payload = malloc(sizeof(mesh_alert_payload_t));
    p->tuning_params = malloc(sizeof(mesh_tuning_params_t));

    if (!p->static_payload || !p->dynamic_payload || !p->previous_dynamic_payload ||
        !p->alert_payload || !p->previous_alert_payload || !p->tuning_params) {
        ESP_LOGE(TAG, "Failed to allocate memory for payloads");
        // Clean up
        if (p->static_payload) free(p->static_payload);
        if (p->dynamic_payload) free(p->dynamic_payload);
        if (p->previous_dynamic_payload) free(p->previous_dynamic_payload);
        if (p->alert_payload) free(p->alert_payload);
        if (p->previous_alert_payload) free(p->previous_alert_payload);
        if (p->tuning_params) free(p->tuning_params);
        free(p);
        return NULL;
    }

    memset(p->static_payload, 0, sizeof(mesh_static_payload_t));
    memset(p->dynamic_payload, 0, sizeof(mesh_dynamic_payload_t));
    memset(p->previous_dynamic_payload, 0, sizeof(mesh_dynamic_payload_t));
    memset(p->alert_payload, 0, sizeof(mesh_alert_payload_t));
    memset(p->previous_alert_payload, 0, sizeof(mesh_alert_payload_t));
    memset(p->tuning_params, 0, sizeof(mesh_tuning_params_t));

    // Initialize peer parameters
    memcpy(p->MACaddress, mac, 6);
    memcpy(p->static_payload->macAddr, mac, ETH_HWADDR_LEN);
    memcpy(p->dynamic_payload->TX.macAddr, mac, ETH_HWADDR_LEN);
    memcpy(p->alert_payload->TX.macAddr, mac, ETH_HWADDR_LEN);
    memcpy(p->tuning_params->macAddr, mac, ETH_HWADDR_LEN);
    p->dynamic_payload->TX.tx_status = TX_OFF;
    p->static_payload->id = p->dynamic_payload->TX.id = p->alert_payload->TX.id = id;
    p->position = p->id = id; // Position same as ID for TX  
    *p->previous_dynamic_payload = *p->dynamic_payload;
    *p->previous_alert_payload = *p->alert_payload;

    struct TX_peer *existing;

    // Add to list - quick operation, hold mutex briefly
    WITH_TX_PEERS_LOCKED {
        // Double-check peer doesn't exist (defensive)
        SLIST_FOREACH(existing, &TX_peers, next) {
            if (memcmp(existing->MACaddress, mac, ETH_HWADDR_LEN) == 0) {
                // Race condition - another thread added it
                ESP_LOGW(TAG, "Peer was added by another thread, cleaning up");
                // Clean up our allocation (outside macro to avoid issues)
                goto cleanup_and_return_existing;
            }
        }
        
        SLIST_INSERT_HEAD(&TX_peers, p, next);
    }

    return p;

cleanup_and_return_existing:
    // Free our allocations
    free(p->static_payload);
    free(p->dynamic_payload);
    free(p->previous_dynamic_payload);
    free(p->alert_payload);
    free(p->previous_alert_payload);
    free(p->tuning_params);
    free(p);
    return existing;  // Return the one that won the race
}

struct RX_peer* RX_peer_add(uint8_t *mac, uint8_t id)
{
    struct RX_peer *p;

    // Check if peer already exists
    p = RX_peer_find_by_mac(mac);
    if (p) 
    {
        ESP_LOGE(TAG, "Peer already exists in the memory pool");
        return p;
    }

    // Allocate memory (outside mutex)
    p = malloc(sizeof * p);
    if (!p) 
    {
        ESP_LOGE(TAG, "Failed to allocate memory for peer");
        return NULL;
    }

    memset(p, 0, sizeof * p);

    memcpy(p->MACaddress, mac, 6);
    p->RX_status = RX_NOT_PRESENT;
    p->position = 0;
    p->id = id;

    struct RX_peer *existing;

    // Add to list
    WITH_RX_PEERS_LOCKED {
        // Double-check (defensive)
        SLIST_FOREACH(existing, &RX_peers, next) {
            if (memcmp(existing->MACaddress, mac, 6) == 0) {
                goto cleanup_and_return_existing_rx;
            }
        }
        
        SLIST_INSERT_HEAD(&RX_peers, p, next);
    }

    return p;

    cleanup_and_return_existing_rx:
    free(p);
    return existing;
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
    if (current->TX.tx_status != previous->TX.tx_status || current->RX.rx_status != previous->RX.rx_status)
        res = true;

    return res;
}

bool alert_payload_changed(mesh_alert_payload_t *current, 
                            mesh_alert_payload_t *previous)
{
    bool res = false;

    if ((current->TX.TX_all_flags != previous->TX.TX_all_flags)
        || (current->RX.RX_all_flags != previous->RX.RX_all_flags))
    {
        res = true;
    }

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
    self_static_payload.id = self_dynamic_payload.TX.id = self_previous_dynamic_payload.TX.id = self_alert_payload.TX.id = self_previous_alert_payload.TX.id = CONFIG_UNIT_ID;

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

void update_status(struct TX_peer *peer)
{
    //TX status
    if (peer->alert_payload->TX.TX_all_flags)
        peer->dynamic_payload->TX.tx_status = TX_ALERT;
    else if (peer->dynamic_payload->TX.tx_status == TX_LOCALIZATION)
        peer->dynamic_payload->TX.tx_status = TX_LOCALIZATION;
    else if (peer->dynamic_payload->RX.macAddr[0] != 0)
        peer->dynamic_payload->TX.tx_status = TX_DEPLOY;
    else
        peer->dynamic_payload->TX.tx_status = TX_OFF;

    // RX status
    if (peer->alert_payload->RX.RX_internal.FullyCharged)
        peer->dynamic_payload->RX.rx_status = RX_FULLY_CHARGED;
    else if (peer->alert_payload->RX.RX_all_flags) {
        peer->dynamic_payload->RX.rx_status = RX_ALERT;
        peer->dynamic_payload->TX.tx_status = TX_ALERT;
    }
    else if (peer->dynamic_payload->RX.voltage > MIN_RX_VOLTAGE)
        peer->dynamic_payload->RX.rx_status = RX_CHARGING;
    else if (peer->dynamic_payload->RX.voltage > MISALIGNED_LIMIT)
        peer->dynamic_payload->RX.rx_status = RX_MISALIGNED;
    else if (peer->dynamic_payload->RX.id != 0)
        peer->dynamic_payload->RX.rx_status = RX_CONNECTED;
    else 
        peer->dynamic_payload->RX.rx_status = RX_NOT_PRESENT;
}
