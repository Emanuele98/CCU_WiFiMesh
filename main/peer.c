#include "peer.h"

static const char *TAG = "PEER";

static SLIST_HEAD(, RX_peer) RX_peers;
static SLIST_HEAD(, TX_peer) TX_peers;

mesh_static_payload_t self_static_payload;
mesh_dynamic_payload_t self_dynamic_payload;
mesh_alert_payload_t self_alert_payload;
mesh_tuning_params_t self_tuning_params;

void peer_init()
{
    delete_all_peers();

    SLIST_INIT(&RX_peers);
    SLIST_INIT(&TX_peers);
    struct TX_peer *p = TX_peer_add(self_mac);
    if (p != NULL) {
        p->id = CONFIG_UNIT_ID;
        p->position = p->id; // Position same as ID for TX
    }

    p->static_payload = &self_static_payload;
    p->dynamic_payload = &self_dynamic_payload;
    p->alert_payload = &self_alert_payload;
    p->tuning_params = &self_tuning_params;
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