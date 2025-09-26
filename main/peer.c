#include "peer.h"

static const char *TAG = "PEER";

static SLIST_HEAD(, RX_peer) RX_peers;
static SLIST_HEAD(, TX_peer) TX_peers;

wpt_dynamic_payload_t dynamic_payload;
wpt_alert_payload_t alert_payload;
wpt_tuning_params_t tuning_params;

void peer_init()
{
    SLIST_INIT(&RX_peers);
    SLIST_INIT(&TX_peers);
}

uint8_t TX_peer_add(uint8_t *mac)
{
    struct TX_peer *p;

    /*Make sure the peer does not exist in the memory pool yet*/
    p = TX_peer_find_by_mac(mac);
    if (p) 
    {
        ESP_LOGE(TAG, "Peer already exists in the memory pool");
        return ESP_FAIL;
    }

    /*Allocate memory for the peer*/
    p = malloc(sizeof * p);
    if (!p) 
    {
        ESP_LOGE(TAG, "Failed to allocate memory for peer");
        return ESP_FAIL;
    }

    /* Set everything to 0 */
    memset(p, 0, sizeof * p);

    /* Initialize peer parameters */
    memcpy(p->MACaddress, mac, 6);
    p->tx_status = TX_DISCONNECTED;
    p->led_command = LED_CONNECTED;
    p->position = -1; //not assigned yet

    /* Add the peer to the list */
    SLIST_INSERT_HEAD(&TX_peers, p, next);

    return ESP_OK;
}

uint8_t RX_peer_add(uint8_t *mac)
{
    struct RX_peer *p;

    /*Make sure the peer does not exist in the memory pool yet*/
    p = RX_peer_find_by_mac(mac);
    if (p) 
    {
        ESP_LOGE(TAG, "Peer already exists in the memory pool");
        return ESP_FAIL;
    }

    /*Allocate memory for the peer*/
    p = malloc(sizeof * p);
    if (!p) 
    {
        ESP_LOGE(TAG, "Failed to allocate memory for peer");
        return ESP_FAIL;
    }

    /* Set everything to 0 */
    memset(p, 0, sizeof * p);

    /* Initialize peer parameters */
    memcpy(p->MACaddress, mac, 6);
    p->RX_status = RX_DISCONNECTED;
    p->position = -1; //not assigned yet

    /* Add the peer to the list */
    SLIST_INSERT_HEAD(&RX_peers, p, next);

    return ESP_OK;
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
        free(TX_p);
        return;
    }

    struct RX_peer *RX_p = RX_peer_find_by_mac(mac);
    if(RX_p != NULL)
    {
        SLIST_REMOVE(&RX_peers, RX_p, RX_peer, next);
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
        free(TX_p);
    }

    while (!SLIST_EMPTY(&RX_peers)) {
        RX_p = SLIST_FIRST(&RX_peers);
        SLIST_REMOVE_HEAD(&RX_peers, next);
        free(RX_p);
    }
}