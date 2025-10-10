#include "peer.h"

static const char *TAG = "PEER";

static SLIST_HEAD(, RX_peer) RX_peers;
static SLIST_HEAD(, TX_peer) TX_peers;

mesh_static_payload_t self_static_payload;
mesh_dynamic_payload_t self_dynamic_payload;
mesh_alert_payload_t self_alert_payload;
mesh_tuning_params_t self_tuning_params;

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

    /*

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
    SLIST_FOREACH(p, &TX_peers, next) {
        if (p->tx_status == TX_LOCALIZATION) {
            p->tx_status = TX_OFF;
        }
    }
}

struct TX_peer* find_next_TX_for_localization(int8_t previousTX_pos) {
    struct TX_peer *p;
    struct TX_peer *first_available = NULL;
    bool found_previous = (previousTX_pos == -1); // Start from beginning if no previous
    
    // Iterate through the list sequentially
    SLIST_FOREACH(p, &TX_peers, next) {
        if (p->tx_status == TX_OFF || p->tx_status == TX_LOCALIZATION) {
            // Store the first available TX for wrap-around
            if (first_available == NULL) {
                first_available = p;
            }
            
            // If we've already passed the previous TX, return this one
            if (found_previous) {
                return p;
            }
        }
        
        // Mark when we've found and passed the previous TX
        if (p->position == previousTX_pos) {
            found_previous = true;
        }
    }
    
    // Wrap around: return the first available TX in the list
    return first_available;
}

struct RX_peer* findRXpeerWPosition(uint8_t pos)
{
    struct RX_peer * p = NULL;

    SLIST_FOREACH(p, &RX_peers, next) 
    {
        if (p->position == pos)
            break;
    }

    return p;
}


void peer_init()
{
    delete_all_peers();

    SLIST_INIT(&RX_peers);
    SLIST_INIT(&TX_peers);
    struct TX_peer *p = TX_peer_add(self_mac);
    if (p != NULL) 
    {
        ESP_LOGI(TAG, "Added self TX peer with ID %d", CONFIG_UNIT_ID);
        p->id = CONFIG_UNIT_ID;
        p->position = p->id; // Position same as ID for TX

        // Free the malloc'd memory first to avoid leak
        free(p->static_payload);
        free(p->dynamic_payload);
        free(p->alert_payload);
        free(p->tuning_params);

        p->static_payload = &self_static_payload;
        p->dynamic_payload = &self_dynamic_payload;
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
    p->alert_payload = malloc(sizeof(mesh_alert_payload_t));
    p->tuning_params = malloc(sizeof(mesh_tuning_params_t));

    if (!p->static_payload || !p->dynamic_payload || 
        !p->alert_payload || !p->tuning_params) {
        ESP_LOGE(TAG, "Failed to allocate memory for payloads");
        // Free any allocated memory
        free(p->static_payload);
        free(p->dynamic_payload);
        free(p->alert_payload);
        free(p->tuning_params);
        free(p);
        return NULL;
    }

    memset(p->static_payload, 0, sizeof(mesh_static_payload_t));
    memset(p->dynamic_payload, 0, sizeof(mesh_dynamic_payload_t));
    memset(p->alert_payload, 0, sizeof(mesh_alert_payload_t));
    memset(p->tuning_params, 0, sizeof(mesh_tuning_params_t));

    /* Initialize peer parameters */
    memcpy(p->MACaddress, mac, 6);
    p->tx_status = TX_OFF;
    p->led_command = LED_CONNECTED;

    /* Add the peer to the list */
    SLIST_INSERT_HEAD(&TX_peers, p, next);

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
    p->alert_payload = malloc(sizeof(mesh_alert_payload_t));

    if (!p->static_payload || !p->dynamic_payload || 
        !p->alert_payload) {
        ESP_LOGE(TAG, "Failed to allocate memory for payloads");
        // Free any allocated memory
        free(p->static_payload);
        free(p->dynamic_payload);
        free(p->alert_payload);
        free(p);
        return NULL;
    }

    /* Set payloads to 0 */
    memset(p->static_payload, 0, sizeof(mesh_static_payload_t));
    memset(p->dynamic_payload, 0, sizeof(mesh_dynamic_payload_t));
    memset(p->alert_payload, 0, sizeof(mesh_alert_payload_t));

    /* Initialize peer parameters */
    memcpy(p->MACaddress, mac, 6);
    p->RX_status = RX_CONNECTED;
    p->position = -1; //not assigned yet

    /* Add the peer to the list */
    SLIST_INSERT_HEAD(&RX_peers, p, next);

    return p;
}

struct TX_peer* TX_peer_find_by_mac(uint8_t *mac)
{
    struct TX_peer *p;

    SLIST_FOREACH(p, &TX_peers, next) {
        if (memcmp(p->MACaddress, mac, 6) == 0) {
            return p;
        }
    }

    return NULL;
}

struct RX_peer* RX_peer_find_by_mac(uint8_t *mac)
{
    struct RX_peer *p;

    SLIST_FOREACH(p, &RX_peers, next) {
        if (memcmp(p->MACaddress, mac, 6) == 0) {
            return p;
        }
    }

    return NULL;
}

struct TX_peer* TX_peer_find_by_position(uint8_t position)
{
    struct TX_peer *p;

    SLIST_FOREACH(p, &TX_peers, next) {
        if (p->position == position)
            return p;
    }

    return NULL;
}

struct RX_peer* RX_peer_find_by_position(int8_t position)
{
    struct RX_peer *p;

    SLIST_FOREACH(p, &RX_peers, next) {
        if (p->position == position)
            return p;
    }

    return NULL;
}

void peer_delete(uint8_t *mac)
{
    struct TX_peer *TX_p = TX_peer_find_by_mac(mac);
    if(TX_p != NULL)
    {
        SLIST_REMOVE(&TX_peers, TX_p, TX_peer, next);
        // Free payload memory
        free(TX_p->static_payload);
        free(TX_p->dynamic_payload);
        free(TX_p->alert_payload);
        free(TX_p->tuning_params);
        // Free peer structure
        free(TX_p);
        return;
    }

    struct RX_peer *RX_p = RX_peer_find_by_mac(mac);
    if(RX_p != NULL)
    {
        SLIST_REMOVE(&RX_peers, RX_p, RX_peer, next);
        // Free payload memory
        free(RX_p->static_payload);
        free(RX_p->dynamic_payload);
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

    while (!SLIST_EMPTY(&TX_peers)) {
        TX_p = SLIST_FIRST(&TX_peers);
        SLIST_REMOVE_HEAD(&TX_peers, next);
        // Free payload memory
        free(TX_p->static_payload);
        free(TX_p->dynamic_payload);
        free(TX_p->alert_payload);
        free(TX_p->tuning_params);
        // Free peer structure
        free(TX_p);
    }

    while (!SLIST_EMPTY(&RX_peers)) {
        RX_p = SLIST_FIRST(&RX_peers);
        SLIST_REMOVE_HEAD(&RX_peers, next);
        // Free payload memory
        free(RX_p->static_payload);
        free(RX_p->dynamic_payload);
        free(RX_p->alert_payload);
        // Free peer structure
        free(RX_p);
    }
}

bool atLeastOneRxNeedLocalization()
{
    //check both RX and TX structs are not empty
    if (SLIST_EMPTY(&RX_peers) || SLIST_EMPTY(&TX_peers))
        return false;

    struct RX_peer *RX_p;
    struct TX_peer *TX_p;

    SLIST_FOREACH(RX_p, &RX_peers, next) 
    {
        if (RX_p->position == -1) //not localized yet
        {
            //check if at least one TX is available
            SLIST_FOREACH(TX_p, &TX_peers, next) 
            {
                if (TX_p->tx_status == TX_OFF || TX_p->tx_status == TX_LOCALIZATION) //available TX
                    return true;
            }
        }
    }

    return false;
}

#define DELTA_VOLTAGE 10.0
#define DELTA_CURRENT 1
#define DELTA_TEMPERATURE 1.0

bool dynamic_payload_changes(mesh_dynamic_payload_t *previous)
{
    if (previous == NULL)
    {
        ESP_LOGE(TAG, "Previous dynamic payload is NULL");
        return false;
    }

    bool res = false;

    if (fabs(self_dynamic_payload.TX.voltage - previous->TX.voltage) > DELTA_VOLTAGE) 
    {
        res = true;
        //ESP_LOGW(TAG, "Voltage change detected: %.2f %.2f V", self_dynamic_payload.TX.voltage, previous->TX.voltage);
        previous->TX.voltage = self_dynamic_payload.TX.voltage;
    }
    if (fabs(self_dynamic_payload.TX.current - previous->TX.current) > DELTA_CURRENT) 
    {
        res = true;
        //ESP_LOGW(TAG, "Current change detected: %.2f %.2f A", self_dynamic_payload.TX.current, previous->TX.current);
        previous->TX.current = self_dynamic_payload.TX.current;
    }
    if (fabs(self_dynamic_payload.TX.temp1 - previous->TX.temp1) > DELTA_TEMPERATURE) 
    {
        res = true;
        //ESP_LOGW(TAG, "Temp1 change detected: %.2f %.2f C", self_dynamic_payload.TX.temp1, previous->TX.temp1);
        previous->TX.temp1 = self_dynamic_payload.TX.temp1;
    }
    if (fabs(self_dynamic_payload.TX.temp2 - previous->TX.temp2) > DELTA_TEMPERATURE) 
    {
        res = true;
        //ESP_LOGW(TAG, "Temp2 change detected: %.2f %.2f C", self_dynamic_payload.TX.temp2, previous->TX.temp2);
        previous->TX.temp2 = self_dynamic_payload.TX.temp2;
    }
    if (fabs(self_dynamic_payload.RX.voltage - previous->RX.voltage) > DELTA_VOLTAGE) 
    {
        res = true;
        //ESP_LOGW(TAG, "Voltage change detected: %.2f %.2f V", self_dynamic_payload.RX.voltage, previous->RX.voltage);
        previous->RX.voltage = self_dynamic_payload.RX.voltage;
    }
    if (fabs(self_dynamic_payload.RX.current - previous->RX.current) > DELTA_CURRENT) 
    {
        res = true;
        //ESP_LOGW(TAG, "Current change detected: %.2f %.2f A", self_dynamic_payload.RX.current, previous->RX.current);
        previous->RX.current = self_dynamic_payload.RX.current;
    }
    if (fabs(self_dynamic_payload.RX.temp1 - previous->RX.temp1) > DELTA_TEMPERATURE) 
    {
        res = true;
        //ESP_LOGW(TAG, "Temp1 change detected: %.2f %.2f C", self_dynamic_payload.RX.temp1, previous->RX.temp1);
        previous->RX.temp1 = self_dynamic_payload.RX.temp1;
    }
    if (fabs(self_dynamic_payload.RX.temp2 - previous->RX.temp2) > DELTA_TEMPERATURE) 
    {
        res = true;
        //ESP_LOGW(TAG, "Temp2 change detected: %.2f %.2f C", self_dynamic_payload.RX.temp2, previous->RX.temp2);
        previous->RX.temp2 = self_dynamic_payload.RX.temp2;
    }

    return res;
}

bool alert_payload_changes(mesh_alert_payload_t *previous)
{
    if (previous == NULL)
    {
        ESP_LOGE(TAG, "Previous alert payload is NULL");
        return false;
    }

    bool res = false;

    if (self_alert_payload.RX.RX_all_flags != previous->RX.RX_all_flags || self_alert_payload.TX.TX_all_flags != previous->TX.TX_all_flags)
        res = true;

    *previous = self_alert_payload;

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

